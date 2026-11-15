#!/bin/bash
#
# Checks that every library each Mach-O file under the paths loads is one
# macOS provides or one the app bundle ships. Exits 1 if any is not.
#
#   deps.sh [--app <Some.app>] <path>...
#
# A dependency passes when it is
#   - under /usr/lib/ or /System/ (macOS's own), or
#   - @executable_path/, @loader_path/ or @rpath/ and names a file that exists.
# Any other absolute path fails, weak or not: it is the build machine's
# (/Users/runner/..., /opt/homebrew/..., /usr/local/...) and a user's Mac either
# lacks it or has some other build there.
#
# @executable_path is the Contents/MacOS of the .app the file is in. A file
# outside any .app (the cloud plugin, which revere dlopens) needs --app to say
# which app loads it. @rpath is tried against the file's own LC_RPATHs and those
# of the app's main executable. A weak dependency named relative to the bundle
# may be missing (dyld skips it); an absolute one still fails.
#
# Each architecture of a file is read on its own.
#
set -uo pipefail

app_opt=""
if [ "${1:-}" = "--app" ]; then app_opt="${2:-}"; shift 2 || true; fi
[ $# -gt 0 ] || { echo "usage: $0 [--app <Some.app>] <path>..." >&2; exit 2; }
if [ -n "$app_opt" ] && [ ! -d "$app_opt/Contents/MacOS" ]; then
  echo "::error::$app_opt is not an app bundle" >&2; exit 2
fi

# "<kind> <name>" for each library one architecture ($2) of the file loads, kind
# being "weak" or "strong". Not LC_ID_DYLIB: a library's own name isn't a load.
deps_of() {
  otool -arch "$2" -l "$1" 2>/dev/null | awk '
    $1=="cmd" { k = ($2=="LC_LOAD_WEAK_DYLIB") ? "weak" : (($2 ~ /^LC_(LOAD|REEXPORT|LOAD_UPWARD|LAZY_LOAD)_DYLIB$/) ? "strong" : ""); next }
    k!="" && $1=="name" { print k, $2; k="" }'
}

rpaths_of() {
  otool -arch "$2" -l "$1" 2>/dev/null | awk '/cmd LC_RPATH/{r=1; next} r && $1=="path"{print $2; r=0}'
}

# The .app a path is inside, or empty.
app_of() {
  case "$1" in
    *.app/*) echo "${1%%.app/*}.app";;
    *) echo "";;
  esac
}

main_exe_of() {  # $1 = .app
  local name
  name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$1/Contents/Info.plist" 2>/dev/null)"
  [ -n "$name" ] && [ -f "$1/Contents/MacOS/$name" ] && echo "$1/Contents/MacOS/$name"
}

# $1 with @executable_path ($2) and @loader_path ($3) put in; empty if it needs
# an @executable_path no app gives.
expand() {
  local p="$1"
  case "$p" in
    @executable_path/*) [ -n "$2" ] || return 0; p="$2/${p#@executable_path/}";;
    @loader_path/*) p="$3/${p#@loader_path/}";;
    @loader_path) p="$3";;
    @executable_path) [ -n "$2" ] || return 0; p="$2";;
  esac
  echo "$p"
}

macho_files() {
  find "$@" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"
  done
}

bad=0
checked=0
while IFS= read -r -d '' f; do
  checked=$((checked+1))
  app="$(app_of "$f")"; [ -n "$app" ] || app="$app_opt"
  exedir=""; exe=""
  if [ -n "$app" ]; then exedir="$app/Contents/MacOS"; exe="$(main_exe_of "$app")"; fi
  loaderdir="$(dirname "$f")"
  for a in $(lipo -archs "$f" 2>/dev/null); do
    rps="$(rpaths_of "$f" "$a")"
    if [ -n "$exe" ] && [ "$exe" != "$f" ]; then
      rps="$rps
$(rpaths_of "$exe" "$a" | sed "s|^@loader_path|@executable_path|")"
    fi
    while read -r kind dep; do
      [ -n "$dep" ] || continue
      case "$dep" in
        /usr/lib/*|/System/*) continue;;
        /*) echo "::error::$f ($a) loads $dep, a path outside the bundle and macOS"; bad=1; continue;;
        @rpath/*)
          found=""
          while IFS= read -r rp; do
            [ -n "$rp" ] || continue
            d="$(expand "$rp" "$exedir" "$loaderdir")"
            [ -n "$d" ] && [ -e "$d/${dep#@rpath/}" ] && { found="$d/${dep#@rpath/}"; break; }
          done <<< "$rps"
          ;;
        @executable_path/*|@loader_path/*)
          p="$(expand "$dep" "$exedir" "$loaderdir")"
          found=""; [ -n "$p" ] && [ -e "$p" ] && found="$p"
          ;;
        *) found="";;
      esac
      if [ -z "$found" ] && [ "$kind" != "weak" ]; then
        if [ -z "$exedir" ] && [ "${dep#@executable_path}" != "$dep" ]; then
          echo "::error::$f ($a) loads $dep; give --app to say which app loads it"
        else
          echo "::error::$f ($a) loads $dep, which the bundle doesn't have"
        fi
        bad=1
      fi
    done < <(deps_of "$f" "$a")
  done
done < <(macho_files "$@")

[ "$checked" -gt 0 ] || { echo "::error::no Mach-O files under: $*"; exit 1; }
[ "$bad" = 0 ] && echo "every library loaded is macOS's or the bundle's ($checked files): $*"
exit "$bad"
