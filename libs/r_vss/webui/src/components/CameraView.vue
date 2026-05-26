<template>
  <div class="camera-view">
    <header class="header">
      <button class="back-btn" @click="$emit('close')">&#8592; Back</button>
      <span class="title">{{ camera.friendly_name || camera.camera_name }}</span>
    </header>

    <div class="image-wrap" ref="imageWrap">
      <img :src="snapshotUrl" class="snapshot" alt="" />
      <div v-if="snapError" class="no-snapshot">No image available</div>
    </div>

    <div class="timeline-bar">
      <TimelineBar
        :camera-id="camera.id"
        :camera-name="camera.friendly_name || camera.camera_name"
        @seek="onSeek"
        @live="onLive"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import TimelineBar from './TimelineBar.vue'
import { fitAspect } from '../utils/aspect.js'

const props = defineProps({ camera: Object })
defineEmits(['close'])

const snapshotUrl = ref('')
const snapError   = ref(false)
const seekedTime  = ref(null)  // null = live mode
const imageWrap   = ref(null)
const reqW        = ref(1280)
const reqH        = ref(720)

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

function recomputeRequestSize() {
  const el = imageWrap.value
  if (!el || el.clientWidth === 0 || el.clientHeight === 0) return false
  const dpr = window.devicePixelRatio || 1
  const boxW = el.clientWidth  * dpr
  const boxH = el.clientHeight * dpr

  const srcW = Number(props.camera.width)  || 0
  const srcH = Number(props.camera.height) || 0

  let newW, newH
  if (srcW > 0 && srcH > 0) {
    // Fit the source's aspect ratio into the box, then cap at the native
    // source resolution — no point asking the server to upscale beyond what
    // the camera produces.
    const fit = fitAspect(srcW, srcH, boxW, boxH)
    newW = Math.round(Math.min(srcW, fit.width))
    newH = Math.round(Math.min(srcH, fit.height))
  } else {
    // Source resolution unknown — fall back to box dims with generic clamp.
    newW = clamp(Math.round(boxW), 640, 2560)
    newH = clamp(Math.round(boxH), 360, 1440)
  }
  // Floor guard so we never request something silly-small on a collapsed pane.
  newW = Math.max(320, newW)
  newH = Math.max(180, newH)

  // Only treat as a meaningful change if either dimension shifts >5%.
  const changed = Math.abs(newW - reqW.value) > reqW.value * 0.05
              ||  Math.abs(newH - reqH.value) > reqH.value * 0.05
  reqW.value = newW
  reqH.value = newH
  return changed
}

function currentIsoString() {
  return seekedTime.value !== null
    ? seekedTime.value
    : new Date(Date.now() - 5000).toISOString()
}

function loadFrame(isoString) {
  const url = `/jpg?camera_id=${props.camera.id}&start_time=${encodeURIComponent(isoString)}&width=${reqW.value}&height=${reqH.value}`
  const img = new Image()
  img.onload  = () => { snapshotUrl.value = url; snapError.value = false }
  img.onerror = () => { snapError.value = true }
  img.src = url
}

function refresh() {
  if (seekedTime.value !== null) return
  loadFrame(currentIsoString())
}

function onSeek(isoString) {
  seekedTime.value = isoString
  loadFrame(isoString)
}

function onLive() {
  seekedTime.value = null
}

let timer = null
let resizeObs = null
let resizeDebounce = null
onMounted(() => {
  recomputeRequestSize()
  refresh()
  timer = setInterval(refresh, 3000)
  if (imageWrap.value && typeof ResizeObserver !== 'undefined') {
    resizeObs = new ResizeObserver(() => {
      clearTimeout(resizeDebounce)
      resizeDebounce = setTimeout(() => {
        if (recomputeRequestSize())
          loadFrame(currentIsoString())
      }, 200)
    })
    resizeObs.observe(imageWrap.value)
  }
})
onUnmounted(() => {
  clearInterval(timer)
  clearTimeout(resizeDebounce)
  if (resizeObs) resizeObs.disconnect()
})
</script>

<style scoped>
.camera-view {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #0f0f0f;
}

.header {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem 1.5rem;
  background: #1a1a1a;
  border-bottom: 1px solid #2a2a2a;
  flex-shrink: 0;
}

.back-btn {
  background: none;
  border: 1px solid #3a3a3a;
  color: #ccc;
  padding: 0.3rem 0.75rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}

.back-btn:hover { background: #2a2a2a; color: #fff; }

.title {
  font-size: 1.1rem;
  font-weight: 500;
}

.image-wrap {
  flex: 1;
  background: #000;
  position: relative;
  overflow: hidden;
  min-height: 0;
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
  font-size: 0.95rem;
}

.timeline-bar {
  height: 112px;
  flex-shrink: 0;
  background: #111;
  border-top: 1px solid #2a2a2a;
}
</style>
