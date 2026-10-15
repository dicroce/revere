#!/usr/bin/env bash
# Regenerates the Revere desktop icons and the web UI favicon from the mark in
# this folder. The mark is a 40-unit square outline with a 10-unit square
# stamp, on a 64-unit grid (revere-mark-compact.svg); the icons put it in white
# on an ink (#18181b) tile with square corners.
#
#   apps/revere/R.ico          Windows exe and tray icon: 16, 32, 48, 256
#   apps/revere/R.png          256: tray icon (Linux) and the Linux install icon
#   apps/revere/R-menubar.png  36: macOS menu bar icon, the bare mark as a template image
#   apps/revere/revere.icns    macOS app bundle icon
#   snap/gui/revere.png        256: snap desktop icon
#   libs/r_vss/webui/public/favicon.svg, favicon-32.png   web UI favicon
#
# Needs rsvg-convert (librsvg) and ImageMagick 7 (`magick`); iconutil on macOS
# for the .icns (skipped elsewhere). Run from anywhere:
#   tools/icons/build-icons.sh
# The web UI favicon is copied into public/; rebuild the web UI (npm run build)
# to carry it into dist/.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INK='#18181b'
PAPER='#ffffff'
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# A tile SVG on a 1024 canvas. $1 = the tile's size inside the canvas (1024 is
# full bleed; macOS icons sit on an 824 square with room around it), $2 = the
# share of the tile the mark's outer edge spans, $3 = the corner radius (0 for
# square corners). macOS 26 puts any icon that isn't Apple's rounded square on
# a grey tile of its own, so the macOS tile has the grid's corners (185 on 824).
tile_svg() {
  local tile=$1 share=$2 radius=${3:-0}
  awk -v tile="$tile" -v share="$share" -v radius="$radius" -v ink="$INK" -v paper="$PAPER" 'BEGIN {
    inset = (1024 - tile) / 2
    # The outline'"'"'s outer edge runs 10.25 to 53.75 on the 64 grid: 43.5 units.
    s = share * tile / 43.5
    t = 512 - 32 * s
    printf "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\" width=\"1024\" height=\"1024\">\n"
    printf "  <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" fill=\"%s\"/>\n", inset, inset, tile, tile, radius, ink
    printf "  <g transform=\"translate(%g %g) scale(%g)\" fill=\"none\">\n", t, t, s
    printf "    <rect x=\"12\" y=\"12\" width=\"40\" height=\"40\" stroke=\"%s\" stroke-width=\"3.5\"/>\n", paper
    printf "    <rect x=\"27\" y=\"27\" width=\"10\" height=\"10\" fill=\"%s\"/>\n", paper
    printf "  </g>\n</svg>\n"
  }'
}

# Small sizes are drawn on the pixel grid instead, so the outline and stamp
# stay crisp: the same proportions (the mark spans half the tile) with
# whole-pixel edges. $1 = size; outline from $2 to $3 with a $4 px stroke;
# stamp from $5 to $6 (inclusive pixel coordinates).
pixel_tile() {
  local n=$1 a=$2 b=$3 w=$4 c=$5 d=$6 out=$7
  magick -size "${n}x${n}" "xc:$INK" +antialias -stroke none \
    -fill "$PAPER" -draw "rectangle $a,$a $b,$b" \
    -fill "$INK" -draw "rectangle $((a + w)),$((a + w)) $((b - w)),$((b - w))" \
    -fill "$PAPER" -draw "rectangle $c,$c $d,$d" "PNG32:$out"
}

render() { rsvg-convert -w "$2" -h "$2" "$1" -o "$3"; }

# Desktop tiles: full bleed, the mark at half the tile.
tile_svg 1024 0.5 > "$WORK/tile.svg"
render "$WORK/tile.svg" 256 "$WORK/tile-256.png"
pixel_tile 16 4 11 1 7 8 "$WORK/tile-16.png"
pixel_tile 32 8 23 2 14 17 "$WORK/tile-32.png"
pixel_tile 48 12 35 2 21 26 "$WORK/tile-48.png"

magick "$WORK/tile-16.png" "$WORK/tile-32.png" "$WORK/tile-48.png" "$WORK/tile-256.png" "$ROOT/apps/revere/R.ico"
cp "$WORK/tile-256.png" "$ROOT/apps/revere/R.png"
cp "$WORK/tile-256.png" "$ROOT/snap/gui/revere.png"

# macOS menu bar: the bare mark, black on transparent, nearly filling the 18 pt
# (36 px) image traypp shows. macOS draws it as a template, so it takes the
# menu bar's own colour in light and dark mode. Pixel-grid: outline 2..33 with
# a 3 px stroke, stamp 14..21.
magick -size 36x36 xc:none +antialias -fill black -draw "rectangle 2,2 33,33" \
  \( -size 26x26 xc:black \) -geometry +5+5 -compose DstOut -composite -compose Over \
  -fill black -draw "rectangle 14,14 21,21" "PNG32:$ROOT/apps/revere/R-menubar.png"

# macOS: the 824 tile of Apple's icon grid, transparent around it.
if command -v iconutil >/dev/null; then
  tile_svg 824 0.5 185 > "$WORK/mac.svg"
  set_dir="$WORK/revere.iconset"
  mkdir "$set_dir"
  for n in 16 32 128 256 512; do
    render "$WORK/mac.svg" "$n" "$set_dir/icon_${n}x${n}.png"
    render "$WORK/mac.svg" $((n * 2)) "$set_dir/icon_${n}x${n}@2x.png"
  done
  iconutil -c icns "$set_dir" -o "$ROOT/apps/revere/revere.icns"
else
  echo "iconutil not found (not macOS): left apps/revere/revere.icns as it is" >&2
fi

# Web UI favicon: the plain mark, ink on light tabs and white on dark ones.
cp "$HERE/favicon.svg" "$ROOT/libs/r_vss/webui/public/favicon.svg"
rsvg-convert -w 32 -h 32 "$HERE/favicon.svg" -o "$ROOT/libs/r_vss/webui/public/favicon-32.png"

echo "Icons written."
