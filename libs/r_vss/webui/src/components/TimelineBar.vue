<template>
  <div class="timeline">
    <div class="controls">
      <div class="left">
        <button class="ctrl-btn" @click="shiftBack" :disabled="exporting">&#8592;</button>
        <button class="ctrl-btn" @click="shiftForward" :disabled="exporting">&#8594;</button>
        <span class="time-label">{{ fmtTime(playheadTime) }}</span>
      </div>
      <div class="right">
        <span v-if="exporting" class="exporting-label">Exporting {{ exportPercent }}%</span>

        <template v-else-if="exportMode">
          <button class="ctrl-btn export-finish" @click="finishExport">Finish Export</button>
          <button class="ctrl-btn" @click="cancelExport">Cancel</button>
        </template>

        <template v-else>
          <select class="zoom-sel" v-model="rangeMinutes" @change="onZoomChange">
            <option :value="10">10m</option>
            <option :value="20">20m</option>
            <option :value="60">1h</option>
            <option :value="120">2h</option>
          </select>
          <button class="ctrl-btn" @click="startExport">Export</button>
          <button class="ctrl-btn" :class="{ active: isLive }" @click="goLive">Live</button>
        </template>
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
import { ref, onMounted, onUnmounted } from 'vue'

const props = defineProps({
  cameraId:   String,
  cameraName: { type: String, default: '' }
})
const emit  = defineEmits(['seek', 'live'])

const canvas          = ref(null)
const rangeMinutes    = ref(20)
const rangeStart      = ref(new Date())
const rangeEnd        = ref(new Date())
const playheadTime    = ref(new Date())
const isLive          = ref(true)
const segments        = ref([])
const motionEvents    = ref([])
const analyticsEvents = ref([])
const dragging        = ref(false)

// Export selection state. When exportMode is true, the user is positioning
// the end marker; the original playhead is frozen as the start. Once they
// click Finish Export we kick off /export and poll progress until the file
// is ready, then trigger a browser download via /export_download.
const exportMode    = ref(false)
const exportStart   = ref(null)  // Date — frozen start marker (was playheadTime when Export clicked)
const exportEnd     = ref(null)  // Date — user-positioned end marker
const exporting     = ref(false) // server-side export running (between Finish Export and download)
const exportPercent = ref(0)

// --- icon preload ---
const ICON_NAMES = ['airplane','backpack','bicycle','bird','boat','bus','car','cat',
                    'dog','handbag','motorcycle','person','suitcase','train','truck']
const iconImages = {}
for (const name of ICON_NAMES) {
  const img = new Image()
  img.src = `/icons/${name}.svg`
  iconImages[name] = img
}

// --- coordinate helpers ---
function msToX(ms) {
  const s = rangeStart.value.getTime()
  const e = rangeEnd.value.getTime()
  return ((ms - s) / (e - s)) * canvas.value.width
}

function xToMs(x) {
  const s = rangeStart.value.getTime()
  const e = rangeEnd.value.getTime()
  return s + (x / canvas.value.width) * (e - s)
}

function fmtTime(d) {
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function fmtLabel(ms) {
  return new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
}

function tickIntervals(rangeMs) {
  if (rangeMs <=  10 * 60000) return [  60000,  120000]
  if (rangeMs <=  20 * 60000) return [  60000,  300000]
  if (rangeMs <=  60 * 60000) return [ 300000,  900000]
  return                              [ 600000, 1800000]
}

// --- data ---
async function loadData() {
  if (!props.cameraId) return
  const s  = encodeURIComponent(rangeStart.value.toISOString())
  const e  = encodeURIComponent(rangeEnd.value.toISOString())
  const id = props.cameraId
  try {
    const [segRes, motRes, anaRes] = await Promise.all([
      fetch(`/contents?camera_id=${id}&start_time=${s}&end_time=${e}`),
      fetch(`/motion_events?camera_id=${id}&start_time=${s}&end_time=${e}`),
      fetch(`/analytics?camera_id=${id}&start_time=${s}&end_time=${e}`)
    ])
    if (segRes.ok) {
      const d = await segRes.json()
      segments.value = (d.segments || []).map(s => ({
        start: new Date(s.start_time).getTime(),
        end:   new Date(s.end_time).getTime()
      }))
    }
    if (motRes.ok) {
      const d = await motRes.json()
      motionEvents.value = (d.motion_events || []).map(m => ({
        start: new Date(m.start_time).getTime(),
        end:   new Date(m.end_time).getTime()
      }))
    }
    if (anaRes.ok) {
      const d = await anaRes.json()
      analyticsEvents.value = (d.analytics || []).map(a => ({
        start:      new Date(a.motion_start_time).getTime(),
        end:        new Date(a.motion_end_time).getTime(),
        detections: a.detections || []
      }))
    }
  } catch (_) {}
  draw()
}

// --- range ---
function setLiveRange() {
  const now = Date.now()
  rangeEnd.value     = new Date(now)
  rangeStart.value   = new Date(now - rangeMinutes.value * 60000)
  playheadTime.value = new Date(now)
}

function goLive() {
  isLive.value = true
  setLiveRange()
  loadData()
  emit('live')
}

function onZoomChange() {
  const pivot = playheadTime.value.getTime()
  const half  = rangeMinutes.value * 60000 / 2
  rangeStart.value = new Date(pivot - half)
  rangeEnd.value   = new Date(pivot + half)
  if (rangeEnd.value > new Date()) { goLive(); return }
  isLive.value = false
  loadData()
}

function shiftBack() {
  isLive.value = false
  const d = rangeMinutes.value * 60000 * 0.2
  rangeStart.value = new Date(rangeStart.value.getTime() - d)
  rangeEnd.value   = new Date(rangeEnd.value.getTime()   - d)
  loadData()
}

function shiftForward() {
  const d      = rangeMinutes.value * 60000 * 0.2
  const newEnd = new Date(rangeEnd.value.getTime() + d)
  if (newEnd >= new Date()) { goLive(); return }
  isLive.value = false
  rangeStart.value = new Date(rangeStart.value.getTime() + d)
  rangeEnd.value   = newEnd
  loadData()
}

// --- draw ---
const TICK_H  = 20
const ICON_SZ = 28

function draw() {
  const cvs = canvas.value
  if (!cvs || cvs.width === 0) return
  const ctx = cvs.getContext('2d')
  const W = cvs.width
  const H = cvs.height

  const SEG_TOP = TICK_H
  const SEG_H   = H - TICK_H

  ctx.clearRect(0, 0, W, H)

  // background
  ctx.fillStyle = '#0a0a0a'
  ctx.fillRect(0, 0, W, H)

  // recording segments
  ctx.fillStyle = '#1e4976'
  for (const seg of segments.value) {
    const x1 = Math.max(0, msToX(seg.start))
    const x2 = Math.min(W, msToX(seg.end))
    if (x2 > x1) ctx.fillRect(x1, SEG_TOP, x2 - x1, SEG_H)
  }

  // motion events
  ctx.fillStyle = '#2e7d9e'
  for (const ev of motionEvents.value) {
    const x1 = Math.max(0, msToX(ev.start))
    const x2 = Math.min(W, msToX(ev.end))
    if (x2 > x1) ctx.fillRect(x1, SEG_TOP, x2 - x1, SEG_H)
  }

  // analytics icons — one icon per event (dominant class), skip if overlapping previous
  ctx.filter = 'invert(1)'
  let nextIconX = 0
  for (const ev of analyticsEvents.value) {
    const x = msToX(ev.start)
    if (x > W || x < 0) continue

    // pick the most-frequent class in this event
    const counts = {}
    for (const det of ev.detections)
      counts[det.class_name] = (counts[det.class_name] || 0) + 1
    const cls = Object.keys(counts).sort((a, b) => counts[b] - counts[a])[0]
    if (!cls) continue

    const img = iconImages[cls]
    if (!img || !img.complete) continue
    const nw = img.naturalWidth  || img.width
    const nh = img.naturalHeight || img.height
    if (!nw || !nh) continue

    const scale = Math.min(ICON_SZ / nw, ICON_SZ / nh)
    const dw = nw * scale
    const dh = nh * scale
    const midX = (msToX(ev.start) + msToX(ev.end)) / 2
    const dx   = midX - dw / 2
    if (dx < nextIconX || dx + dw > W) continue

    const dy = SEG_TOP + (SEG_H - dh) / 2
    ctx.drawImage(img, dx, dy, dw, dh)
    nextIconX = dx + dw + 4
  }
  ctx.filter = 'none'

  // tick marks + labels
  const rangeMs = rangeEnd.value.getTime() - rangeStart.value.getTime()
  const [minorMs, majorMs] = tickIntervals(rangeMs)
  const firstTick = Math.ceil(rangeStart.value.getTime() / minorMs) * minorMs
  ctx.font = '10px system-ui, sans-serif'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'bottom'
  for (let t = firstTick; t <= rangeEnd.value.getTime(); t += minorMs) {
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

  // tick/segment divider
  ctx.strokeStyle = '#222'
  ctx.lineWidth = 1
  ctx.beginPath()
  ctx.moveTo(0, TICK_H)
  ctx.lineTo(W, TICK_H)
  ctx.stroke()

  // playhead(s)
  ctx.strokeStyle = '#4caf50'
  ctx.lineWidth = 2
  const drawHead = (t) => {
    const x = msToX(t.getTime())
    if (x >= 0 && x <= W) {
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, H)
      ctx.stroke()
    }
  }
  if (exportMode.value) {
    if (exportStart.value) drawHead(exportStart.value)
    if (exportEnd.value)   drawHead(exportEnd.value)
  } else {
    drawHead(playheadTime.value)
  }
}

// --- mouse ---
let seekTimer = null

function handleSeek(e) {
  if (exporting.value) return
  const rect = canvas.value.getBoundingClientRect()
  const x    = (e.clientX - rect.left) * (canvas.value.width / rect.width)
  const ms   = Math.min(xToMs(x), Date.now())
  if (ms < rangeStart.value.getTime()) return

  if (exportMode.value) {
    // Drag the end marker only; clamp so it can't precede the start marker.
    const minMs = exportStart.value.getTime() + 1000  // at least 1 second of clip
    exportEnd.value = new Date(Math.max(ms, minMs))
    draw()
    return
  }

  isLive.value      = false
  playheadTime.value = new Date(ms)
  draw()
  clearTimeout(seekTimer)
  seekTimer = setTimeout(() => emit('seek', playheadTime.value.toISOString()), 100)
}

function onMouseDown(e) { if (exporting.value) return; dragging.value = true;  handleSeek(e) }
function onMouseMove(e) { if (dragging.value)  handleSeek(e) }
function onMouseUp()    { dragging.value = false }
function onMouseLeave() { dragging.value = false }

// --- export ---
function startExport() {
  // Freeze current playhead as start, default end one minute later (clamped
  // to now in case the user is already near-live).
  const startMs = playheadTime.value.getTime()
  const endMs   = Math.min(startMs + 60_000, Date.now())
  exportStart.value = new Date(startMs)
  exportEnd.value   = new Date(Math.max(endMs, startMs + 1000))
  exportMode.value  = true
  draw()
}

function cancelExport() {
  exportMode.value  = false
  exportStart.value = null
  exportEnd.value   = null
  draw()
}

function safeFileBase(name) {
  return (name || 'camera').trim().replace(/[^a-zA-Z0-9_-]+/g, '_').replace(/^_+|_+$/g, '') || 'camera'
}

function fmtForFile(d) {
  // 2026-05-23_16-03-09 — filesystem-safe variant of the ISO timestamp.
  return d.toISOString().replace('T', '_').replace(/[:.]/g, '-').replace(/Z$/, '')
}

let exportPoll = null

async function finishExport() {
  if (!exportStart.value || !exportEnd.value) return
  const s = exportStart.value.toISOString()
  const e = exportEnd.value.toISOString()
  const fileName = `${safeFileBase(props.cameraName)}_${fmtForFile(exportStart.value)}_${fmtForFile(exportEnd.value)}.mov`

  exporting.value     = true
  exportPercent.value = 0

  const params = new URLSearchParams({
    camera_id:  props.cameraId,
    start_time: s,
    end_time:   e,
    file_name:  fileName
  })

  let id
  try {
    const res = await fetch('/export?' + params.toString())
    if (!res.ok) throw new Error(`server returned ${res.status}`)
    id = (await res.json()).id
  } catch (err) {
    alert('Failed to start export: ' + err.message)
    exporting.value = false
    return
  }

  exportPoll = setInterval(async () => {
    try {
      const r = await fetch('/export_progress?id=' + encodeURIComponent(id))
      if (!r.ok) return
      const j = await r.json()
      exportPercent.value = j.percent_complete | 0
      if (exportPercent.value >= 100) {
        clearInterval(exportPoll)
        exportPoll = null
        // Trigger the browser download. /export_download serves with
        // Content-Disposition: attachment so the browser saves rather than navigates.
        window.location.href = '/export_download?id=' + encodeURIComponent(id)
        // Reset selection state.
        exporting.value   = false
        exportMode.value  = false
        exportStart.value = null
        exportEnd.value   = null
        draw()
      }
    } catch (_) { /* keep polling */ }
  }, 1000)
}

// --- resize ---
let ro = null
function resize() {
  if (!canvas.value) return
  canvas.value.width  = canvas.value.clientWidth
  canvas.value.height = canvas.value.clientHeight
  draw()
}

// --- lifecycle ---
let liveTimer = null
let drawTimer = null

onMounted(() => {
  goLive()

  ro = new ResizeObserver(resize)
  ro.observe(canvas.value)
  resize()

  // Refresh data every 30s in live mode
  liveTimer = setInterval(() => {
    if (isLive.value) { setLiveRange(); loadData(); emit('live') }
  }, 30000)

  // Advance playhead every 2s in live mode
  drawTimer = setInterval(() => {
    if (isLive.value) {
      playheadTime.value = new Date()
      rangeEnd.value     = new Date()
      rangeStart.value   = new Date(Date.now() - rangeMinutes.value * 60000)
      draw()
    }
  }, 2000)
})

onUnmounted(() => {
  clearInterval(liveTimer)
  clearInterval(drawTimer)
  clearTimeout(seekTimer)
  if (exportPoll) clearInterval(exportPoll)
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

.ctrl-btn.export-finish {
  border-color: #4caf50;
  color: #4caf50;
}

.exporting-label {
  font-size: 0.8rem;
  color: #4caf50;
  font-variant-numeric: tabular-nums;
}

.time-label {
  font-size: 0.8rem;
  color: #aaa;
  font-variant-numeric: tabular-nums;
  min-width: 5.5rem;
  padding-left: 0.25rem;
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

.canvas {
  flex: 1;
  width: 100%;
  display: block;
  cursor: crosshair;
  min-height: 0;
}
</style>
