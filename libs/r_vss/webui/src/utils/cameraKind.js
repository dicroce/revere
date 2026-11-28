// What a camera properties save sends for the Doorbell switch (/update_camera_properties' "kind").
//
// /cameras gives "kind" (what the camera is taken to be: the person's choice, or what its model says) and
// "kind_setting" (the person's choice, null until they make one). The switch is sent as a choice only once it
// differs from what the model suggests, or when a choice was made before, so saving a camera's other settings never
// pins a guess. Older revere (no "kind" in /cameras): nothing is sent.
export function doorbellChoice(camera, isDoorbell) {
  if (!camera || camera.kind === undefined) return {}
  const kind = isDoorbell ? 'doorbell' : 'camera'
  const chosen = camera.kind_setting !== null && camera.kind_setting !== undefined
  return chosen || kind !== camera.kind ? { kind } : {}
}
