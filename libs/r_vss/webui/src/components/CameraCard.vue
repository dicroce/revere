<template>
  <div class="card">
    <div class="snapshot-wrap">
      <img
        :src="snapshotUrl"
        class="snapshot"
        alt=""
        @error="snapError = true"
      />
      <div v-if="snapError" class="no-snapshot">No image available</div>
    </div>
    <div class="info">
      <span class="name">{{ camera.friendly_name || camera.camera_name }}</span>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'

const props = defineProps({
  camera: Object,
  index:  { type: Number, default: 0 },
  total:  { type: Number, default: 1 }
})

const snapError   = ref(false)
const snapshotUrl = ref('')

function buildUrl() {
  const t = new Date(Date.now() - 5000).toISOString()
  return `/jpg?camera_id=${props.camera.id}&start_time=${encodeURIComponent(t)}&width=640&height=360`
}

function refresh() {
  snapError.value   = false
  snapshotUrl.value = buildUrl()
}

let intervalId = null
let timeoutId  = null

onMounted(() => {
  snapshotUrl.value = buildUrl()
  const stagger = (props.index / Math.max(props.total, 1)) * 3000
  timeoutId = setTimeout(() => {
    refresh()
    intervalId = setInterval(refresh, 3000)
  }, stagger)
})

onUnmounted(() => {
  clearTimeout(timeoutId)
  clearInterval(intervalId)
})
</script>

<style scoped>
.card {
  background: #1a1a1a;
  border: 1px solid #2a2a2a;
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
  object-fit: cover;
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
