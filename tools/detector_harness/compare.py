"""Compare two detector_harness CSVs (a reference plugin and a candidate),
to pick the candidate's confidence threshold so it behaves like the reference.

    python compare.py --ref yolov8.csv --cand yolox.csv [--ref-threshold 0.65]
                      [--labels <deep_tag annotations dir>] [--group person]

For each candidate threshold it reports, per group (person / vehicle / animal,
as r_early_alerts groups classes):
  agree      objects both find (same group, IoU >= 0.5)
  cand_only  objects only the candidate finds (extra detections, or misses by the reference)
  ref_only   objects only the reference finds
With --labels (deep_tag / Pascal VOC XML, frames letterboxed to 640 as revere
does), also the recall of each on the labelled objects. Labelled sets may be
incomplete (unlabelled people), so only recall is meaningful, not precision.

Standard library only.
"""

import argparse
import csv
import glob
import os
import xml.etree.ElementTree as ET
from collections import defaultdict

FRAME = 640
GROUPS = {
    "person": {"person"},
    "vehicle": {"car", "truck", "bus", "motorcycle", "motorbike", "bicycle"},
    "animal": {"bird", "cat", "dog", "horse", "sheep", "cow", "bear"},
}


def group_of(name):
    for g, names in GROUPS.items():
        if name in names:
            return g
    return None


def load(path):
    dets = defaultdict(list)
    images = set()
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            images.add(r["image"])
            if not r["class_name"]:
                continue
            g = group_of(r["class_name"])
            if g:
                dets[r["image"]].append((g, float(r["confidence"]),
                                         tuple(float(r[k]) for k in ("x1", "y1", "x2", "y2"))))
    return dets, images


def load_labels(ann_dir):
    labels = defaultdict(list)
    for f in glob.glob(os.path.join(ann_dir, "*.xml")):
        root = ET.parse(f).getroot()
        w = int(root.find("size/width").text)
        h = int(root.find("size/height").text)
        s = min(FRAME / w, FRAME / h)
        px, py = (FRAME - int(w * s)) // 2, (FRAME - int(h * s)) // 2
        image = root.find("filename").text
        for o in root.iter("object"):
            g = group_of(o.find("name").text) or ("vehicle" if o.find("name").text == "vehicle" else None)
            if not g:
                continue
            b = o.find("bndbox")
            x1, y1, x2, y2 = (float(b.find(k).text) for k in ("xmin", "ymin", "xmax", "ymax"))
            labels[image].append((g, 1.0, (x1 * s + px, y1 * s + py, x2 * s + px, y2 * s + py)))
    return labels


def iou(a, b):
    ix1, iy1, ix2, iy2 = max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])
    if ix2 <= ix1 or iy2 <= iy1:
        return 0.0
    inter = (ix2 - ix1) * (iy2 - iy1)
    return inter / ((a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter)


def match(a, b, group, thr=0.5):
    """Greedy one-to-one matching of group's boxes; returns (matched, a_only, b_only)."""
    a = sorted([d for d in a if d[0] == group], key=lambda d: -d[1])
    b = [d for d in b if d[0] == group]
    used = set()
    m = 0
    for d in a:
        best, bi = thr, None
        for i, e in enumerate(b):
            if i not in used and iou(d[2], e[2]) >= best:
                best, bi = iou(d[2], e[2]), i
        if bi is not None:
            used.add(bi)
            m += 1
    return m, len(a) - m, len(b) - m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--cand", required=True)
    ap.add_argument("--ref-threshold", type=float, default=0.65)
    ap.add_argument("--labels", action="append", default=[], help="annotations dir (repeatable)")
    ap.add_argument("--thresholds", default="0.30,0.35,0.40,0.45,0.50,0.55,0.60,0.65,0.70,0.75")
    args = ap.parse_args()

    ref, ref_images = load(args.ref)
    cand, cand_images = load(args.cand)
    images = sorted(ref_images & cand_images)
    labels = defaultdict(list)
    for d in args.labels:
        for k, v in load_labels(d).items():
            labels[k].extend(v)
    print(f"{len(images)} images in both; reference at {args.ref_threshold}")

    def above(dets, t):
        return [d for d in dets if d[1] >= t]

    for group in GROUPS:
        n_ref = sum(len([d for d in above(ref[i], args.ref_threshold) if d[0] == group]) for i in images)
        n_lab = sum(len([d for d in labels.get(i, []) if d[0] == group]) for i in images)
        if n_ref == 0 and n_lab == 0 and not any(d[0] == group for i in images for d in cand[i]):
            continue
        print(f"\n== {group}: reference finds {n_ref}" + (f", {n_lab} labelled" if n_lab else ""))
        if n_lab:
            hit = sum(match(labels[i], above(ref[i], args.ref_threshold), group)[0] for i in images)
            print(f"   reference recall on labelled: {hit}/{n_lab} = {hit / n_lab:.1%}")
        print(f"   {'thresh':>6} {'cand':>5} {'agree':>5} {'cand_only':>9} {'ref_only':>8}" + ("  label_recall" if n_lab else ""))
        for t in (float(x) for x in args.thresholds.split(",")):
            n_cand = agree = c_only = r_only = hit = 0
            for i in images:
                c = above(cand[i], t)
                m, co, ro = match(c, above(ref[i], args.ref_threshold), group)
                n_cand += m + co
                agree += m
                c_only += co
                r_only += ro
                if n_lab:
                    hit += match(labels.get(i, []), c, group)[0]
            line = f"   {t:>6.2f} {n_cand:>5} {agree:>5} {c_only:>9} {r_only:>8}"
            if n_lab:
                line += f"  {hit}/{n_lab} = {hit / n_lab:.1%}"
            print(line)


if __name__ == "__main__":
    main()
