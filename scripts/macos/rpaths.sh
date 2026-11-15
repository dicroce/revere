#!/bin/bash
#
# Finds, and with --fix removes, repeated LC_RPATH entries in the Mach-O files
# under each path given (an .app, a directory, or a single file).
#
# dyld on macOS 15.4 and later refuses to load a library that lists the same
# rpath twice ("duplicate LC_RPATH"). dylibbundler -p makes such libraries: it
# rewrites every rpath a library already has to the one -p path, so a library
# CMake built with two (@loader_path and @executable_path/../lib) leaves with
# @executable_path/../libs/ twice.
#
#   rpaths.sh <path>...        list each file with a repeat; exit 1 if any
#   rpaths.sh --fix <path>...  keep one of each repeat, re-sign ad hoc what
#                              changed; then check, as above. A universal file
#                              is reported, never changed.
#
# Each architecture of a file is read on its own: a universal file lists one
# set of load commands per slice, so its rpaths would otherwise all repeat.
#
set -uo pipefail

fix=0
if [ "${1:-}" = "--fix" ]; then fix=1; shift; fi
[ $# -gt 0 ] || { echo "usage: $0 [--fix] <path>..." >&2; exit 2; }

# The LC_RPATH paths of one architecture of the file ($2), one per line, in
# load-command order. A universal file lists each slice's own.
rpaths_of() {
  otool -arch "$2" -l "$1" 2>/dev/null | awk '/cmd LC_RPATH/{r=1; next} r && $1=="path"{print $2; r=0}'
}

# Each rpath a slice repeats, as "<arch>: <path>".
repeats_of() {
  local a
  for a in $(lipo -archs "$1" 2>/dev/null); do rpaths_of "$1" "$a" | sort | uniq -d | sed "s|^|$a: |"; done
}

# Every Mach-O file under the paths (not symlinks: their targets are listed).
macho_files() {
  find "$@" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"
  done
}

if [ "$fix" = 1 ]; then
  while IFS= read -r -d '' f; do
    changed=0
    archs="$(lipo -archs "$f" 2>/dev/null)"
    # -delete_rpath changes every slice at once, so a universal file is left
    # alone; the check below still reports a repeat in it.
    if [ "$(echo $archs | wc -w)" -ne 1 ]; then
      [ -z "$(repeats_of "$f")" ] || echo "::error::$f is universal ($archs); not changing it"
      continue
    fi
    while IFS= read -r p; do
      [ -n "$p" ] || continue
      # Each -delete_rpath removes the first matching entry; leave exactly one.
      while [ "$(rpaths_of "$f" "$archs" | grep -cxF -- "$p")" -gt 1 ]; do
        chmod u+w "$f"
        install_name_tool -delete_rpath "$p" "$f" 2>/dev/null || { echo "::error::could not remove a repeated rpath $p from $f"; break; }
        changed=1
      done
    done < <(rpaths_of "$f" "$archs" | sort | uniq -d)
    if [ "$changed" = 1 ]; then
      # install_name_tool invalidated the signature; arm64 won't load it unsigned.
      codesign --force --sign - "$f" >/dev/null 2>&1 || echo "::error::codesign failed for $f"
      echo "fixed: $f"
    fi
  done < <(macho_files "$@")
fi

bad=0
while IFS= read -r -d '' f; do
  d="$(repeats_of "$f" | tr '\n' ' ')"
  if [ -n "$d" ]; then
    echo "::error::duplicate LC_RPATH in $f: $d"
    bad=1
  fi
done < <(macho_files "$@")
[ "$bad" = 0 ] && echo "no duplicate LC_RPATH in: $*"
exit "$bad"
