<template>
  <CameraView
    v-if="selectedCamera"
    :camera="selectedCamera"
    @close="selectedCamera = null"
  />

  <div v-else class="app">
    <header class="header">
      <h1>Revere</h1>
      <span class="camera-count">{{ cameras.length }} camera{{ cameras.length !== 1 ? 's' : '' }}</span>
    </header>

    <main class="content">
      <div v-if="error" class="error">{{ error }}</div>

      <div v-else-if="cameras.length === 0 && !loading" class="empty">
        No cameras found. Add a camera in the Revere app.
      </div>

      <div v-else class="camera-grid">
        <CameraCard
          v-for="(camera, index) in cameras"
          :key="camera.id"
          :camera="camera"
          :index="index"
          :total="cameras.length"
          @click="selectedCamera = camera"
        />
      </div>
    </main>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import CameraCard from './components/CameraCard.vue'
import CameraView from './components/CameraView.vue'

const cameras        = ref([])
const loading        = ref(true)
const error          = ref(null)
const selectedCamera = ref(null)

onMounted(async () => {
  try {
    const res = await fetch('/cameras')
    if (!res.ok) throw new Error(`Server returned ${res.status}`)
    cameras.value = ((await res.json()).cameras ?? []).filter(c => c.state === 'assigned')
  } catch (e) {
    error.value = `Could not reach Revere: ${e.message}`
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.app {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

.header {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem 1.5rem;
  background: #1a1a1a;
  border-bottom: 1px solid #2a2a2a;
}

.header h1 {
  font-size: 1.25rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.camera-count {
  font-size: 0.85rem;
  color: #888;
}

.content {
  flex: 1;
  padding: 1.5rem;
}

.camera-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 1rem;
}

.empty,
.error {
  text-align: center;
  margin-top: 4rem;
  color: #888;
  font-size: 0.95rem;
}

.error {
  color: #e05555;
}
</style>
