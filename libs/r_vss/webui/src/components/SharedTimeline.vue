<!--
  SharedTimeline — one timeline bar for many cameras (the Timeline view).

  A lean sibling of TimelineBar: the same tick/segment/playhead drawing and
  scrub/zoom/pan behaviour, but with no camera of its own. It fetches
  /contents and /motion_events for every camera in `cameraIds` over the visible
  range and draws the UNION: the recorded row is lit wherever any camera has
  footage, the motion row wherever any camera saw motion. (No export, no
  analytics icons — those stay single-camera.)

  The parent owns the playhead (`playheadMs`) and transport state; this
  component only reports user intent: seek(ms), live, toggle-play, speed.
-->
<template>
  <div class="timeline">
    <div class="controls">
      <div class="left">
        <button class="ctrl-btn" @click="shiftBack" title="Earlier">&#8592;</button>
        <button class="ctrl-btn" @click="shiftForward" title="Later">&#8594;</button>
        <span class="time-label">{{ fmtTime(playheadMs) }}</span>
        <span class="date-label">{{ fmtDate(playheadMs) }}</span>
      </div>
      <div class="right">
        <button
          class="ctrl-btn play-btn"
          :disabled="live"
          :title="playing ? 'Pause' : 'Play'"
          @click="$emit('toggle-play')"
        >
          <svg v-if="!playing" viewBox="0 0 24 24" width="14" height="14"><path d="M8 5v14l11-7z" fill="currentColor"/></svg>
          <svg v-else          viewBox="0 0 24 24" width="14" height="14"><path d="M6 5h4v14H6zM14 5h4v14h-4z" fill="currentColor"/></svg>
        </button>
        <select
          class="zoom-sel"
          :value="speed"
          :disabled="live"
          title="Playback speed"
          @change="$emit('update:speed', Number($event.target.value))"
        >
          <option :value="1">1x</option>
          <option :value="2">2x</option>
          <option :value="4">4x</option>
          <option :value="8">8x</option>
        </select>
        <select class="zoom-sel" v-model="rangeMinutes" @change="onZoomChange" title="Visible range">
          <option :value="10">10m</option>
          <option :value="20">20m</option>
          <option :value="60">1h</option>
          <option :value="120">2h</option>
          <option :value="360">6h</option>
        </select>
        <button class="ctrl-btn" :class="{ active: live }" @click="$emit('live')">Live</button>
      </div>
    </div>
    <canvas
      ref="canvas"
      class="canvas"
      @mousedown="onMouseDown"
      @mousemove="onMouseMove"
      @mouseup="onMouseUp"
      @mouseleave="onMouseLeave"
    />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted } from 'vue'

const props = defineProps({
  cameraIds:  { type: Array,   default: () => [] },
  // Shared playhead, ms since epoch.
  playheadMs: { type: Number,  required: true },
  live:       { type: Boolean, default: true },
  playing:    { type: Boolean, default: false },
  speed:      { type: Number,  default: 1 },
})
const emit = defineEmits(['seek', 'live', 'toggle-play', 'update:speed'])

const canvas       = ref(null)
const rangeMinutes = ref(20)
const rangeStart   = ref(Date.now() - 20 * 60000)   // ms
const rangeEnd     = ref(Date.now())                 // ms
const segments     = ref([])   // union across cameras: [{ start, end }] ms
const motionEvents = ref([])   // union across cameras
const dragging     = ref(false)

// --- coordinate helpers ---
function msToX(ms) {
  return ((ms - rangeStart.value) / (rangeEnd.value - rangeStart.value)) * canvas.value.width
}

function xToMs(x) {
  return rangeStart.value + (x / canvas.value.width) * (rangeEnd.value - rangeStart.value)
}

function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function fmtDate(ms) {
  const d = new Date(ms)
  const today = new Date()
  if (d.toDateString() === today.toDateString()) return ''
  return d.toLocaleDateString([], { month: 'short', day: 'numeric' })
}

function fmtLabel(ms) {
  return new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
}

function tickIntervals(rangeMs) {
  if (rangeMs <=  10 * 60000) return [  60000,  120000]
  if (rangeMs <=  20 * 60000) return [  60000,  300000]
  if (rangeMs <=  60 * 60000) return [ 300000,  900000]
  if (rangeMs <= 120 * 60000) return [ 600000, 1800000]
  return                              [1800000, 3600000]
}

// --- data ---

// Coalesce overlapping/adjacent intervals into their union.
function mergeIntervals(list) {
  const sorted = list.filter(i => i.end > i.start).sort((a, b) => a.start - b.start)
  const out = []
  for (const iv of sorted) {
    const last = out[out.length - 1]
    if (last && iv.start <= last.end) last.end = Math.max(last.end, iv.end)
    else out.push({ start: iv.start, end: iv.end })
  }
  return out
}

let loadSeq = 0

async function loadData() {
  const ids = props.cameraIds
  const seq = ++loadSeq
  if (!ids.length) { segments.value = []; motionEvents.value = []; draw(); return }

  const s = encodeURIComponent(new Date(rangeStart.value).toISOString())
  const e = encodeURIComponent(new Date(rangeEnd.value).toISOString())

  const toMs = (o, a, b) => ({ start: new Date(o[a]).getTime(), end: new Date(o[b]).getTime() })

  const perCamera = await Promise.all(ids.map(async (id) => {
    const r = { segs: [], motion: [] }
    try {
      const [segRes, motRes] = await Promise.all([
        fetch(`/contents?camera_id=${id}&start_time=${s}&end_time=${e}`),
        fetch(`/motion_events?camera_id=${id}&start_time=${s}&end_time=${e}`),
      ])
      if (segRes.ok) r.segs   = ((await segRes.json()).segments      || []).map(x => toMs(x, 'start_time', 'end_time'))
      if (motRes.ok) r.motion = ((await motRes.json()).motion_events || []).map(x => toMs(x, 'start_time', 'end_time'))
    } catch (_) { /* a camera that fails just contributes nothing */ }
    return r
  }))

  if (seq !== loadSeq) return   // a newer range superseded this request
  segments.value     = mergeIntervals(perCamera.flatMap(r => r.segs))
  motionEvents.value = mergeIntervals(perCamera.flatMap(r => r.motion))
  draw()
}

// --- range ---
function setLiveRange() {
  const now = Date.now()
  rangeEnd.value   = now
  rangeStart.value = now - rangeMinutes.value * 60000
}

function onZoomChange() {
  const half = rangeMinutes.value * 60000 / 2
  rangeStart.value = props.playheadMs - half
  rangeEnd.value   = props.playheadMs + half
  if (rangeEnd.value > Date.now()) { setLiveRange() }
  loadData()
}

function shiftBack() {
  const d = rangeMinutes.value * 60000 * 0.2
  rangeStart.value -= d
  rangeEnd.value   -= d
  loadData()
}

function shiftForward() {
  const d = rangeMinutes.value * 60000 * 0.2
  if (rangeEnd.value + d >= Date.now()) { setLiveRange() }
  else { rangeStart.value += d; rangeEnd.value += d }
  loadData()
}

// --- draw ---
const TICK_H = 20

function draw() {
  const cvs = canvas.value
  if (!cvs || cvs.width === 0) return
  const ctx = cvs.getContext('2d')
  const W = cvs.width
  const H = cvs.height

  const SEG_TOP = TICK_H
  const SEG_H   = H - TICK_H

  ctx.clearRect(0, 0, W, H)
  ctx.fillStyle = '#0a0a0a'
  ctx.fillRect(0, 0, W, H)

  // recorded (any camera)
  ctx.fillStyle = '#1e4976'
  for (const seg of segments.value) {
    const x1 = Math.max(0, msToX(seg.start))
    const x2 = Math.min(W, msToX(seg.end))
    if (x2 > x1) ctx.fillRect(x1, SEG_TOP, x2 - x1, SEG_H)
  }

  // motion (any camera)
  ctx.fillStyle = '#2e7d9e'
  for (const ev of motionEvents.value) {
    const x1 = Math.max(0, msToX(ev.start))
    const x2 = Math.min(W, msToX(ev.end))
    if (x2 > x1) ctx.fillRect(x1, SEG_TOP, x2 - x1, SEG_H)
  }

  // ticks + labels
  const rangeMs = rangeEnd.value - rangeStart.value
  const [minorMs, majorMs] = tickIntervals(rangeMs)
  const firstTick = Math.ceil(rangeStart.value / minorMs) * minorMs
  ctx.font = '10px system-ui, sans-serif'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'bottom'
  for (let t = firstTick; t <= rangeEnd.value; t += minorMs) {
    const x = msToX(t)
    if (x < 0 || x > W) continue
    const major = t % majorMs === 0
    ctx.strokeStyle = major ? '#555' : '#363636'
    ctx.lineWidth = 1
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, major ? TICK_H - 4 : TICK_H / 2)
    ctx.stroke()
    if (major) {
      ctx.fillStyle = '#777'
      ctx.fillText(fmtLabel(t), x, TICK_H - 5)
    }
  }

  ctx.strokeStyle = '#222'
  ctx.lineWidth = 1
  ctx.beginPath()
  ctx.moveTo(0, TICK_H)
  ctx.lineTo(W, TICK_H)
  ctx.stroke()

  // playhead
  const x = msToX(props.playheadMs)
  if (x >= 0 && x <= W) {
    ctx.strokeStyle = '#4caf50'
    ctx.lineWidth = 2
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, H)
    ctx.stroke()
  }
}

// --- mouse ---
function handleSeek(e) {
  const rect = canvas.value.getBoundingClientRect()
  const x    = (e.clientX - rect.left) * (canvas.value.width / rect.width)
  const ms   = Math.min(xToMs(x), Date.now())
  if (ms < rangeStart.value) return
  emit('seek', Math.round(ms))
}

function onMouseDown(e) { dragging.value = true; handleSeek(e) }
function onMouseMove(e) { if (dragging.value) handleSeek(e) }
function onMouseUp()    { dragging.value = false }
function onMouseLeave() { dragging.value = false }

// Follow the playhead: redraw, and recenter if playback runs off the visible
// range (not while the user is dragging, so we don't fight them).
watch(() => props.playheadMs, (ms) => {
  if (!props.live && !dragging.value && (ms > rangeEnd.value || ms < rangeStart.value)) {
    const half = rangeMinutes.value * 60000 / 2
    rangeStart.value = ms - half
    rangeEnd.value   = ms + half
    if (rangeEnd.value > Date.now()) setLiveRange()
    loadData()
  }
  draw()
})

watch(() => props.live, (live) => {
  if (live) { setLiveRange(); loadData() }
})

// The recording camera set changes (App polls /cameras): refetch the union.
watch(() => props.cameraIds.join(','), () => loadData())

// --- resize / lifecycle ---
let ro = null
function resize() {
  if (!canvas.value) return
  canvas.value.width  = canvas.value.clientWidth
  canvas.value.height = canvas.value.clientHeight
  draw()
}

let liveTimer = null
let drawTimer = null

onMounted(() => {
  setLiveRange()
  loadData()

  ro = new ResizeObserver(resize)
  ro.observe(canvas.value)
  resize()

  // Live: authoritative /contents refresh every 30s, range follows now every 2s.
  liveTimer = setInterval(() => {
    if (props.live) { setLiveRange(); loadData() }
  }, 30000)
  drawTimer = setInterval(() => {
    if (!props.live) return
    setLiveRange()
    draw()
  }, 2000)
})

onUnmounted(() => {
  clearInterval(liveTimer)
  clearInterval(drawTimer)
  if (ro) ro.disconnect()
})
</script>

<style scoped>
.timeline {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.controls {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 0.6rem;
  height: 32px;
  flex-shrink: 0;
  border-bottom: 1px solid #1e1e1e;
}

.left, .right {
  display: flex;
  align-items: center;
  gap: 0.35rem;
}

.ctrl-btn {
  background: none;
  border: 1px solid #3a3a3a;
  color: #ccc;
  padding: 0.15rem 0.5rem;
  border-radius: 3px;
  cursor: pointer;
  font-size: 0.8rem;
  line-height: 1.5;
}

.ctrl-btn:hover { background: #363636; color: #fff; }

.ctrl-btn.active {
  border-color: #4caf50;
  color: #4caf50;
}

.ctrl-btn:disabled {
  opacity: 0.4;
  cursor: default;
}

.play-btn {
  display: inline-flex;
  align-items: center;
  padding: 0.3rem 0.5rem;
}

.time-label {
  font-size: 0.8rem;
  color: #aaa;
  font-variant-numeric: tabular-nums;
  min-width: 5.5rem;
  padding-left: 0.25rem;
}

.date-label {
  font-size: 0.75rem;
  color: #777;
}

.zoom-sel {
  background: #1e1e1e;
  border: 1px solid #3a3a3a;
  color: #ccc;
  padding: 0.1rem 0.25rem;
  border-radius: 3px;
  font-size: 0.8rem;
  cursor: pointer;
}

.zoom-sel:disabled {
  opacity: 0.4;
  cursor: default;
}

.canvas {
  flex: 1;
  width: 100%;
  display: block;
  cursor: crosshair;
  min-height: 0;
}
</style>
