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
         Volume starts at 0 (muted) every load, so nothing blasts audio on entry
         and live can autoplay; the user opts into sound with the slider below. -->
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

    <!-- Volume control (bottom-right), shown once video is on screen. -->
    <div v-if="revealed" class="volume-ctrl">
      <button class="vol-btn" :title="volume === 0 ? 'Unmute' : 'Mute'" @click="toggleMute">
        <svg v-if="volume === 0" viewBox="0 0 24 24" width="22" height="22"><path d="M7 9v6h4l5 5V4l-5 5H7z" fill="currentColor"/><path d="M19 5 5 19" stroke="currentColor" stroke-width="2"/></svg>
        <svg v-else viewBox="0 0 24 24" width="22" height="22"><path d="M7 9v6h4l5 5V4l-5 5H7z" fill="currentColor"/><path d="M17 7a6 6 0 0 1 0 10" fill="none" stroke="currentColor" stroke-width="2"/></svg>
      </button>
      <input class="vol-slider" type="range" min="0" max="1" step="0.05" v-model.number="volume" />
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

function applyVolume() {
  const v = video.value
  if (!v) return
  v.volume = volume.value
  v.muted  = volume.value === 0   // muted at 0 → true silence + autoplay allowed
}
function toggleMute() {
  if (volume.value > 0) { lastVolume = volume.value; volume.value = 0 }
  else volume.value = lastVolume || 0.6
}
watch(volume, applyVolume)

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

// Gap-skipping: recordings are full of holes (motion-only, offline). We play only
// within recorded segments and compress gaps out of the MSE timeline, so
// presentation time is contiguous even though wall-clock isn't.
let segments      = []          // [{startMs,endMs}] recorded segments, sorted
let presentationMs = 0          // next window's position on the (gap-free) MSE timeline
let timeline      = []          // [{presMs,wallMs,durMs}] maps presentation → wall-clock
let liveMode      = false       // chasing the live write-head (vs seeked playback)
let waitingForData = false      // live: caught up to the head, polling for more

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

  // Seek to the requested instant, or (live) to just behind the committed head.
  await loadSegments(live ? (Date.now() - 60000) : Date.parse(props.isoTime))
  if (live) {
    const head = liveHeadMs()
    ptsBaseMs = (head > 0 ? head : Date.now()) - LIVE_MARGIN_MS
  } else {
    ptsBaseMs = Date.parse(props.isoTime)
  }
  windowStartMs = ptsBaseMs

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

  applyVolume()                            // a fresh <video> defaults to full/un-muted
  pump()                                   // drain the queued first window
  video.value.play().catch(() => {})
}

function onCanPlay() {
  revealed.value = true
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
  } catch (_) { /* teardown or transient — leave list as-is */ }
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
async function fetchWindow(winStart, winEnd) {
  const s = new Date(winStart).toISOString()
  const e = new Date(winEnd).toISOString()
  const url = `/transcode_fmp4?camera_id=${props.camera.id}`
    + `&start_time=${encodeURIComponent(s)}&end_time=${encodeURIComponent(e)}`
    + `&width=${vidW}&height=${vidH}&bitrate=${BITRATE}`
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
      if (res.status === 503) { await delay(RETRY_BASE_MS * (attempt + 1)); continue }
      return res
    } catch (_) {
      clearTimeout(to); abort.signal.removeEventListener('abort', onAbort)
      if (abort && abort.signal.aborted) return null   // real teardown, don't retry
      await delay(RETRY_BASE_MS * (attempt + 1))        // timeout / network — retry
    }
  }
  feeding = false                                       // retries exhausted
  return null
}

async function fetchNextWindow() {
  if (!feeding || fetching || waitingForData) return
  fetching = true
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
    const winEnd   = Math.min(winStart + WINDOW_MS, seg.endMs)

    // Live: if this window is only short because it's clamped at the write-head
    // (not a real segment boundary), wait for more data rather than churning a
    // tiny window.
    if (liveMode && winEnd === seg.endMs && seg.endMs === liveHeadMs()
        && (winEnd - winStart) < MIN_LIVE_FETCH_MS) {
      scheduleLivePoll()
      return
    }
    if (winEnd <= winStart) { windowStartMs = seg.endMs; return }

    const res = await fetchWindow(winStart, winEnd)
    if (!res) return                                   // teardown / retries exhausted
    if (!res.ok) { feeding = false; return }

    const codecs = res.headers.get('X-Revere-Codecs')
    if (codecs) negotiatedCodecs = codecs
    const buf = await res.arrayBuffer()

    if (buf && buf.byteLength) {
      // 0-based self-contained fMP4. Keep the init on the first window, strip it
      // (append from first moof) on the rest. offsetSec places it on the gap-free
      // presentation timeline; timeline records the wall-clock mapping.
      let bytes = new Uint8Array(buf)
      if (firstSegment) firstSegment = false
      else              bytes = stripToFirstMoof(bytes)
      const durMs = winEnd - winStart
      appendQueue.push({ bytes, offsetSec: presentationMs / 1000 })
      timeline.push({ presMs: presentationMs, wallMs: winStart, durMs })
      presentationMs += durMs
      windowStartMs = winEnd
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

  if (feeding && bufferedAhead() < AHEAD_S) fetchNextWindow()
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
