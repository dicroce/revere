// useLiveMse — drive a <video> element with live camera footage over Media
// Source Extensions, pulled from /transcode_fmp4 in its remux-only
// `passthrough=1` mode (the camera's native H.264/H.265, no server transcode).
//
// This is deliberately a small, standalone driver for grid tiles. It is not a
// refactor of CameraPlayer.vue (which keeps its own seek/gap-skipping MSE
// logic); it copies only the bits a live tile needs.
//
// Passthrough protocol (see r_ws.cpp _get_passthrough_fmp4): every response is a
// self-contained fMP4 whose PTS 0 is the key frame at X-Revere-First-Ts (epoch
// ms) and which ends just before the key frame at X-Revere-Next-Start. We ask
// for [nextStart, now) each time, so consecutive windows tile exactly on the
// real-time axis: timestampOffset = (firstTs - origin). An empty body means a
// whole GOP hasn't been committed yet — poll again shortly. Tiles are muted, so
// they ask for audio=0 (the server would otherwise AAC-encode G.711 for them).
//
// Returned refs:
//   revealed — the video has painted a frame (hide the poster)
//   failed   — this browser/camera can't play the stream (keep the poster);
//              e.g. an H.265 camera where MediaSource.isTypeSupported says no
import { ref } from 'vue'

const LIVE_MARGIN_MS    = 6000   // first window starts this far behind now (≥ ~1 GOP)
const LIVE_POLL_MS      = 1000   // re-ask when the server had no whole GOP yet
const FETCH_TIMEOUT_MS  = 15000
const RETRY_BASE_MS     = 500
const MAX_CONSEC_FAILS  = 6      // then give up and leave the poster up
const BEHIND_S          = 20     // evict buffered media older than this
const MAX_LAG_S         = 8      // if the playhead falls this far behind the live edge, jump
const FALLBACK_CODECS   = 'avc1.4d4029'

// Global budget so a big grid can't open a session per off-screen tile; tiles
// over budget simply keep their JPEG. (Tiles also start/stop on visibility.)
const MAX_TILE_PLAYERS = 12
let activeTilePlayers = 0

export function useLiveMse(videoRef, getCameraId, opts = {}) {
  const passthrough = opts.passthrough !== false
  const revealed = ref(false)
  const failed   = ref(false)

  // plain locals (not reactive)
  let mediaSource  = null
  let sourceBuffer = null
  let appendQueue  = []
  let running      = false
  let fetching     = false
  let firstSegment = true
  let originMs     = 0      // epoch ms that maps to presentation time 0
  let nextStartMs  = 0      // epoch ms the next window must start at
  let consecFails  = 0
  let pollTimer    = null
  let abort        = null
  let holdsSlot    = false

  const delay = (ms) => new Promise(r => setTimeout(r, ms))
  const once  = (t, ev) => new Promise(r => t.addEventListener(ev, r, { once: true }))

  function windowUrl(startMs, endMs) {
    const s = new Date(startMs).toISOString()
    const e = new Date(endMs).toISOString()
    return `/transcode_fmp4?camera_id=${encodeURIComponent(getCameraId())}`
      + `&start_time=${encodeURIComponent(s)}&end_time=${encodeURIComponent(e)}`
      + (passthrough ? '&passthrough=1&audio=0' : '')
  }

  async function fetchOnce(url) {
    const ctl = new AbortController()
    const onAbort = () => ctl.abort()
    abort.signal.addEventListener('abort', onAbort)
    const to = setTimeout(() => ctl.abort(), FETCH_TIMEOUT_MS)
    try {
      return await fetch(url, { signal: ctl.signal })
    } finally {
      clearTimeout(to)
      abort.signal.removeEventListener('abort', onAbort)
    }
  }

  function schedulePoll(ms) {
    clearTimeout(pollTimer)
    pollTimer = setTimeout(() => { pollTimer = null; fetchNextWindow() }, ms)
  }

  async function fetchNextWindow() {
    if (!running || fetching) return
    fetching = true
    try {
      const res = await fetchOnce(windowUrl(nextStartMs, Date.now()))
      if (!running) return

      if (res.status === 503) {                       // server at capacity: back off
        consecFails++
        schedulePoll(RETRY_BASE_MS * Math.min(consecFails, 6))
        return
      }
      if (!res.ok) {
        if (++consecFails >= MAX_CONSEC_FAILS) giveUp()
        else schedulePoll(RETRY_BASE_MS * consecFails)
        return
      }
      consecFails = 0

      const buf = await res.arrayBuffer()
      if (!running) return

      const nextStart = Number(res.headers.get('X-Revere-Next-Start'))
      if (!buf.byteLength) {
        // No whole GOP committed yet. Poll again; keep nextStartMs as-is.
        if (nextStart > 0) nextStartMs = nextStart
        schedulePoll(LIVE_POLL_MS)
        return
      }

      const firstTs = Number(res.headers.get('X-Revere-First-Ts')) || nextStartMs
      const codecs  = res.headers.get('X-Revere-Codecs') || FALLBACK_CODECS

      if (!sourceBuffer) {
        if (!setupSourceBuffer(codecs)) return       // codec unsupported → failed
        originMs = firstTs
      }

      let bytes = new Uint8Array(buf)
      if (firstSegment) firstSegment = false
      else              bytes = stripToFirstMoof(bytes)

      appendQueue.push({ bytes, offsetSec: (firstTs - originMs) / 1000 })
      if (nextStart > firstTs) nextStartMs = nextStart
      pump()
      // More may already be committed; ask again right away (the server answers
      // an incomplete GOP with an empty body, which drops us into the poll).
      schedulePoll(0)
    } catch (_) {
      if (!running || (abort && abort.signal.aborted)) return
      if (++consecFails >= MAX_CONSEC_FAILS) giveUp()
      else schedulePoll(RETRY_BASE_MS * consecFails)
    } finally {
      fetching = false
    }
  }

  function setupSourceBuffer(codecs) {
    const mime = `video/mp4; codecs="${codecs}"`
    if (typeof MediaSource === 'undefined' || !MediaSource.isTypeSupported(mime)) {
      giveUp()
      return false
    }
    try {
      sourceBuffer = mediaSource.addSourceBuffer(mime)
    } catch (e) {
      console.warn('useLiveMse: addSourceBuffer failed', mime, e)
      giveUp()
      return false
    }
    sourceBuffer.addEventListener('updateend', pump)
    return true
  }

  // Slice a fragmented-mp4 buffer from its first `moof` box (drop ftyp+moov).
  function stripToFirstMoof(u8) {
    const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength)
    let off = 0
    while (off + 8 <= u8.byteLength) {
      const size = dv.getUint32(off)
      if (u8[off+4]===0x6d && u8[off+5]===0x6f && u8[off+6]===0x6f && u8[off+7]===0x66) // 'moof'
        return u8.subarray(off)
      if (size < 8) break
      off += size
    }
    return u8
  }

  function pump() {
    const v = videoRef.value
    if (!sourceBuffer || sourceBuffer.updating || !v) return
    const t = v.currentTime
    const b = sourceBuffer.buffered

    if (t > BEHIND_S + 10 && b.length && b.start(0) < t - BEHIND_S) {
      try { sourceBuffer.remove(0, t - BEHIND_S) } catch (_) {}
      return                                        // updateend re-enters pump()
    }

    if (appendQueue.length) {
      const item = appendQueue.shift()
      try {
        if (sourceBuffer.timestampOffset !== item.offsetSec)
          sourceBuffer.timestampOffset = item.offsetSec
        sourceBuffer.appendBuffer(item.bytes)
      } catch (e) {
        console.warn('useLiveMse: append failed', e)
        giveUp()
      }
      return
    }

    // Stay near the live edge: a backgrounded tab or a stall can leave the
    // playhead well behind what we've buffered.
    if (b.length) {
      const end = b.end(b.length - 1)
      if (end - t > MAX_LAG_S) { try { v.currentTime = end - 1 } catch (_) {} }
      if (v.paused && revealed.value) v.play().catch(() => {})
    }
  }

  function onCanPlay() { revealed.value = true }
  function onVideoError() { giveUp() }

  function giveUp() {
    failed.value = true
    stop()
  }

  async function start() {
    if (running || failed.value) return
    if (typeof MediaSource === 'undefined') { failed.value = true; return }
    if (activeTilePlayers >= MAX_TILE_PLAYERS) return   // over budget: stay on the poster
    const v = videoRef.value
    if (!v) return

    activeTilePlayers++; holdsSlot = true
    running      = true
    firstSegment = true
    consecFails  = 0
    appendQueue  = []
    abort        = new AbortController()
    nextStartMs  = Date.now() - LIVE_MARGIN_MS

    v.muted = true
    v.playsInline = true
    v.addEventListener('canplay', onCanPlay, { once: true })
    v.addEventListener('timeupdate', pump)
    v.addEventListener('error', onVideoError)

    mediaSource = new MediaSource()
    v.src = URL.createObjectURL(mediaSource)
    await once(mediaSource, 'sourceopen')
    if (!running) return
    v.play().catch(() => {})
    fetchNextWindow()
  }

  function stop() {
    running  = false
    fetching = false
    revealed.value = false
    appendQueue = []
    clearTimeout(pollTimer); pollTimer = null
    if (abort) { try { abort.abort() } catch (_) {} abort = null }
    if (holdsSlot) { activeTilePlayers--; holdsSlot = false }

    const v = videoRef.value
    if (v) {
      v.pause()
      v.removeEventListener('canplay', onCanPlay)
      v.removeEventListener('timeupdate', pump)
      v.removeEventListener('error', onVideoError)
      v.removeAttribute('src')
      try { v.load() } catch (_) {}
    }
    if (sourceBuffer) { try { sourceBuffer.removeEventListener('updateend', pump) } catch (_) {} }
    if (mediaSource && mediaSource.readyState === 'open') { try { mediaSource.endOfStream() } catch (_) {} }
    mediaSource  = null
    sourceBuffer = null
  }

  return { revealed, failed, start, stop }
}
