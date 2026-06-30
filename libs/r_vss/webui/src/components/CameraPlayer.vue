<!--
  CameraPlayer — the video surface for a single camera.

  Three display modes, driven entirely by the `isoTime` prop:
    • live  (isoTime === null) — a JPEG that refreshes every few seconds. Live
      video is a follow-up; for now "live" is the same snapshot slideshow the UI
      has always shown.
    • scrub (isoTime is an ISO string, not playing) — a single JPEG at that
      instant. Cheap and instant, ideal while dragging the timeline.
    • play  (the user hit Play on a scrubbed position) — real video via Media
      Source Extensions, appended from the server's /transcode_fmp4 endpoint.

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
         Not muted: playback is started by the ▶ click (a user gesture), so audio
         is allowed; cameras without audio simply play silent. -->
    <video ref="video" class="layer video" :class="{ revealed }" playsinline></video>

    <div v-if="stillError && !revealed" class="msg">No image available</div>

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
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { fitAspect } from '../utils/aspect.js'

const props = defineProps({
  camera:  { type: Object, required: true },
  // null => live (still refresh); ISO string => a seeked recorded position.
  isoTime: { type: String, default: null },
})
// Absolute ISO of the currently displayed video frame, so the host can follow
// the playhead on the timeline. Emitted while playing.
const emit = defineEmits(['timeupdate'])

const isLive = computed(() => props.isoTime === null)

// --- playback tuning -------------------------------------------------------
const BITRATE      = 4_000_000
const FPS_NUM      = 30
const FPS_DEN      = 1
const WINDOW_MS    = 3000   // fMP4 window pulled per request
const AHEAD_S      = 10     // keep this many seconds buffered ahead of playhead
const BEHIND_S     = 30     // evict buffered media older than this behind playhead
const MAX_W        = 1280   // cap transcode at 720p for v1 (within avc1 4.1, lighter)
const MAX_H        = 720
// Fallback MSE codec string if the server doesn't send X-Revere-Codecs (h264
// Main@4.1, video only). Normally the first window's header supplies the real one.
const FALLBACK_CODECS = 'avc1.4d4029'

// --- refs / reactive state -------------------------------------------------
const wrap       = ref(null)
const video      = ref(null)
const stillUrl   = ref('')
const stillError = ref(false)
const revealed   = ref(false)   // the entire "swap": still shown until this flips
const playing    = ref(false)
const reqW       = ref(1280)
const reqH       = ref(720)

// --- MSE driver state (plain locals; not reactive) -------------------------
let mediaSource  = null
let sourceBuffer = null
let appendQueue  = []
let feeding      = false        // still pulling windows from the server
let fetching     = false        // a window request is in flight (single-flight)
let firstSegment = true         // the first appended window carries the init segment
let negotiatedCodecs = null     // exact MSE codec string from the first window's header
let ptsBaseMs    = 0            // MSE timeline origin for this play session
let windowStartMs = 0          // wall-clock start of the next window to fetch
let vidW = 0, vidH = 0
let abort        = null

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

async function startPlayback() {
  if (props.isoTime === null) return
  stopVideo()                              // clean slate

  const sz = computeVideoSize()
  vidW = sz.w; vidH = sz.h
  ptsBaseMs       = Date.parse(props.isoTime)
  windowStartMs   = ptsBaseMs
  firstSegment    = true
  negotiatedCodecs = null
  abort           = new AbortController()

  mediaSource = new MediaSource()
  video.value.src = URL.createObjectURL(mediaSource)
  await once(mediaSource, 'sourceopen')
  if (!mediaSource) return                 // torn down while we awaited

  playing.value = true
  feeding = true

  // Fetch the first window before creating the SourceBuffer: the server reports
  // the exact codec string (including whether audio is present) in a response
  // header, and addSourceBuffer must be told the right codecs up front. pump()
  // no-ops until sourceBuffer exists, so the bytes just queue.
  await fetchNextWindow()
  if (!mediaSource) return                 // torn down mid-fetch

  const mime = `video/mp4; codecs="${negotiatedCodecs || FALLBACK_CODECS}"`
  try {
    sourceBuffer = mediaSource.addSourceBuffer(mime)
  } catch (e) {
    console.error('CameraPlayer: addSourceBuffer failed', mime, e)
    stopVideo()
    return
  }
  sourceBuffer.addEventListener('updateend', pump)

  // The reveal — hide the still the moment the video can paint.
  video.value.addEventListener('canplay', onCanPlay, { once: true })
  video.value.addEventListener('timeupdate', onVideoTime)

  pump()                                   // drain the queued first window
  video.value.play().catch(() => {})
}

function onCanPlay() { revealed.value = true }

function onVideoTime() {
  if (video.value)
    emit('timeupdate', new Date(ptsBaseMs + video.value.currentTime * 1000).toISOString())
  pump()
}

async function fetchNextWindow() {
  if (!feeding || fetching) return
  fetching = true
  try {
    const winStart = windowStartMs
    const s = new Date(winStart).toISOString()
    const e = new Date(winStart + WINDOW_MS).toISOString()
    const url = `/transcode_fmp4?camera_id=${props.camera.id}`
      + `&start_time=${encodeURIComponent(s)}&end_time=${encodeURIComponent(e)}`
      + `&width=${vidW}&height=${vidH}&bitrate=${BITRATE}`
      + `&framerate_num=${FPS_NUM}&framerate_den=${FPS_DEN}&codec=h264`
    const res = await fetch(url, { signal: abort.signal })
    if (!res.ok) { feeding = false; return }
    const codecs = res.headers.get('X-Revere-Codecs')
    if (codecs) negotiatedCodecs = codecs
    const buf = await res.arrayBuffer()
    if (buf && buf.byteLength) {
      // Each window is a self-contained, 0-based fMP4 (ftyp+moov+fragments). MSE
      // wants a single init segment then media-only appends, so keep the init on
      // the first window and strip it (append from the first moof) on the rest.
      // offsetSec positions this 0-based window on the global timeline.
      let bytes = new Uint8Array(buf)
      if (firstSegment) firstSegment = false
      else              bytes = stripToFirstMoof(bytes)
      const offsetSec = (winStart - ptsBaseMs) / 1000
      appendQueue.push({ bytes, offsetSec })
      windowStartMs += WINDOW_MS
    } else {
      feeding = false                      // ran off the end of the recording
    }
  } catch (_) {
    // aborted (seek/teardown) or network error — stop feeding quietly
    feeding = false
  } finally {
    fetching = false
  }
  pump()
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

  if (feeding && bufferedAhead() < AHEAD_S) fetchNextWindow()
}

function stopVideo() {
  feeding  = false
  fetching = false
  revealed.value = false
  appendQueue = []
  if (abort) { try { abort.abort() } catch (_) {} abort = null }

  const v = video.value
  if (v) {
    v.pause()
    v.removeEventListener('timeupdate', onVideoTime)
    v.removeEventListener('canplay', onCanPlay)
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

// --- react to seek / live --------------------------------------------------
watch(() => props.isoTime, (iso) => {
  stopVideo()
  playing.value = false
  if (iso === null) { startLiveRefresh(); loadStill(liveIso()) }
  else              { stopLiveRefresh();  loadStill(iso) }
})

// --- lifecycle -------------------------------------------------------------
let resizeObs = null
let resizeDebounce = null
onMounted(() => {
  recomputeStillSize()
  if (props.isoTime === null) { startLiveRefresh(); loadStill(liveIso()) }
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
</style>
