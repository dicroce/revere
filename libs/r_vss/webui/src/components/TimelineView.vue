<!--
  TimelineView — synchronized multi-camera review as thumbnails (#99).

  A grid of every recording camera, each tile a still, with ONE shared timeline
  underneath. The timeline's playhead T drives every tile: each shows the /jpg
  frame nearest T, so no transcode sessions are involved.

    • Scrub: drag on the timeline sets T; tiles refresh (debounced per tile).
    • Play: T advances at `speed` x wall clock; tiles refresh every
      PLAY_REFRESH_MS. Reaching "now" drops back to Live.
    • Live: T follows now; tiles refresh every LIVE_REFRESH_MS, like the Live tab.

  Clicking a tile opens the single-camera view at T.
-->
<template>
  <div class="timeline-view">
    <div class="thumb-grid">
      <ThumbTile
        v-for="camera in cameras"
        :key="camera.id"
        :camera="camera"
        :time-ms="tileTimeMs"
        @click="openCamera(camera)"
      />
    </div>

    <div class="bar">
      <SharedTimeline
        :camera-ids="cameraIds"
        :playhead-ms="playheadMs"
        :live="live"
        :playing="playing"
        v-model:speed="speed"
        @seek="onSeek"
        @live="goLive"
        @toggle-play="togglePlay"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import ThumbTile from './ThumbTile.vue'
import SharedTimeline from './SharedTimeline.vue'

const props = defineProps({
  cameras: { type: Array, default: () => [] },
})
// open(camera, isoTime | null): the user clicked a tile; null means "live".
const emit = defineEmits(['open'])

// --- cadence (ms) ---
const LIVE_REFRESH_MS = 3000   // tile refresh while following now (matches CameraCard)
const LIVE_LAG_MS     = 5000   // request this far behind now so the frame has been written
const PLAY_REFRESH_MS = 1000   // tile refresh while playing
const PLAY_TICK_MS    = 250    // playhead marker advance while playing

const cameraIds = computed(() => props.cameras.map(c => c.id))

const playheadMs = ref(Date.now())              // what the timeline draws / the label shows
const tileTimeMs = ref(Date.now() - LIVE_LAG_MS) // what the tiles are asked for
const live       = ref(true)
const playing    = ref(false)
const speed      = ref(1)

// --- transport ---
let liveTimer = null
let playTimer = null
let lastTickMs = 0       // wall-clock time of the previous play tick
let sinceRefreshMs = 0   // wall-clock elapsed since tiles were last refreshed while playing

function stopTimers() {
  clearInterval(liveTimer); liveTimer = null
  clearInterval(playTimer); playTimer = null
}

function goLive() {
  stopTimers()
  live.value    = true
  playing.value = false
  const tick = () => {
    const now = Date.now()
    playheadMs.value = now
    tileTimeMs.value = now - LIVE_LAG_MS
  }
  tick()
  liveTimer = setInterval(tick, LIVE_REFRESH_MS)
}

function startPlaying() {
  stopTimers()
  playing.value  = true
  lastTickMs     = Date.now()
  sinceRefreshMs = 0
  playTimer = setInterval(() => {
    const now     = Date.now()
    const elapsed = now - lastTickMs
    lastTickMs    = now
    const next = playheadMs.value + elapsed * speed.value
    if (next >= now - LIVE_LAG_MS) { goLive(); return }   // caught up with now
    playheadMs.value = next
    sinceRefreshMs += elapsed
    if (sinceRefreshMs >= PLAY_REFRESH_MS) {
      sinceRefreshMs = 0
      tileTimeMs.value = next
    }
  }, PLAY_TICK_MS)
}

function pause() {
  stopTimers()
  playing.value = false
}

function togglePlay() {
  if (live.value) return
  if (playing.value) pause()
  else startPlaying()
}

function onSeek(ms) {
  live.value = false
  playheadMs.value = ms
  tileTimeMs.value = ms
  if (playing.value) {
    // Keep playing from the new position.
    lastTickMs     = Date.now()
    sinceRefreshMs = 0
  } else {
    stopTimers()
  }
}

function openCamera(camera) {
  emit('open', camera, live.value ? null : new Date(playheadMs.value).toISOString())
}

onMounted(goLive)
onUnmounted(stopTimers)
</script>

<style scoped>
.timeline-view {
  display: flex;
  flex-direction: column;
  /* Fill the viewport below the header so the bar sits at the bottom even
     with a short grid; with more cameras the grid grows and the bar sticks. */
  min-height: calc(100vh - 130px);
}

.thumb-grid {
  flex: 1;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 1rem;
  align-content: start;
  padding-bottom: 1rem;
}

.bar {
  position: sticky;
  bottom: 0;
  height: 112px;
  flex-shrink: 0;
  background: #111;
  border-top: 1px solid #363636;
  /* Bleed over the content padding so the bar spans the full width. */
  margin: 0 -1.5rem -1.5rem;
}
</style>
