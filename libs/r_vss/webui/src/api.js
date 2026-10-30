// Small API helper for the Revere web UI.
//
// Every endpoint except /version, /auth_status, /login, /logout and
// /set_password requires a session from /login. The session reaches the server
// two ways:
//   - as a bearer token, which apiFetch() attaches. It is held in memory and
//     mirrored to localStorage so a reload keeps you signed in until it expires
//     server-side (24h);
//   - as the HttpOnly session cookie the server sets on /login (and on
//     /session), which the browser sends by itself with same-origin GETs: the
//     <img> snapshots, the MSE video fetches, the export download, and the
//     plain fetch() calls for timelines. The server honours the cookie only on
//     GET/HEAD, so every write goes through apiFetch().

import { ref } from 'vue'

const TOKEN_KEY = 'revere_token'

export const token = ref(localStorage.getItem(TOKEN_KEY) || null)

export function setToken(t) {
  token.value = t || null
  if (t) localStorage.setItem(TOKEN_KEY, t)
  else localStorage.removeItem(TOKEN_KEY)
}

export function isAuthed() {
  return !!token.value
}

// fetch() wrapper that attaches the bearer token and normalizes errors. On 401
// it clears the (now invalid/expired) token so the UI drops back to the login
// prompt.
export async function apiFetch(path, opts = {}) {
  const headers = { ...(opts.headers || {}) }
  if (token.value) headers['Authorization'] = `Bearer ${token.value}`

  const res = await fetch(path, { ...opts, headers })

  if (res.status === 401) {
    setToken(null)
    throw new Error('Session expired — please log in again.')
  }
  if (!res.ok) {
    let msg = `Request failed (${res.status})`
    try {
      const body = await res.json()
      if (body && body.error) msg = body.error
    } catch { /* body wasn't JSON */ }
    throw new Error(msg)
  }
  return res
}

export async function getCameras() {
  const res = await apiFetch('/cameras')
  return (await res.json()).cameras ?? []
}

// Which build this is (no auth needed):
// { version, build_number, build_time, git_revision, plugins: [{ name, version }] }
export async function getVersion() {
  const res = await fetch('/version')
  if (!res.ok) throw new Error(`version ${res.status}`)
  return res.json()
}

// --- auth ---

export async function getAuthStatus() {
  const res = await fetch('/auth_status')
  if (!res.ok) throw new Error(`auth_status ${res.status}`)
  return res.json() // { password_set: bool }
}

// A token restored from localStorage has no cookie yet (or an expired one):
// have the server set it, so cookie-borne requests work again. Clears the
// token if the server no longer accepts it.
export async function ensureSessionCookie() {
  if (!token.value) return
  const res = await fetch('/session', {
    method: 'POST',
    headers: { Authorization: `Bearer ${token.value}` },
  })
  if (res.status === 401) setToken(null)
}

export async function login(password) {
  const res = await fetch('/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password }),
  })
  if (res.status === 401) throw new Error('Invalid password.')
  if (!res.ok) throw new Error(`Login failed (${res.status}).`)
  const j = await res.json()
  setToken(j.token)
  return j
}

// Ends the session server-side and clears its cookie. The local state is
// dropped first so the UI signs out even if the server can't be reached.
export async function logout() {
  const t = token.value
  setToken(null)
  try {
    await fetch('/logout', {
      method: 'POST',
      headers: t ? { Authorization: `Bearer ${t}` } : {},
    })
  } catch { /* offline: the session just expires */ }
}

// Set or change the system password. On first run (no password set) this needs
// no auth; to change an existing password supply the current one (or be logged
// in, in which case the bearer token authorizes the change).
export async function setSystemPassword(newPassword, currentPassword) {
  const headers = { 'Content-Type': 'application/json' }
  if (token.value) headers['Authorization'] = `Bearer ${token.value}`
  const body = { new_password: newPassword }
  if (currentPassword) body.current_password = currentPassword

  const res = await fetch('/set_password', {
    method: 'POST',
    headers,
    body: JSON.stringify(body),
  })
  if (res.status === 403) {
    throw new Error('The first password has to be set on the Revere machine itself (open the web UI there, or use the desktop app).')
  }
  if (!res.ok) {
    let msg = `Could not set password (${res.status}).`
    try { const b = await res.json(); if (b && b.error) msg = b.error } catch {}
    throw new Error(msg)
  }
  return res.json()
}

// --- record flow ---

const qs = (obj) =>
  Object.entries(obj)
    .filter(([, v]) => v !== undefined && v !== null && v !== '')
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join('&')

export async function getCameraProfiles(cameraId, username, password) {
  const res = await apiFetch('/camera_profiles?' + qs({ camera_id: cameraId, username, password }))
  return (await res.json()).profiles ?? []
}

// Kicks off the (~15s) measurement and resolves with { byte_rate, video_codec,
// jpeg_b64 } once it completes. Calls onProgress(percent) while polling.
export async function measureCamera(cameraId, username, password, profileToken, onProgress) {
  const start = await apiFetch('/measure_camera?' + qs({
    camera_id: cameraId, username, password, profile_token: profileToken,
  }))
  const { id } = await start.json()

  for (;;) {
    await new Promise((r) => setTimeout(r, 1000))
    const pr = await (await apiFetch('/measure_progress?id=' + encodeURIComponent(id))).json()
    if (onProgress) onProgress(pr.percent_complete ?? 0)
    if (pr.failed) throw new Error(pr.error || 'Measurement failed.')
    if ((pr.percent_complete ?? 0) >= 100) break
  }

  return (await apiFetch('/measure_result?id=' + encodeURIComponent(id))).json()
}

export async function configureCamera(payload) {
  const res = await apiFetch('/configure_camera', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })
  return res.json()
}

// Stops recording the camera (it returns to Discovered). When deleteFiles is
// true the server also deletes its storage files, which takes a few seconds
// while recording handles are released.
export async function removeCamera(cameraId, deleteFiles) {
  const res = await apiFetch('/remove_camera', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ camera_id: cameraId, delete_files: !!deleteFiles }),
  })
  return res.json()
}

// Updates a recording camera's settings (motion detection, motion pruning,
// minimum continuous retention hours). The server bounces the camera so the
// change applies live.
export async function updateCameraProperties(cameraId, props) {
  const res = await apiFetch('/update_camera_properties', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ camera_id: cameraId, ...props }),
  })
  return res.json()
}

// Drops a discovered camera. If it's still on the network it will be
// re-discovered on the next poll (mirrors the desktop "Forget" button).
export async function forgetCamera(cameraId) {
  const res = await apiFetch('/forget_camera', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ camera_id: cameraId }),
  })
  return res.json()
}

// Manually adds a (non-ONVIF) camera by its RTSP URL. It lands in the
// Discovered list. Mirrors the desktop "Add RTSP Source Camera" dialog.
export async function addRtspCamera(fields) {
  const res = await apiFetch('/add_rtsp_camera', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(fields),
  })
  return res.json()
}

// --- system plugins (the desktop's Plugins section) ---

// [{ name, display_name, version, enabled, status: { label, tone } | null,
//    lines: [{ text, url?, copy? } | { tiles, title? } | { qr, qr_svg }] }]
export async function getSystemPlugins() {
  const res = await apiFetch('/system_plugins')
  return (await res.json()).plugins ?? []
}

export async function setSystemPluginEnabled(name, enabled) {
  const res = await apiFetch('/system_plugin_enabled', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, enabled }),
  })
  return res.json()
}

// "Keep this computer awake":
// { enabled, on_battery, power: 'ac'|'battery'|'unknown', wanted, held,
//   problem, unavailable, events: [{ what: 'asleep'|'paused', start_ms, end_ms }] newest first }
export async function getKeepAwake() {
  const res = await apiFetch('/keep_awake')
  return res.json()
}

// Either or both of { enabled, on_battery }; returns the same as getKeepAwake.
export async function setKeepAwake(changes) {
  const res = await apiFetch('/keep_awake', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(changes),
  })
  return res.json()
}

// --- storage operations (the desktop's Move storage / Change retention) ---

// { record_file_path, byte_rate, file_size_text, retention_days }, plus
// new_file_size_text for days when given (what Change retention would make).
export async function getStorageInfo(cameraId, days) {
  const res = await apiFetch('/storage_info?' + qs({ camera_id: cameraId, days }))
  return res.json()
}

// Moves the camera's files to newDir, a folder on the machine Revere runs on;
// with startFresh, deletes its recordings and creates empty files there.
export async function moveStorage(cameraId, newDir, startFresh, deleteConfirmed) {
  const res = await apiFetch('/storage_move', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ camera_id: cameraId, new_dir: newDir, start_fresh: startFresh, delete_confirmed: deleteConfirmed }),
  })
  return res.json()
}

// Recreates the recording file for days of video at the current bitrate,
// deleting the camera's recordings.
export async function changeRetention(cameraId, days, deleteConfirmed) {
  const res = await apiFetch('/storage_change_retention', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ camera_id: cameraId, days, delete_confirmed: deleteConfirmed }),
  })
  return res.json()
}

// The running (or last) storage operation:
// { kind, camera_id, running, done, error, progress (0..1), status }.
export async function getStorageOp() {
  const res = await apiFetch('/storage_op')
  return res.json()
}

export async function cancelStorageOp() {
  const res = await apiFetch('/storage_op_cancel', { method: 'POST' })
  return res.json()
}
