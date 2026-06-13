// Small API helper for the Revere web UI.
//
// Read endpoints (/cameras, /jpg, ...) are unauthenticated. The camera-
// management endpoints (the "Record" flow) require a bearer token obtained from
// /login. The token is held in memory and mirrored to localStorage so a page
// reload keeps you signed in until it expires server-side (24h).

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

// --- auth ---

export async function getAuthStatus() {
  const res = await fetch('/auth_status')
  if (!res.ok) throw new Error(`auth_status ${res.status}`)
  return res.json() // { password_set: bool }
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

export function logout() {
  setToken(null)
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
