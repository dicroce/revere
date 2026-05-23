// Fit a source rectangle of (srcW × srcH) inside a bounding box of (boxW × boxH)
// while preserving the source's aspect ratio (letterbox / pillarbox as needed).
// Returns the inner rectangle as { x, y, width, height }, with x/y the offset
// from the top-left of the outer box.
//
// Returns the full box on degenerate input (any dimension <= 0) so callers
// don't have to special-case "we don't know the source size yet."
export function fitAspect(srcW, srcH, boxW, boxH) {
  if (srcW <= 0 || srcH <= 0 || boxW <= 0 || boxH <= 0) {
    return { x: 0, y: 0, width: boxW, height: boxH }
  }
  const srcAspect = srcW / srcH
  const boxAspect = boxW / boxH
  let width, height
  if (srcAspect > boxAspect) {
    // Source is wider than the box — fit to box width, letterbox top/bottom.
    width  = boxW
    height = boxW / srcAspect
  } else {
    // Source is taller (or equal) than the box — fit to box height, pillarbox sides.
    height = boxH
    width  = boxH * srcAspect
  }
  return {
    x: (boxW - width)  / 2,
    y: (boxH - height) / 2,
    width,
    height,
  }
}
