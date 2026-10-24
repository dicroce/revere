<!--
  ThumbTile — one camera's still in the synchronized Timeline view.

  Shows the /jpg frame nearest `timeMs` (ms since epoch), sized to the tile like
  CameraCard. Loads go through an off-screen Image so the previous frame stays
  on screen until the new one has arrived (no flash to blank), and so a load
  that is superseded by a newer time is abandoned: its request is cancelled by
  clearing src and its result ignored if it still lands. Changes to `timeMs`
  are debounced so a fast scrub produces one request per tile, not dozens.
-->
<template>
  <div class="card" @click="$emit('click')">
    <div class="snapshot-wrap" ref="snapshotWrap">
      <img v-if="shownUrl" :src="shownUrl" class="snapshot" alt="" />
      <div v-if="!shownUrl && failed" class="no-snapshot">No image available</div>
      <div v-else-if="stale" class="stale-badge">no frame at this time</div>
    </div>
    <div class="info">
      <span class="name">{{ camera.friendly_name || camera.camera_name }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted } from 'vue'
import { fitAspect } from '../utils/aspect.js'

const props = defineProps({
  camera: { type: Object, required: true },
  // Shared playhead, ms since epoch. Every tile in the grid gets the same value.
  timeMs: { type: Number, required: true },
})
defineEmits(['click'])

// How long to wait after the last timeMs change before requesting a frame.
const DEBOUNCE_MS = 150

const snapshotWrap = ref(null)
const shownUrl     = ref('')     // what's on screen (last successful load)
const failed       = ref(false)  // last request failed and nothing is shown
const stale        = ref(false)  // last request failed but an older frame is still shown
const reqW         = ref(640)
const reqH         = ref(360)

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

// Same sizing rule as CameraCard: fit the camera's aspect inside the tile box
// at device pixels, never upscale past the source, with sane floors.
function recomputeRequestSize() {
  const el = snapshotWrap.value
  if (!el || el.clientWidth === 0 || el.clientHeight === 0) return false
  const dpr = window.devicePixelRatio || 1
  const boxW = el.clientWidth  * dpr
  const boxH = el.clientHeight * dpr

  const srcW = Number(props.camera.width)  || 0
  const srcH = Number(props.camera.height) || 0

  let newW, newH
  if (srcW > 0 && srcH > 0) {
    const fit = fitAspect(srcW, srcH, boxW, boxH)
    newW = Math.round(Math.min(srcW, fit.width))
    newH = Math.round(Math.min(srcH, fit.height))
  } else {
    newW = clamp(Math.round(boxW), 320, 1280)
    newH = clamp(Math.round(boxH), 180,  720)
  }
  newW = Math.max(160, newW)
  newH = Math.max(90,  newH)

  const changed = Math.abs(newW - reqW.value) > reqW.value * 0.05
              ||  Math.abs(newH - reqH.value) > reqH.value * 0.05
  reqW.value = newW
  reqH.value = newH
  return changed
}

function buildUrl(ms) {
  const t = new Date(ms).toISOString()
  return `/jpg?camera_id=${props.camera.id}&start_time=${encodeURIComponent(t)}&width=${reqW.value}&height=${reqH.value}`
}

// --- loading ---
let pending   = null   // the in-flight Image, if any
let loadSeq   = 0      // bumps per request; a finished load only applies if it is still current
let debounceId = null

function cancelPending() {
  if (!pending) return
  pending.onload = pending.onerror = null
  pending.src = ''   // aborts the in-flight request in all major browsers
  pending = null
}

function load(ms) {
  cancelPending()
  const seq = ++loadSeq
  const url = buildUrl(ms)
  const img = new Image()
  img.onload = () => {
    if (seq !== loadSeq) return
    pending = null
    shownUrl.value = url
    failed.value = false
    stale.value  = false
  }
  img.onerror = () => {
    if (seq !== loadSeq) return
    pending = null
    if (shownUrl.value) stale.value = true
    else failed.value = true
  }
  pending = img
  img.src = url
}

function scheduleLoad() {
  clearTimeout(debounceId)
  debounceId = setTimeout(() => load(props.timeMs), DEBOUNCE_MS)
}

watch(() => props.timeMs, scheduleLoad)

// --- lifecycle ---
let resizeObs = null
let resizeDebounce = null

onMounted(() => {
  recomputeRequestSize()
  load(props.timeMs)
  if (snapshotWrap.value && typeof ResizeObserver !== 'undefined') {
    resizeObs = new ResizeObserver(() => {
      clearTimeout(resizeDebounce)
      resizeDebounce = setTimeout(() => {
        if (recomputeRequestSize()) scheduleLoad()
      }, 200)
    })
    resizeObs.observe(snapshotWrap.value)
  }
})

onUnmounted(() => {
  clearTimeout(debounceId)
  clearTimeout(resizeDebounce)
  cancelPending()
  if (resizeObs) resizeObs.disconnect()
})
</script>

<style scoped>
/* Mirrors CameraCard so the Timeline grid matches the Live grid. */
.card {
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 6px;
  overflow: hidden;
  cursor: pointer;
}

.card:hover {
  border-color: #4a4a4a;
}

.snapshot-wrap {
  position: relative;
  width: 100%;
  aspect-ratio: 16 / 9;
  background: #111;
}

.snapshot {
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

.no-snapshot {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #555;
  font-size: 0.85rem;
}

.stale-badge {
  position: absolute;
  left: 0.5rem;
  bottom: 0.5rem;
  padding: 0.1rem 0.4rem;
  border-radius: 3px;
  background: rgba(0, 0, 0, 0.65);
  color: #999;
  font-size: 0.7rem;
}

.info {
  padding: 0.6rem 0.85rem;
}

.name {
  font-size: 0.9rem;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
