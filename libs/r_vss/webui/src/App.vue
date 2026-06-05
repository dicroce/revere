<template>
  <CameraView
    v-if="selectedCamera"
    :camera="selectedCamera"
    @close="selectedCamera = null"
  />

  <div v-else class="app">
    <header class="header">
      <h1>Revere</h1>
      <nav class="tabs">
        <button :class="{ active: tab === 'manage' }" @click="tab = 'manage'">Cameras</button>
        <button :class="{ active: tab === 'live' }" @click="tab = 'live'">Live</button>
      </nav>
    </header>

    <main class="content">
      <ManageTab v-if="tab === 'manage'" />

      <template v-else>
        <div v-if="error" class="error">{{ error }}</div>

        <div v-else-if="liveCameras.length === 0 && !loading" class="empty">
          No cameras are recording yet. Add one from the Cameras tab.
        </div>

        <div v-else class="camera-grid">
          <CameraCard
            v-for="(camera, index) in liveCameras"
            :key="camera.id"
            :camera="camera"
            :index="index"
            :total="liveCameras.length"
            @click="selectedCamera = camera"
          />
        </div>
      </template>
    </main>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import CameraCard from './components/CameraCard.vue'
import CameraView from './components/CameraView.vue'
import ManageTab from './components/ManageTab.vue'

const tab            = ref('manage')
const liveCameras    = ref([])
const loading        = ref(true)
const error          = ref(null)
const selectedCamera = ref(null)

let pollId = null

async function loadLive() {
  try {
    const res = await fetch('/cameras')
    if (!res.ok) throw new Error(`Server returned ${res.status}`)
    liveCameras.value = ((await res.json()).cameras ?? []).filter(c => c.state === 'assigned')
    error.value = null
  } catch (e) {
    error.value = `Could not reach Revere: ${e.message}`
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  loadLive()
  // Keep the live grid current as cameras are added/removed from the Cameras tab.
  pollId = setInterval(loadLive, 5000)
})

onUnmounted(() => clearInterval(pollId))
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
  gap: 1.5rem;
  padding: 0.75rem 1.5rem;
  background: #1a1a1a;
  border-bottom: 1px solid #2a2a2a;
}

.header h1 {
  font-size: 1.25rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.tabs {
  display: flex;
  gap: 0.25rem;
}

.tabs button {
  background: none;
  border: none;
  color: #888;
  padding: 0.4rem 0.9rem;
  font-size: 0.9rem;
  border-radius: 4px;
  cursor: pointer;
}

.tabs button:hover { color: #ccc; }

.tabs button.active {
  color: #fff;
  background: #2a2a2a;
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
