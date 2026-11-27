#!/usr/bin/env bash
# Regenerates the Revere desktop icons and the web UI favicon from the mark in
# this folder: the Shutter, four blades around a square opening on a 64-unit
# grid, in two cuts (revere-mark.svg for 48 px and up, revere-mark-compact.svg
# for 32 px and below). The icons put it in white on an ink (#18181b) tile with
# square corners, the mark spanning half the tile.
#
#   apps/revere/R.ico          Windows exe and tray icon: 16, 32, 48, 256
#   apps/revere/R.png          256: tray icon (Linux) and the Linux install icon
#   apps/revere/R-menubar.png  36: macOS menu bar icon, the bare mark as a template image
#   apps/revere/revere.icns    macOS app bundle icon
#   apps/vision/vision.icns    macOS app bundle icon for Vision: its own mark, a square outline with a play
#                              triangle, on the same tile
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

# The Shutter's blades on the 64 grid, as x y w h: $1 = display or compact.
blades() {
  if [ "$1" = compact ]; then
    echo "12 12 24 12" "40 12 12 24" "28 40 24 12" "12 28 12 24"
  else
    echo "12 12 25 13" "39 12 13 25" "27 39 25 13" "12 27 13 25"
  fi
}

# A Revere tile SVG on a 1024 canvas. $1 = the tile's size inside the canvas
# (1024 is full bleed; macOS icons sit on an 824 square with room around it),
# $2 = the share of the tile the mark's outer bounds (12 to 52, 40 units) span,
# $3 = the corner radius (0 for square corners), $4 = the cut (display or
# compact). macOS 26 puts any icon that isn't Apple's rounded square on a grey
# tile of its own, so the macOS tile has the grid's corners (185 on 824).
tile_svg() {
  local tile=$1 share=$2 radius=${3:-0} cut=${4:-display}
  awk -v tile="$tile" -v share="$share" -v radius="$radius" -v ink="$INK" -v paper="$PAPER" -v blades="$(blades "$cut")" 'BEGIN {
    inset = (1024 - tile) / 2
    s = share * tile / 40
    t = 512 - 32 * s
    printf "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\" width=\"1024\" height=\"1024\">\n"
    printf "  <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" fill=\"%s\"/>\n", inset, inset, tile, tile, radius, ink
    printf "  <g transform=\"translate(%g %g) scale(%g)\" fill=\"%s\">\n", t, t, s, paper
    n = split(blades, v, " ")
    for (i = 1; i <= n; i += 4)
      printf "    <rect x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\"/>\n", v[i], v[i + 1], v[i + 2], v[i + 3]
    printf "  </g>\n</svg>\n"
  }'
}

# Vision's tile: its own mark, a square outline with a play triangle as the
# stamp (as make_app_icons.py draws it), unchanged. Arguments as tile_svg's,
# without the cut.
vision_tile_svg() {
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
    printf "    <polygon points=\"27.5,24.5 41.5,32 27.5,39.5\" fill=\"%s\"/>\n", paper
    printf "  </g>\n</svg>\n"
  }'
}

# The size an icon is shown at picks the cut: compact at 32 px and below,
# display from 48 px.
cut_for() { if [ "$1" -le 32 ]; then echo compact; else echo display; fi; }

render() { rsvg-convert -w "$2" -h "$2" "$1" -o "$3"; }

# Desktop tiles: full bleed, the mark at half the tile, each size rendered from
# the cut for its size.
tile_svg 1024 0.5 0 display > "$WORK/tile.svg"
tile_svg 1024 0.5 0 compact > "$WORK/tile-compact.svg"
render "$WORK/tile.svg" 256 "$WORK/tile-256.png"
render "$WORK/tile.svg" 48 "$WORK/tile-48.png"
render "$WORK/tile-compact.svg" 32 "$WORK/tile-32.png"
render "$WORK/tile-compact.svg" 16 "$WORK/tile-16.png"

magick "$WORK/tile-16.png" "$WORK/tile-32.png" "$WORK/tile-48.png" "$WORK/tile-256.png" "$ROOT/apps/revere/R.ico"
cp "$WORK/tile-256.png" "$ROOT/apps/revere/R.png"
cp "$WORK/tile-256.png" "$ROOT/snap/gui/revere.png"

# macOS menu bar: the bare mark, black on transparent, its outer bounds about 2 to 34
# of the 18 pt (36 px) image traypp shows: the compact cut, since it's shown at
# 18 pt. macOS draws it as a template, so it takes the menu bar's own colour in
# light and dark mode.
sed -e 's/fill="currentColor"/fill="#000000"/' -e 's/viewBox="0 0 64 64"/viewBox="10 10 44 44"/' \
  "$HERE/revere-mark-compact.svg" > "$WORK/menubar.svg"
render "$WORK/menubar.svg" 36 "$ROOT/apps/revere/R-menubar.png"

# macOS: the 824 tile of Apple's icon grid, transparent around it.
if command -v iconutil >/dev/null; then
  tile_svg 824 0.5 185 display > "$WORK/mac-display.svg"
  tile_svg 824 0.5 185 compact > "$WORK/mac-compact.svg"
  set_dir="$WORK/revere.iconset"
  mkdir "$set_dir"
  for n in 16 32 128 256 512; do
    render "$WORK/mac-$(cut_for "$n").svg" "$n" "$set_dir/icon_${n}x${n}.png"
    render "$WORK/mac-$(cut_for $((n * 2))).svg" $((n * 2)) "$set_dir/icon_${n}x${n}@2x.png"
  done
  iconutil -c icns "$set_dir" -o "$ROOT/apps/revere/revere.icns"

  # Vision: the same tile with the play triangle.
  vision_tile_svg 824 0.5 185 > "$WORK/mac-vision.svg"
  set_dir="$WORK/vision.iconset"
  mkdir "$set_dir"
  for n in 16 32 128 256 512; do
    render "$WORK/mac-vision.svg" "$n" "$set_dir/icon_${n}x${n}.png"
    render "$WORK/mac-vision.svg" $((n * 2)) "$set_dir/icon_${n}x${n}@2x.png"
  done
  iconutil -c icns "$set_dir" -o "$ROOT/apps/vision/vision.icns"
else
  echo "iconutil not found (not macOS): left apps/revere/revere.icns and apps/vision/vision.icns as they are" >&2
fi

# Web UI favicon: the plain mark, ink on light tabs and white on dark ones.
cp "$HERE/favicon.svg" "$ROOT/libs/r_vss/webui/public/favicon.svg"
rsvg-convert -w 32 -h 32 "$HERE/favicon.svg" -o "$ROOT/libs/r_vss/webui/public/favicon-32.png"

echo "Icons written."
