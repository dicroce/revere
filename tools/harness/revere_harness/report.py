from __future__ import annotations

import json
import pathlib
import xml.etree.ElementTree as ET
from typing import Any

from revere_harness.steps import StepResult


def _escape_md(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", "; ")


def write_reports(
    out_dir: pathlib.Path,
    suite: str,
    results: list[StepResult],
    meta: dict[str, Any],
    resources: dict[str, dict],
) -> pathlib.Path:
    out_dir.mkdir(parents=True, exist_ok=True)

    passed = sum(1 for r in results if r.status == "passed")
    failed = sum(1 for r in results if r.status == "failed")
    skipped = sum(1 for r in results if r.status == "skipped")
    total_seconds = sum(r.seconds for r in results)

    summary = {
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "seconds": round(total_seconds, 2),
    }

    json_data = {
        "suite": suite,
        "meta": meta,
        "summary": summary,
        "steps": [
            {
                "suite": r.suite,
                "name": r.name,
                "status": r.status,
                "seconds": r.seconds,
                "detail": r.detail,
            }
            for r in results
        ],
        "resources": resources,
    }

    json_path = out_dir / "results.json"
    json_path.write_text(json.dumps(json_data, indent=2))

    root = ET.Element("testsuite")
    root.set("name", suite)
    root.set("tests", str(len(results)))
    root.set("failures", str(failed))
    root.set("skipped", str(skipped))
    root.set("time", f"{total_seconds:.2f}")

    for r in results:
        tc = ET.SubElement(root, "testcase")
        tc.set("name", r.name)
        tc.set("classname", r.suite)
        tc.set("time", f"{r.seconds:.2f}")
        if r.status == "failed":
            detail_lines = r.detail.splitlines()
            msg = detail_lines[0] if detail_lines else ""
            fail_el = ET.SubElement(tc, "failure")
            fail_el.set("message", msg)
            fail_el.text = r.detail
        elif r.status == "skipped":
            skip_el = ET.SubElement(tc, "skipped")
            skip_el.set("message", r.detail)

    junit_path = out_dir / "junit.xml"
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(junit_path, encoding="unicode", xml_declaration=True)

    md_lines: list[str] = []
    md_lines.append(f"# Harness report: {suite}")
    
    if "suite_error" in meta:
        md_lines.append(f"**The suite stopped early:** {meta['suite_error']}")
    
    md_lines.append("")
    md_lines.append(
        f"**{passed} passed, {failed} failed, {skipped} skipped** in {total_seconds:.2f} s"
    )
    md_lines.append("")
    md_lines.append("## Meta")
    md_lines.append("| Key | Value |")
    md_lines.append("| --- | ----- |")
    for k, v in meta.items():
        md_lines.append(f"| {k} | {v} |")
    md_lines.append("")
    md_lines.append("## Steps")
    md_lines.append("| Step | Result | Seconds | Detail |")
    md_lines.append("| ---- | ------ | ------- | ------ |")
    for r in results:
        detail = _escape_md(r.detail) if r.detail else ""
        md_lines.append(f"| {r.name} | {r.status} | {r.seconds:.2f} | {detail} |")
    md_lines.append("")
    md_lines.append("## Resources")
    md_lines.append("| Process | Samples | Max CPU % | RSS first MB | RSS last MB | RSS max MB | Max threads | Max fds |")
    md_lines.append("| ------- | ------- | --------- | ------------ | ----------- | ---------- | ----------- | ------- |")
    if resources:
        for label, stats in resources.items():
            md_lines.append(
                f"| {label} | {stats['samples']} | {stats['cpu_percent_max']:.1f} "
                f"| {stats['rss_mb_first']:.1f} | {stats['rss_mb_last']:.1f} "
                f"| {stats['rss_mb_max']:.1f} | {stats['threads_max']} | {stats['fds_max']} |"
            )
    else:
        md_lines.append("No resource samples were taken.")
    md_lines.append("")
    md_lines.append("## Files")
    for p in sorted(out_dir.iterdir()):
        if p.is_dir():
            md_lines.append(f"- {p.name}/")
        else:
            md_lines.append(f"- {p.name}")

    report_path = out_dir / "report.md"
    report_path.write_text("\n".join(md_lines) + "\n")

    return report_path