<!--
  CameraPlayer — the video surface for a single camera.

  Three display modes, driven entirely by the `isoTime` prop:
    • live  (isoTime === null) — real video via Media Source Extensions,
      appended from the server's /transcode_fmp4 endpoint and kept near the
      live edge. A still is shown until the first frame can paint.
    • scrub (isoTime is an ISO string, not playing) — a single JPEG at that
      instant. Cheap and instant, ideal while dragging the timeline.
    • play  (the user hit Play on a scrubbed position) — real video via Media
      Source Extensions, appended from the server's /transcode_fmp4 endpoint.

  Both video modes prefer the server's remux-only `passthrough=1` variant of
  /transcode_fmp4 (issue #105): the camera's native H.264/H.265 at native
  resolution and bitrate, no transcode slot. The first window of a camera is a
  probe — if MediaSource.isTypeSupported rejects the codec the server reports
  (e.g. H.265 without a hardware decoder), or the server can't remux the
  stream, the session restarts transparently through the transcoder and the
  choice is remembered for the page session.

  The still <img> and the <video> are stacked in the same box with identical
  object-fit, so the same frame lands on the same pixels. The still stays on top
  until the <video> can paint a frame (`canplay`), then the video is revealed —
  an invisible handoff with no element swap. Any seek drops back to the still.
-->
<template>
  <div class="player" ref="wrap">
    <!-- Layer 1: the cheap scrub/live still. -->
    <img :src="stillUrl" class="layer still" alt="" @error="stillError = true" />

    <!-- Layer 2: the video, pixel-aligned on top, revealed once it has a frame.
         Volume starts at 0 (muted) every load, so nothing blasts audio on entry
         and live can autoplay; the user opts into sound with the slider below. -->
    <video ref="video" class="layer video" :class="{ revealed }" playsinline></video>

    <div v-if="stillError && !revealed" class="msg">No image available</div>

    <!-- Stream status overlay (issue #95): retry / unavailable / codec states.
         Hidden (status === null) while playback is healthy. -->
    <div v-if="status" class="status-overlay" :class="status">
      <template v-if="status === 'reconnecting'">
        <span class="spinner"></span><span>Reconnecting…</span>
      </template>
      <template v-else-if="status === 'unavailable'">
        <span>Stream unavailable</span>
        <button class="retry-btn" @click="retry">Retry</button>
      </template>
      <template v-else-if="status === 'codec'">
        <span>This browser can't play this codec</span>
      </template>
    </div>

    <!-- Play / pause overlay (hidden in live mode). -->
    <button
      v-if="!isLive"
      class="play-overlay"
      :title="playing ? 'Pause' : 'Play'"
      @click="togglePlay"
    >
      <svg v-if="!playing" viewBox="0 0 24 24" width="34" height="34"><path d="M8 5v14l11-7z" fill="currentColor"/></svg>
      <svg v-else          viewBox="0 0 24 24" width="34" height="34"><path d="M6 5h4v14H6zM14 5h4v14h-4z" fill="currentColor"/></svg>
    </button>

    <!-- Volume control (bottom-right), shown once video is on screen. -->
    <div v-if="revealed" class="volume-ctrl">
      <button class="vol-btn" :title="volume === 0 ? 'Unmute' : 'Mute'" @click="toggleMute">
        <svg v-if="volume === 0" viewBox="0 0 24 24" width="22" height="22"><path d="M7 9v6h4l5 5V4l-5 5H7z" fill="currentColor"/><path d="M19 5 5 19" stroke="currentColor" stroke-width="2"/></svg>
        <svg v-else viewBox="0 0 24 24" width="22" height="22"><path d="M7 9v6h4l5 5V4l-5 5H7z" fill="currentColor"/><path d="M17 7a6 6 0 0 1 0 10" fill="none" stroke="currentColor" stroke-width="2"/></svg>
      </button>
      <!-- 0–4x like vision; >1 is applied via a WebAudio GainNode (see applyVolume). -->
      <input class="vol-slider" type="range" min="0" max="4" step="0.05" v-model.number="volume"
             :title="Math.round(volume * 100) + '%'" />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { fitAspect } from '../utils/aspect.js'

const props = defineProps({
  camera:  { type: Object, required: true },
  // null => live (still refresh); ISO string => a seeked recorded position.
  isoTime: { type: String, default: null },
  // Playback speed for seeked playback (issue #101): 0.5 / 1 / 2 / 4. Ignored in
  // live mode (always 1x). Applied via video.playbackRate on the MSE element.
  rate:    { type: Number, default: 1 },
})
// timeupdate: absolute ISO of the currently displayed video frame, so the host
// can follow the playhead on the timeline. Emitted while playing (and after a
// frame step). seek: a frame step landed outside the MSE buffer (or there is no
// video session); the host should seek to this ISO via the normal scrub path.
const emit = defineEmits(['timeupdate', 'seek'])

const isLive = computed(() => props.isoTime === null)

// --- playback tuning -------------------------------------------------------
const BITRATE      = 4_000_000
const FPS_NUM      = 30
const FPS_DEN      = 1
const WINDOW_MS    = 3000   // fMP4 window pulled per request
const AHEAD_S      = 10     // keep this many seconds buffered ahead of playhead
                            // (WINDOW_MS and AHEAD_S are media seconds; both are
                            // scaled by the playback rate so a 4x session still
                            // has ~10 real seconds in hand — see rateScale())
const BEHIND_S     = 30     // evict buffered media older than this behind playhead
const MAX_AUDIO_RATE = 2    // mute above this: browsers mute/garble audio past ~2x
const MAX_W        = 1280   // cap transcode at 720p for v1 (within avc1 4.1, lighter)
const MAX_H        = 720
const MAX_RETRIES      = 3          // per-window fetch retries (503 / timeout / blip)
const FETCH_TIMEOUT_MS = 15000      // abort a stuck window request and retry
const RETRY_BASE_MS    = 400        // backoff step between retries
const SEG_LOOKAHEAD_MS = 6 * 60 * 60 * 1000  // load recording segments this far ahead
// Live: start this far behind the committed write-head (small buffer), poll for
// new footage this often when caught up, and don't fetch a live window until at
// least this much new data has accumulated (avoids churning sub-second windows).
const LIVE_MARGIN_MS   = 3000
const LIVE_POLL_MS     = 1000
const MIN_LIVE_FETCH_MS = 2000
// Fallback MSE codec string if the server doesn't send X-Revere-Codecs (h264
// Main@4.1, video only). Normally the first window's header supplies the real one.
const FALLBACK_CODECS = 'avc1.4d4029'
// Passthrough (issue #105). Windows there are GOP-aligned (see r_ws.cpp
// _get_passthrough_fmp4): a reply holds whole GOPs only, starting at the key
// frame at/before start_time (X-Revere-First-Ts) and stopping before the last
// key frame in range (X-Revere-Next-Start, where the next window must begin);
// an empty body means the window held no whole GOP yet. A seeked window must
// therefore span at least one GOP: we don't know the GOP length up front, so
// start at PT_WINDOW_MIN_MS and double on an empty reply up to PT_WINDOW_MAX_MS.
// Live passthrough asks for everything up to the head and starts a little
// further back than transcode so the first reply is likely to hold a whole GOP.
const PT_WINDOW_MIN_MS   = 4000
const PT_WINDOW_MAX_MS   = 60000
const PT_LIVE_MARGIN_MS  = 5000
// Stream mode per camera, remembered for the page session so later seeks don't
// re-probe: 'passthrough' | 'transcode'. Unset = probe on the next start.
const streamModeByCamera = new Map()
// Live auto-recovery (issue #95): if nothing new has been appended for this long
// (or the <video> errors), tear the live session down and re-enter live. Restarts
// back off exponentially up to LIVE_RESTART_MAX_MS, and after LIVE_MAX_RESTARTS
// consecutive failures we stop and show "Stream unavailable" + Retry.
const LIVE_STALL_MS        = 10000
const LIVE_WATCHDOG_MS     = 2000
const LIVE_RESTART_BASE_MS = 1000
const LIVE_RESTART_MAX_MS  = 30000
const LIVE_MAX_RESTARTS    = 8

// --- refs / reactive state -------------------------------------------------
const wrap       = ref(null)
const video      = ref(null)
const stillUrl   = ref('')
const stillError = ref(false)
const revealed   = ref(false)   // the entire "swap": still shown until this flips
const playing    = ref(false)
const reqW       = ref(1280)
const reqH       = ref(720)
const volume     = ref(0)       // starts silent every load; user opts into sound
let lastVolume   = 0.6          // level restored when un-muting via the speaker icon

// Gain above unity (issue #95): the native element volume is clamped to [0,1], so
// for > 1 we route the element through a WebAudio GainNode. The graph is built
// lazily on the first > 1 request — which comes from the slider, i.e. a user
// gesture, so the AudioContext is allowed to start. createMediaElementSource may
// only be called once per element; the <video> persists across play sessions, so
// one graph serves all of them. Once built, the element's audio flows through the
// context exclusively (we keep gain=1 and use the native volume for <= 1).
let audioCtx = null, gainNode = null
function ensureGainGraph() {
  if (gainNode) return true
  const v = video.value
  const AC = window.AudioContext || window.webkitAudioContext
  if (!v || !AC) return false
  try {
    audioCtx = new AC()
    const src = audioCtx.createMediaElementSource(v)
    gainNode = audioCtx.createGain()
    src.connect(gainNode)
    gainNode.connect(audioCtx.destination)
    return true
  } catch (e) {
    console.warn('CameraPlayer: WebAudio gain unavailable, volume capped at 1', e)
    audioCtx = null; gainNode = null
    return false
  }
}

function applyVolume() {
  const v = video.value
  if (!v) return
  const vol = volume.value
  // Muted at 0 → true silence + autoplay allowed. Also muted above MAX_AUDIO_RATE
  // (issue #101): fast playback is a visual tool and sped-up audio is useless.
  v.muted = vol === 0 || effectiveRate() > MAX_AUDIO_RATE
  if (vol > 1 && ensureGainGraph()) {
    v.volume = 1
    gainNode.gain.value = vol
  } else {
    v.volume = Math.min(vol, 1)
    if (gainNode) gainNode.gain.value = 1
  }
  if (audioCtx && vol > 0 && audioCtx.state === 'suspended')
    audioCtx.resume().catch(() => {})
}
function toggleMute() {
  if (volume.value > 0) { lastVolume = volume.value; volume.value = 0 }
  else volume.value = lastVolume || 0.6
}
watch(volume, applyVolume)

// --- playback speed (issue #101) -------------------------------------------
// Live always runs at 1x; seeked playback follows the `rate` prop.
function effectiveRate() {
  if (liveMode) return 1
  const r = Number(props.rate)
  return r > 0 && Number.isFinite(r) ? r : 1
}
// Fetch-ahead scale: at 4x the playhead eats media 4x faster, so keep 4x as much
// media buffered and pull 4x longer windows. Never shrink below 1 (0.5x gains
// nothing from smaller windows).
function rateScale() { return Math.max(1, effectiveRate()) }

function applyRate() {
  const v = video.value
  if (!v) return
  const r = effectiveRate()
  if (v.playbackRate !== r) { try { v.playbackRate = r } catch (_) {} }
  applyVolume()                            // mute/un-mute around MAX_AUDIO_RATE
}
watch(() => props.rate, () => { applyRate(); pump() })   // pump: new threshold may want a fetch

// --- MSE driver state (plain locals; not reactive) -------------------------
let mediaSource  = null
let sourceBuffer = null
let appendQueue  = []
let feeding      = false        // still pulling windows from the server
let fetching     = false        // a window request is in flight (single-flight)
let firstSegment = true         // the first appended window carries the init segment
let negotiatedCodecs = null     // exact MSE codec string from the first window's header
let ptsBaseMs    = 0            // wall-clock of the play-start seek point
let windowStartMs = 0          // wall-clock start of the next window to fetch
let vidW = 0, vidH = 0
let abort        = null
// Passthrough (issue #105) session state.
let passthrough     = false     // this session uses the remux-only passthrough protocol
let ptProbing       = false     // passthrough not yet confirmed for this camera (first window)
let ptWindowMs      = PT_WINDOW_MIN_MS   // seeked passthrough window length (grows to ≥ 1 GOP)
let ptGopMs         = 0         // duration of the last passthrough window (≥ 1 GOP)
let ptFrameMs       = 0         // camera frame period from X-Revere-Frame-Duration-Ms
let pendingSeekSec  = -1        // first passthrough window began a GOP early; jump here once buffered
let fallbackPending = false     // passthrough rejected mid-start; a transcode restart is scheduled

// Gap-skipping: recordings are full of holes (motion-only, offline). We play only
// within recorded segments and compress gaps out of the MSE timeline, so
// presentation time is contiguous even though wall-clock isn't.
let segments      = []          // [{startMs,endMs}] recorded segments, sorted
let presentationMs = 0          // next window's position on the (gap-free) MSE timeline
let timeline      = []          // [{presMs,wallMs,durMs}] maps presentation → wall-clock
let liveMode      = false       // chasing the live write-head (vs seeked playback)
let waitingForData = false      // live: caught up to the head, polling for more

// --- stream status + live auto-recovery (issue #95) ------------------------
// status: null (healthy) | 'reconnecting' | 'unavailable' | 'codec'
const status = ref(null)
let session        = 0          // startPlayback generation; stale awaits bail out
let lastAppendMs   = 0          // wall-clock of the last window queued for append
let liveRestarts   = 0          // consecutive live restarts without fresh data
let liveRestartAt  = 0          // wall-clock of the last live restart
let liveRestartTimer = null
let liveWatchdog   = null
let segmentsFailing = false     // live: /contents poll failing (server unreachable)

// Note fresh media. Clears a "Reconnecting…" banner and, in live, resets the
// restart backoff once we've received footage newer than the last restart.
function noteAppend(winEndMs) {
  lastAppendMs = Date.now()
  if (status.value === 'reconnecting') status.value = null
  if (liveMode && winEndMs > liveRestartAt) liveRestarts = 0
}

// The feed died (retries exhausted / hard HTTP error / <video> error). Live
// streams recover on their own with backoff; seeked playback asks the user.
function onFeedFailed() {
  feeding = false
  if (liveMode && props.isoTime === null) scheduleLiveRestart()
  else status.value = 'unavailable'
}

function scheduleLiveRestart() {
  if (liveRestartTimer) return
  stopVideo()                              // also stops the watchdog
  playing.value = false                    // lets the JPEG fallback refresh meanwhile
  if (liveRestarts >= LIVE_MAX_RESTARTS) { status.value = 'unavailable'; return }
  const wait = Math.min(LIVE_RESTART_MAX_MS, LIVE_RESTART_BASE_MS * 2 ** liveRestarts)
  liveRestarts++
  status.value = 'reconnecting'
  liveRestartTimer = setTimeout(() => {
    liveRestartTimer = null
    if (props.isoTime !== null) return     // user left live meanwhile
    liveRestartAt = Date.now()
    enterLive()
  }, wait)
}
function cancelLiveRestart() {
  if (liveRestartTimer) { clearTimeout(liveRestartTimer); liveRestartTimer = null }
}

// Live stall check. "No new media for LIVE_STALL_MS" alone isn't a fault — a
// motion-only / idle camera legitimately writes nothing — so we refresh the
// segment list and only restart if the server holds footage we failed to pull.
let stallChecking = false
async function checkLiveStall(minIdleMs) {
  if (!liveMode || !mediaSource || stallChecking) return
  if (Date.now() - lastAppendMs < minIdleMs) return
  stallChecking = true
  const mySession = session
  try { await loadSegments(windowStartMs) } finally { stallChecking = false }
  if (mySession !== session || !liveMode) return
  // Passthrough can't emit the GOP in progress, so a long-GOP camera is
  // legitimately up to a GOP or two behind the head without being stalled.
  const slack = passthrough ? 2 * ptGopMs : 0
  if (Date.now() - lastAppendMs < minIdleMs + slack) return
  if (liveHeadMs() - windowStartMs > LIVE_STALL_MS / 2 + slack) {
    console.warn('CameraPlayer: live stream stalled, restarting')
    onFeedFailed()
  }
}
function startLiveWatchdog() {
  stopLiveWatchdog()
  lastAppendMs = Date.now()
  liveWatchdog = setInterval(() => checkLiveStall(LIVE_STALL_MS), LIVE_WATCHDOG_MS)
}
function stopLiveWatchdog() {
  if (liveWatchdog) { clearInterval(liveWatchdog); liveWatchdog = null }
}

// <video> element errors: a MediaError is fatal to the current MSE session.
function onVideoError() {
  if (video.value && video.value.error)
    console.error('CameraPlayer: video error', video.value.error.code, video.value.error.message)
  if (liveMode) onFeedFailed()
  else { stopVideo(); playing.value = false; status.value = 'unavailable' }
}
// 'stalled' is normal in small doses; treat it as persistent once we've also
// gone half the stall window without new media (same server-side check applies).
function onVideoStalled() { checkLiveStall(LIVE_STALL_MS / 2) }

// Retry button: reset the backoff and start over from the current mode.
function retry() {
  status.value = null
  liveRestarts = 0
  cancelLiveRestart()
  if (props.isoTime === null) { liveRestartAt = Date.now(); enterLive() }
  // Resume an established feed in place; if nothing was ever appended the
  // SourceBuffer may carry the fallback codec string, so rebuild from scratch.
  else if (mediaSource && sourceBuffer && !firstSegment) { feeding = true; fetchNextWindow() }
  else startPlayback()
}

// --- still (jpg) -----------------------------------------------------------
function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

function recomputeStillSize() {
  const el = wrap.value
  if (!el || el.clientWidth === 0 || el.clientHeight === 0) return
  const dpr  = window.devicePixelRatio || 1
  const boxW = el.clientWidth  * dpr
  const boxH = el.clientHeight * dpr
  const srcW = Number(props.camera.width)  || 0
  const srcH = Number(props.camera.height) || 0
  let w, h
  if (srcW > 0 && srcH > 0) {
    const fit = fitAspect(srcW, srcH, boxW, boxH)
    w = Math.round(Math.min(srcW, fit.width))
    h = Math.round(Math.min(srcH, fit.height))
  } else {
    w = clamp(Math.round(boxW), 640, 2560)
    h = clamp(Math.round(boxH), 360, 1440)
  }
  reqW.value = Math.max(320, w)
  reqH.value = Math.max(180, h)
}

function loadStill(iso) {
  recomputeStillSize()
  const url = `/jpg?camera_id=${props.camera.id}`
            + `&start_time=${encodeURIComponent(iso)}&width=${reqW.value}&height=${reqH.value}`
  const img = new Image()
  img.onload  = () => { stillUrl.value = url; stillError.value = false }
  img.onerror = () => { stillError.value = true }
  img.src = url
}
function liveIso() { return new Date(Date.now() - 5000).toISOString() }

let liveTimer = null
function startLiveRefresh() {
  stopLiveRefresh()
  liveTimer = setInterval(() => { if (!playing.value) loadStill(liveIso()) }, 3000)
}
function stopLiveRefresh() { if (liveTimer) { clearInterval(liveTimer); liveTimer = null } }

// --- video size (fixed for a play session) ---------------------------------
function computeVideoSize() {
  const srcW = Number(props.camera.width)  || 1280
  const srcH = Number(props.camera.height) || 720
  const fit  = fitAspect(srcW, srcH, MAX_W, MAX_H)
  let w = Math.min(srcW, fit.width)
  let h = Math.min(srcH, fit.height)
  // Encoders want even dimensions.
  w = Math.max(160, Math.round(w / 2) * 2)
  h = Math.max(120, Math.round(h / 2) * 2)
  return { w, h }
}

// --- MSE playback ----------------------------------------------------------
const once = (t, ev) => new Promise(r => t.addEventListener(ev, r, { once: true }))

async function startPlayback(live = false) {
  if (!live && props.isoTime === null) return
  stopVideo()                              // clean slate
  const mySession = ++session              // a newer start/stop invalidates our awaits

  if (typeof MediaSource === 'undefined') { status.value = 'codec'; return }

  const sz = computeVideoSize()
  vidW = sz.w; vidH = sz.h
  firstSegment    = true
  negotiatedCodecs = null
  presentationMs  = 0
  segments        = []
  timeline        = []
  liveMode        = !!live
  waitingForData  = false
  abort           = new AbortController()

  // Passthrough unless this camera is already known to need the transcoder.
  const mode = streamModeByCamera.get(props.camera.id)
  passthrough     = mode !== 'transcode'
  ptProbing       = passthrough && mode !== 'passthrough'
  ptWindowMs      = PT_WINDOW_MIN_MS
  ptGopMs         = 0
  pendingSeekSec  = -1
  fallbackPending = false

  // Seek to the requested instant, or (live) to just behind the committed head.
  await loadSegments(live ? (Date.now() - 60000) : Date.parse(props.isoTime))
  if (mySession !== session) return        // torn down while we awaited
  if (live) {
    const head = liveHeadMs()
    ptsBaseMs = (head > 0 ? head : Date.now()) - (passthrough ? PT_LIVE_MARGIN_MS : LIVE_MARGIN_MS)
  } else {
    ptsBaseMs = Date.parse(props.isoTime)
  }
  windowStartMs = ptsBaseMs

  mediaSource = new MediaSource()
  video.value.src = URL.createObjectURL(mediaSource)
  await once(mediaSource, 'sourceopen')
  if (!mediaSource || mySession !== session) return   // torn down while we awaited

  playing.value = true
  feeding = true
  if (live) startLiveWatchdog()

  // The reveal — hide the still the moment the video can paint.
  video.value.addEventListener('canplay', onCanPlay, { once: true })
  video.value.addEventListener('timeupdate', onVideoTime)
  video.value.addEventListener('error', onVideoError)
  video.value.addEventListener('stalled', onVideoStalled)
  applyRate()                              // load() resets playbackRate; also applies volume/mute

  // The SourceBuffer is created by fetchNextWindow (ensureSourceBuffer) when the
  // first window with media arrives: the server reports the exact codec string
  // (including whether audio is present) in a response header, and
  // addSourceBuffer must be told the right codecs up front. In live the first
  // reply may be empty (a GOP still being committed), so creation can't be tied
  // to this first fetch. In passthrough the first window is also the probe: a
  // rejected codec, or a server that can't remux this stream, schedules a
  // transparent restart through the transcoder (fallbackToTranscode).
  await fetchNextWindow()
  if (!mediaSource || mySession !== session || fallbackPending) return
  video.value.play().catch(() => {})
}

// Create the SourceBuffer once the first window has told us the exact codec
// string. Returns false (having dealt with it) if the browser can't play it: in
// passthrough that means "use the transcoder for this camera" and the session
// restarts in transcode mode; in transcode mode it is the terminal 'codec' state.
function ensureSourceBuffer() {
  if (sourceBuffer) return true
  if (!mediaSource || mediaSource.readyState !== 'open') return false
  const mime = `video/mp4; codecs="${negotiatedCodecs || FALLBACK_CODECS}"`
  let ok = MediaSource.isTypeSupported(mime)
  if (!ok) console.error('CameraPlayer: codec not supported by this browser', mime)
  else {
    try { sourceBuffer = mediaSource.addSourceBuffer(mime) }
    catch (e) { console.error('CameraPlayer: addSourceBuffer failed', mime, e); ok = false }
  }
  if (!ok) {
    if (passthrough) { fallbackToTranscode(mime); return false }
    stopVideo()
    playing.value = false
    status.value = 'codec'
    return false
  }
  sourceBuffer.addEventListener('updateend', pump)
  if (ptProbing) { ptProbing = false; streamModeByCamera.set(props.camera.id, 'passthrough') }
  return true
}

// Passthrough can't serve this camera (codec rejected by MSE, or the server
// can't remux the stream). Remember that for the page session and restart the
// current mode through the transcoder. Deferred a tick so the in-flight
// fetchNextWindow unwinds first (its finally clears `fetching`); a seek or
// teardown in between bumps `session` and cancels the restart.
function fallbackToTranscode(reason) {
  if (fallbackPending) return
  fallbackPending = true
  console.warn('CameraPlayer: passthrough unavailable, using transcode:', reason)
  streamModeByCamera.set(props.camera.id, 'transcode')
  const live = liveMode, mySession = session
  setTimeout(() => {
    if (session !== mySession) return
    if (live ? props.isoTime !== null : props.isoTime === null) return
    startPlayback(live)
  }, 0)
}

function onCanPlay() {
  revealed.value = true
  if (status.value === 'reconnecting') status.value = null
  stopLiveRefresh()   // live video took over from the JPEG fallback
}

function onVideoTime() {
  if (video.value)
    emit('timeupdate', new Date(presentationToWallMs(video.value.currentTime)).toISOString())
  pump()
}

// Map a presentation-time position (video.currentTime, seconds) back to the real
// wall-clock time it represents, using the per-window mapping. Needed because we
// compress gaps out of the presentation timeline.
function presentationToWallMs(presSec) {
  const presMs = presSec * 1000
  for (let i = timeline.length - 1; i >= 0; i--) {
    const t = timeline[i]
    if (presMs >= t.presMs)
      return t.wallMs + Math.min(presMs - t.presMs, t.durMs)
  }
  return ptsBaseMs + presMs
}

const delay = (ms) => new Promise(r => setTimeout(r, ms))

// Load the recorded-segment list for [from, from+lookahead] so playback can skip
// gaps. Replaces the current list (only future windows consult it).
async function loadSegments(fromMs) {
  const s = new Date(fromMs - 2000).toISOString()
  const e = new Date(fromMs + SEG_LOOKAHEAD_MS).toISOString()
  try {
    const res = await fetch(`/contents?camera_id=${props.camera.id}`
      + `&start_time=${encodeURIComponent(s)}&end_time=${encodeURIComponent(e)}`,
      { signal: abort.signal })
    if (!res.ok) return
    const d = await res.json()
    segments = (d.segments || [])
      .map(x => ({ startMs: Date.parse(x.start_time), endMs: Date.parse(x.end_time) }))
      .filter(x => x.endMs > fromMs)
      .sort((a, b) => a.startMs - b.startMs)
    if (segmentsFailing) {                 // server reachable again
      segmentsFailing = false
      if (status.value === 'reconnecting') status.value = null
    }
  } catch (_) {
    /* teardown or transient — leave list as-is */
    // Live: the segment poll is our only heartbeat while caught up, so a failing
    // /contents (server unreachable) is surfaced as "Reconnecting…" (issue #95).
    if (liveMode && abort && !abort.signal.aborted) {
      segmentsFailing = true
      status.value = 'reconnecting'
    }
  }
}

// First segment whose data extends past ms (the one containing ms, or the next
// one after a gap). null if none known.
function segFor(ms) {
  for (const s of segments) if (ms < s.endMs) return s
  return null
}

// Wall-clock of the latest committed footage (max segment end), or 0 if none.
function liveHeadMs() {
  let h = 0
  for (const s of segments) if (s.endMs > h) h = s.endMs
  return h
}

// Live: caught up to the head. Wait, refresh the segment list, and try again.
function scheduleLivePoll() {
  waitingForData = true
  setTimeout(async () => {
    waitingForData = false
    if (!feeding || !abort || abort.signal.aborted) return
    await loadSegments(windowStartMs)
    fetchNextWindow()
  }, LIVE_POLL_MS)
}

// Fetch one window with a timeout and retries (503 = server at capacity, plus
// transient timeouts/blips). Returns the Response, or null on teardown / exhausted
// retries (in which case feeding is cleared).
async function fetchWindow(winStart, winEnd, throughEnd = false) {
  const s = new Date(winStart).toISOString()
  const e = new Date(winEnd).toISOString()
  let url = `/transcode_fmp4?camera_id=${props.camera.id}`
    + `&start_time=${encodeURIComponent(s)}&end_time=${encodeURIComponent(e)}`
  if (passthrough)
    url += '&passthrough=1' + (throughEnd ? '&through_end=1' : '')   // audio on (default)
  else
    url += `&width=${vidW}&height=${vidH}&bitrate=${BITRATE}`
      + `&framerate_num=${FPS_NUM}&framerate_den=${FPS_DEN}&codec=h264`

  for (let attempt = 0; attempt <= MAX_RETRIES; attempt++) {
    if (!feeding || !abort || abort.signal.aborted) return null
    const ctl = new AbortController()
    const onAbort = () => ctl.abort()
    abort.signal.addEventListener('abort', onAbort)
    const to = setTimeout(() => ctl.abort(), FETCH_TIMEOUT_MS)
    try {
      const res = await fetch(url, { signal: ctl.signal })
      clearTimeout(to); abort.signal.removeEventListener('abort', onAbort)
      if (res.status === 503) {
        status.value = 'reconnecting'
        await delay(RETRY_BASE_MS * (attempt + 1)); continue
      }
      return res
    } catch (_) {
      clearTimeout(to); abort.signal.removeEventListener('abort', onAbort)
      if (abort && abort.signal.aborted) return null   // real teardown, don't retry
      status.value = 'reconnecting'
      await delay(RETRY_BASE_MS * (attempt + 1))        // timeout / network — retry
    }
  }
  if (!feeding || !abort || abort.signal.aborted) return null
  console.warn('CameraPlayer: window fetch retries exhausted', s, e)
  onFeedFailed()                                        // retries exhausted
  return null
}

async function fetchNextWindow() {
  if (!feeding || fetching || waitingForData) return
  fetching = true
  let refetch = false                      // passthrough: window was too short, widened
  try {
    // Find the segment to read from, snapping across any gap. Refresh the list
    // in case more has been recorded since we last looked.
    let seg = segFor(windowStartMs)
    if (!seg) {
      await loadSegments(windowStartMs)
      seg = segFor(windowStartMs)
      if (!seg) {
        // No footage here. In live that means we've caught the write-head — poll
        // for more; in seeked playback it's the genuine end of the recording.
        if (liveMode) { scheduleLivePoll(); return }
        feeding = false
        return
      }
    }

    const winStart = Math.max(windowStartMs, seg.startMs)  // snap into the segment
    // Passthrough windows must hold at least one whole GOP (ptWindowMs grows on
    // an empty reply); live passthrough simply asks for everything up to the head.
    const wantMs = passthrough
      ? (liveMode ? Infinity : Math.max(WINDOW_MS * rateScale(), ptWindowMs))
      : WINDOW_MS * rateScale()
    const winEnd   = Math.min(winStart + wantMs, seg.endMs)
    // Seeked passthrough reaching the end of a recorded segment: ask for the
    // final (partial) GOP too, otherwise it is never emitted.
    const throughEnd = passthrough && !liveMode && winEnd === seg.endMs

    // Live: if this window is only short because it's clamped at the write-head
    // (not a real segment boundary), wait for more data rather than churning a
    // tiny window.
    if (liveMode && winEnd === seg.endMs && seg.endMs === liveHeadMs()
        && (winEnd - winStart) < MIN_LIVE_FETCH_MS) {
      scheduleLivePoll()
      return
    }
    if (winEnd <= winStart) { windowStartMs = seg.endMs; return }

    const res = await fetchWindow(winStart, winEnd, throughEnd)
    if (!res) return                                   // teardown / retries exhausted
    if (!res.ok) {
      // Probing passthrough: the server can't remux this stream (unsupported
      // codec, no parameter sets, ...) — use the transcoder for this camera.
      if (passthrough && ptProbing) { fallbackToTranscode('HTTP ' + res.status); return }
      console.warn('CameraPlayer: window fetch failed', res.status)
      onFeedFailed()
      return
    }

    const codecs = res.headers.get('X-Revere-Codecs')
    if (codecs) negotiatedCodecs = codecs
    const frameDur = Number(res.headers.get('X-Revere-Frame-Duration-Ms'))
    if (frameDur > 0) ptFrameMs = frameDur
    const buf = await res.arrayBuffer()

    if (buf && buf.byteLength) {
      if (!ensureSourceBuffer()) return                // codec rejected: fallback / 'codec'
      // 0-based self-contained fMP4. Keep the init on the first window, strip it
      // (append from first moof) on the rest. offsetSec places it on the gap-free
      // presentation timeline; timeline records the wall-clock mapping.
      let bytes = new Uint8Array(buf)
      if (firstSegment) firstSegment = false
      else              bytes = stripToFirstMoof(bytes)
      // Where this window really sits on the wall clock. A transcode window covers
      // exactly [winStart, winEnd). A passthrough window is GOP-aligned: it starts
      // at the key frame at/before winStart (X-Revere-First-Ts — only the first
      // window of a session can land early) and ends before the key frame at
      // X-Revere-Next-Start, which is where the next window must begin.
      let wallMs = winStart, nextMs = winEnd, presMs = presentationMs
      if (passthrough) {
        const firstTs   = Number(res.headers.get('X-Revere-First-Ts'))
        const nextStart = Number(res.headers.get('X-Revere-Next-Start'))
        if (firstTs > 0) wallMs = firstTs
        if (nextStart > wallMs) nextMs = nextStart
        ptGopMs = nextMs - wallMs
        if (timeline.length === 0) {
          // First window began a GOP before the requested instant: land on the
          // instant once it's buffered (pump) rather than showing the GOP head.
          if (ptsBaseMs > wallMs && ptsBaseMs < nextMs)
            pendingSeekSec = (ptsBaseMs - wallMs) / 1000
        } else if (wallMs < winStart) {
          // A later window landed before where we asked. Either we asked mid-GOP
          // (after a through_end tail at the live head) and its early frames are
          // ones we already appended, or the storage query backed up across a
          // gap into the previous segment. Either way, align winStart with the
          // presentation position it already has: the early frames re-cover the
          // same media (MSE replaces overlapping frames) instead of being played
          // twice, and the gap stays compressed out.
          presMs = Math.max(0, presentationMs - (winStart - wallMs))
        }
      }
      const durMs = nextMs - wallMs
      appendQueue.push({ bytes, offsetSec: presMs / 1000 })
      timeline.push({ presMs, wallMs, durMs })
      presentationMs = presMs + durMs
      windowStartMs = nextMs
      noteAppend(nextMs)
    } else if (passthrough && liveMode) {
      // No whole GOP committed past windowStartMs yet; poll for more.
      scheduleLivePoll()
      return
    } else if (passthrough && !throughEnd && ptWindowMs < PT_WINDOW_MAX_MS) {
      // Window shorter than a GOP: widen and ask again right away.
      ptWindowMs = Math.min(PT_WINDOW_MAX_MS, ptWindowMs * 2)
      refetch = true
    } else {
      // Segment list said there's data here but there isn't — treat as a gap and
      // jump to the segment end so the next pass snaps to the following segment.
      windowStartMs = seg.endMs
    }
  } catch (_) {
    if (!(abort && abort.signal.aborted)) feeding = false
  } finally {
    fetching = false
  }
  // Until the SourceBuffer exists pump() can't drive the fetch loop (it keys off
  // bufferedAhead), so keep pulling from here: a widened passthrough window, or
  // a first window that turned out to be a gap. Every branch above either
  // advances windowStartMs, parks us (waitingForData / fallbackPending) or
  // clears feeding, so this can't spin.
  if (refetch || (!sourceBuffer && feeding && !waitingForData && !fallbackPending)) fetchNextWindow()
  else pump()
}

// Return the slice of a fragmented-mp4 buffer starting at the first `moof` box,
// dropping the leading init segment (ftyp + moov). MP4 boxes are
// [4-byte big-endian size][4-byte type][payload]. Falls back to the whole buffer
// if no moof is found.
function stripToFirstMoof(u8) {
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength)
  let off = 0
  while (off + 8 <= u8.byteLength) {
    const size = dv.getUint32(off)
    if (u8[off+4]===0x6d && u8[off+5]===0x6f && u8[off+6]===0x6f && u8[off+7]===0x66) // 'moof'
      return u8.subarray(off)
    if (size < 8) break                    // malformed / 64-bit size — bail safely
    off += size
  }
  return u8
}

function bufferedAhead() {
  const b = sourceBuffer && sourceBuffer.buffered
  if (!b || !b.length || !video.value) return 0
  return b.end(b.length - 1) - video.value.currentTime
}

function pump() {
  if (!sourceBuffer || sourceBuffer.updating) return
  const t = video.value ? video.value.currentTime : 0

  // Sliding-window eviction so a long session can't exhaust the buffer quota.
  if (t > BEHIND_S + 10 && sourceBuffer.buffered.length
      && sourceBuffer.buffered.start(0) < t - BEHIND_S) {
    try { sourceBuffer.remove(0, t - BEHIND_S) } catch (_) {}
    // Drop wall-clock mapping entries fully behind the evicted point.
    const cutoffMs = (t - BEHIND_S) * 1000
    while (timeline.length > 1 && (timeline[0].presMs + timeline[0].durMs) < cutoffMs)
      timeline.shift()
    return                                  // updateend re-enters pump()
  }

  if (appendQueue.length) {
    const item = appendQueue.shift()
    // Position this 0-based window on the global timeline. timestampOffset can
    // only be set while not updating, which we guaranteed above.
    if (sourceBuffer.timestampOffset !== item.offsetSec)
      sourceBuffer.timestampOffset = item.offsetSec
    try { sourceBuffer.appendBuffer(item.bytes) } catch (_) {}
    return
  }

  // Passthrough: the first window started at the key frame before the requested
  // instant; now that it's buffered, move the playhead onto the instant itself.
  if (pendingSeekSec > 0 && video.value && bufferedContains(pendingSeekSec)) {
    try { video.value.currentTime = pendingSeekSec } catch (_) {}
    pendingSeekSec = -1
  }

  // Fetch-ahead: keep AHEAD_S (scaled by rate, see rateScale) of media buffered.
  // Passthrough windows are GOP-aligned and never shorter than a GOP, so at 4x
  // (~40 s wanted) the loop simply pulls a few more windows per pass; the
  // threshold needs no mode-specific tuning.
  if (feeding && bufferedAhead() < AHEAD_S * rateScale()) fetchNextWindow()
}

// --- frame step (issue #101) -----------------------------------------------
// Step one frame back (dir = -1) or forward (+1) in seeked playback. One frame
// is frameMs() of presentation time. Playing video is paused first (stepping
// implies pausing). If the target lies inside the MSE buffer we just move
// currentTime — the paused <video> paints the new frame instantly and
// timeupdate keeps the timeline in sync. Otherwise (no video session, or stepped
// off the buffered edge) we fall back to the host's normal seek path, which
// shows a /jpg still at the new time.
//
// Frame period: the transcoder runs at a fixed FPS_NUM/FPS_DEN. In passthrough
// the frames are the camera's own, so we use the server's measured period
// (X-Revere-Frame-Duration-Ms: mean real timestamp delta of the frames in the
// last window — cameras' SPS timing is often wrong, so it is not used) and fall
// back to 1/30 s until a window has arrived.
function frameMs() {
  const pt = mediaSource ? passthrough : streamModeByCamera.get(props.camera.id) !== 'transcode'
  if (pt) return ptFrameMs > 0 ? ptFrameMs : 1000 / 30
  return 1000 * FPS_DEN / FPS_NUM
}

function bufferedContains(presSec) {
  const b = sourceBuffer && sourceBuffer.buffered
  if (!b) return false
  for (let i = 0; i < b.length; i++)
    if (presSec >= b.start(i) && presSec <= b.end(i)) return true
  return false
}

function stepFrame(dir) {
  if (props.isoTime === null || liveMode) return      // live: no stepping
  const d = dir < 0 ? -1 : 1
  const v = video.value

  if (playing.value && v) { v.pause(); playing.value = false }

  if (mediaSource && sourceBuffer && v && revealed.value) {
    const target = v.currentTime + d * frameMs() / 1000
    if (target >= 0 && bufferedContains(target)) {
      v.currentTime = target
      emit('timeupdate', new Date(presentationToWallMs(target)).toISOString())
      return
    }
    // Outside the buffer: seek by wall-clock from the frame on screen.
    emit('seek', new Date(presentationToWallMs(v.currentTime) + d * frameMs()).toISOString())
    return
  }

  // Scrub still (no video session): nudge the seeked instant by one frame.
  const base = Date.parse(props.isoTime)
  if (Number.isFinite(base)) emit('seek', new Date(base + d * frameMs()).toISOString())
}

function stopVideo() {
  feeding  = false
  fetching = false
  revealed.value = false
  appendQueue = []
  segments = []
  timeline = []
  presentationMs = 0
  liveMode = false
  waitingForData = false
  pendingSeekSec = -1
  if (abort) { try { abort.abort() } catch (_) {} abort = null }
  session++                                // invalidate any in-flight startPlayback
  stopLiveWatchdog()
  cancelLiveRestart()
  segmentsFailing = false

  const v = video.value
  if (v) {
    v.pause()
    v.removeEventListener('timeupdate', onVideoTime)
    v.removeEventListener('canplay', onCanPlay)
    v.removeEventListener('error', onVideoError)
    v.removeEventListener('stalled', onVideoStalled)
    v.removeAttribute('src')
    try { v.load() } catch (_) {}
  }
  if (sourceBuffer) {
    try { sourceBuffer.removeEventListener('updateend', pump) } catch (_) {}
  }
  if (mediaSource && mediaSource.readyState === 'open') {
    try { mediaSource.endOfStream() } catch (_) {}
  }
  mediaSource = null
  sourceBuffer = null
}

function togglePlay() {
  if (playing.value) {
    // Pause: keep the decoded frame on screen.
    video.value && video.value.pause()
    playing.value = false
  } else if (mediaSource) {
    // Resume an already-built stream.
    video.value && video.value.play().catch(() => {})
    playing.value = true
  } else {
    startPlayback()
  }
}

// Live mode: chase the write-head as video. Show a JPEG immediately and keep it
// refreshing as a fallback until the live video reveals (onCanPlay stops it), so
// a camera with no recent footage still shows something.
function enterLive() {
  loadStill(liveIso())
  startLiveRefresh()
  startPlayback(true)
}

// --- react to seek / live --------------------------------------------------
watch(() => props.isoTime, (iso) => {
  stopVideo()
  playing.value = false
  status.value = null                      // a seek/live switch starts fresh
  liveRestarts = 0
  if (iso === null) enterLive()
  else { stopLiveRefresh(); loadStill(iso) }
})

// --- lifecycle -------------------------------------------------------------
let resizeObs = null
let resizeDebounce = null
onMounted(() => {
  recomputeStillSize()
  if (props.isoTime === null) enterLive()
  else loadStill(props.isoTime)

  if (wrap.value && typeof ResizeObserver !== 'undefined') {
    resizeObs = new ResizeObserver(() => {
      clearTimeout(resizeDebounce)
      resizeDebounce = setTimeout(() => {
        // Only the still tracks element size; the video keeps its session size.
        if (!playing.value && !revealed.value) {
          recomputeStillSize()
          loadStill(props.isoTime === null ? liveIso() : props.isoTime)
        }
      }, 200)
    })
    resizeObs.observe(wrap.value)
  }
})

onUnmounted(() => {
  stopLiveRefresh()
  stopVideo()
  clearTimeout(resizeDebounce)
  if (resizeObs) resizeObs.disconnect()
})

// --- host API (keyboard shortcuts in CameraView) ---------------------------
defineExpose({ togglePlay, toggleMute, stepFrame })
</script>

<style scoped>
.player {
  position: relative;
  width: 100%;
  height: 100%;
  background: #000;
  overflow: hidden;
}

/* Both layers share the exact same box and fit, so a pixel at time T in the
   still sits where the same pixel sits in the video. */
.layer {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

.video {
  opacity: 0;
  transition: opacity 120ms linear;
}
.video.revealed { opacity: 1; }

.msg {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #555;
  font-size: 0.95rem;
}

/* Stream status overlay (issue #95): small pill, top-center, above the layers. */
.status-overlay {
  position: absolute;
  top: 12px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 6px 12px;
  border-radius: 16px;
  background: rgba(0,0,0,0.6);
  border: 1px solid rgba(255,255,255,0.2);
  color: #eee;
  font-size: 0.85rem;
  white-space: nowrap;
  pointer-events: none;
}
.status-overlay.unavailable, .status-overlay.codec { color: #ffb4a8; }
.status-overlay .retry-btn {
  pointer-events: auto;
  padding: 2px 10px;
  border-radius: 10px;
  border: 1px solid rgba(255,255,255,0.35);
  background: rgba(255,255,255,0.1);
  color: #fff;
  font-size: 0.8rem;
  cursor: pointer;
}
.status-overlay .retry-btn:hover { background: rgba(255,255,255,0.22); }
.status-overlay .spinner {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  border: 2px solid rgba(255,255,255,0.25);
  border-top-color: #fff;
  animation: status-spin 0.9s linear infinite;
}
@keyframes status-spin { to { transform: rotate(360deg); } }

.play-overlay {
  position: absolute;
  left: 50%;
  bottom: 16px;
  transform: translateX(-50%);
  width: 56px;
  height: 56px;
  border-radius: 50%;
  border: 1px solid rgba(255,255,255,0.25);
  background: rgba(0,0,0,0.45);
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  opacity: 0.85;
  transition: opacity 120ms, background 120ms;
}
.play-overlay:hover { opacity: 1; background: rgba(0,0,0,0.65); }

.volume-ctrl {
  position: absolute;
  right: 16px;
  bottom: 16px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  border-radius: 20px;
  background: rgba(0,0,0,0.45);
  border: 1px solid rgba(255,255,255,0.2);
  opacity: 0.85;
  transition: opacity 120ms;
}
.volume-ctrl:hover { opacity: 1; }

.vol-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  background: none;
  border: none;
  color: #fff;
  cursor: pointer;
  padding: 0;
}

.vol-slider {
  width: 90px;
  accent-color: #4caf50;
  cursor: pointer;
}
</style>
