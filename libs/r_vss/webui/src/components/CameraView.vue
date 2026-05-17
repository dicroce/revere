<template>
  <div class="camera-view">
    <header class="header">
      <button class="back-btn" @click="$emit('close')">&#8592; Back</button>
      <span class="title">{{ camera.friendly_name || camera.camera_name }}</span>
    </header>

    <div class="image-wrap">
      <img :src="snapshotUrl" class="snapshot" alt="" />
      <div v-if="snapError" class="no-snapshot">No image available</div>
    </div>

    <div class="timeline-bar">
      <TimelineBar :camera-id="camera.id" @seek="onSeek" @live="onLive" />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import TimelineBar from './TimelineBar.vue'

const props = defineProps({ camera: Object })
defineEmits(['close'])

const snapshotUrl = ref('')
const snapError   = ref(false)
const seekedTime  = ref(null)  // null = live mode

function loadFrame(isoString) {
  const url = `/jpg?camera_id=${props.camera.id}&start_time=${encodeURIComponent(isoString)}&width=1280&height=720`
  const img = new Image()
  img.onload  = () => { snapshotUrl.value = url; snapError.value = false }
  img.onerror = () => { snapError.value = true }
  img.src = url
}

function refresh() {
  if (seekedTime.value !== null) return
  loadFrame(new Date(Date.now() - 5000).toISOString())
}

function onSeek(isoString) {
  seekedTime.value = isoString
  loadFrame(isoString)
}

function onLive() {
  seekedTime.value = null
}

let timer = null
onMounted(() => { refresh(); timer = setInterval(refresh, 3000) })
onUnmounted(() => clearInterval(timer))
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
