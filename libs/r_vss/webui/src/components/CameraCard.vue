<template>
  <div class="card">
    <div class="snapshot-wrap" ref="snapshotWrap">
      <!-- Poster: the JPEG snapshot. Stays up (and refreshing) until live video
           paints, and is all the tile shows when the browser can't play the
           camera's native codec. -->
      <img
        :src="snapshotUrl"
        class="snapshot"
        alt=""
        @error="snapError = true"
      />
      <!-- Live video (passthrough fMP4 over MSE), pixel-aligned over the poster
           and revealed once it has a frame. Muted; clicks go to the card. -->
      <video ref="videoEl" class="live" :class="{ revealed }" muted playsinline></video>
      <div v-if="snapError && !revealed" class="no-snapshot">No image available</div>
    </div>
    <div class="info">
      <span class="name">{{ camera.friendly_name || camera.camera_name }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted } from 'vue'
import { fitAspect } from '../utils/aspect.js'
import { useLiveMse } from '../composables/useLiveMse.js'

const props = defineProps({
  camera: Object,
  index:  { type: Number, default: 0 },
  total:  { type: Number, default: 1 }
})

const snapError    = ref(false)
const snapshotUrl  = ref('')
const snapshotWrap = ref(null)
const reqW         = ref(640)
const reqH         = ref(360)
const videoEl      = ref(null)

// Live video for the tile. Only runs while the tile is actually on screen and
// the page is visible, so a large grid doesn't hold a session per hidden tile.
const { revealed, failed: liveFailed, start: startLive, stop: stopLive } =
  useLiveMse(videoEl, () => props.camera.id, { passthrough: true })
let onScreen = false
function syncLive() {
  const want = onScreen && document.visibilityState === 'visible' && !liveFailed.value
  if (want) startLive()
  else stopLive()
}
function onVisibility() { syncLive() }

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

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

function buildUrl() {
  const t = new Date(Date.now() - 5000).toISOString()
  return `/jpg?camera_id=${props.camera.id}&start_time=${encodeURIComponent(t)}&width=${reqW.value}&height=${reqH.value}`
}

function refresh() {
  if (revealed.value) return            // live video is up; the poster can rest
  snapError.value   = false
  snapshotUrl.value = buildUrl()
}
// When live video drops (tile scrolled away, stream failed), bring the poster
// back up to date right away instead of waiting for the next tick.
watch(revealed, (on) => { if (!on) refresh() })

let intervalId = null
let timeoutId  = null
let resizeObs  = null
let resizeDebounce = null
let viewObs    = null

onMounted(() => {
  recomputeRequestSize()
  snapshotUrl.value = buildUrl()
  const stagger = (props.index / Math.max(props.total, 1)) * 3000
  timeoutId = setTimeout(() => {
    refresh()
    intervalId = setInterval(refresh, 3000)
  }, stagger)
  if (snapshotWrap.value && typeof ResizeObserver !== 'undefined') {
    resizeObs = new ResizeObserver(() => {
      clearTimeout(resizeDebounce)
      resizeDebounce = setTimeout(() => {
        if (recomputeRequestSize()) refresh()
      }, 200)
    })
    resizeObs.observe(snapshotWrap.value)
  }
  if (snapshotWrap.value && typeof IntersectionObserver !== 'undefined') {
    viewObs = new IntersectionObserver((entries) => {
      onScreen = entries.some((e) => e.isIntersecting)
      syncLive()
    }, { threshold: 0.25 })
    viewObs.observe(snapshotWrap.value)
  } else {
    onScreen = true
    syncLive()
  }
  document.addEventListener('visibilitychange', onVisibility)
})

onUnmounted(() => {
  clearTimeout(timeoutId)
  clearInterval(intervalId)
  clearTimeout(resizeDebounce)
  if (resizeObs) resizeObs.disconnect()
  if (viewObs) viewObs.disconnect()
  document.removeEventListener('visibilitychange', onVisibility)
  stopLive()
})
</script>

<style scoped>
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

/* Same box and fit as the poster so the handoff is invisible. */
.live {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
  pointer-events: none;
  opacity: 0;
  transition: opacity 120ms linear;
}
.live.revealed { opacity: 1; }

.no-snapshot {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #555;
  font-size: 0.85rem;
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
