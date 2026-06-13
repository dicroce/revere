<template>
  <div v-if="!authChecked" class="splash">Loading…</div>

  <LoginView
    v-else-if="!authed"
    :password-set="passwordSet"
    @authenticated="onAuthenticated"
  />

  <CameraView
    v-else-if="selectedCamera"
    :camera="selectedCamera"
    @close="selectedCamera = null"
  />

  <div v-else class="app">
    <header class="header">
      <img src="/logos/revere-lockup-white.svg" alt="Revere" class="logo" />
      <nav class="tabs">
        <button :class="{ active: tab === 'manage' }" @click="tab = 'manage'">Config</button>
        <button :class="{ active: tab === 'live' }" @click="tab = 'live'">Live</button>
      </nav>
      <span class="spacer"></span>
      <button class="signout" @click="signOut">Sign out</button>
    </header>

    <main class="content">
      <ManageTab v-if="tab === 'manage'" />

      <template v-else>
        <div v-if="error" class="error">{{ error }}</div>
        <div v-else-if="liveCameras.length === 0" class="empty">
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
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { isAuthed, getAuthStatus, getCameras, logout } from './api.js'
import CameraCard from './components/CameraCard.vue'
import CameraView from './components/CameraView.vue'
import ManageTab from './components/ManageTab.vue'
import LoginView from './components/LoginView.vue'

const authChecked = ref(false)
const passwordSet = ref(true)
const authed = computed(() => isAuthed())

const tab = ref('manage')
const liveCameras = ref([])
const error = ref(null)
const selectedCamera = ref(null)

let pollId = null

async function loadLive() {
  if (!authed.value) return
  try {
    const cams = await getCameras() // apiFetch attaches the token; clears it on 401
    liveCameras.value = cams.filter((c) => c.state === 'assigned')
    error.value = null
  } catch (e) {
    // A 401 clears the token (api.js) -> the gate flips back to the login screen.
    error.value = `Could not reach Revere: ${e.message}`
  }
}

function onAuthenticated() {
  passwordSet.value = true
  loadLive()
}

function signOut() {
  logout()
  selectedCamera.value = null
  liveCameras.value = []
  tab.value = 'manage'
}

onMounted(async () => {
  try {
    passwordSet.value = !!(await getAuthStatus()).password_set
  } catch {
    passwordSet.value = true
  }
  // Verify a stored token (if any) before showing the app.
  if (authed.value) {
    try { await getCameras() } catch { /* invalid token cleared by api.js */ }
  }
  authChecked.value = true

  if (authed.value) loadLive()
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
  background: #1e1e1e;
  border-bottom: 1px solid #363636;
}

.header .logo {
  height: 26px;
  display: block;
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
.tabs button.active { color: #fff; background: #363636; }

.header .spacer { flex: 1; }

.signout {
  background: none;
  border: 1px solid #333;
  color: #ccc;
  padding: 0.35rem 0.8rem;
  font-size: 0.8rem;
  border-radius: 4px;
  cursor: pointer;
}
.signout:hover { color: #fff; border-color: #4a4a4a; }

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

.error { color: #e05555; }

.splash {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #888;
  background: #111;
}
</style>
