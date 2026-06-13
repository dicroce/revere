<template>
  <div class="overlay" @mousedown="onBackdropDown" @mouseup.self="backdropPress && tryClose()">
    <div class="wizard">
      <header>
        <h3>Set up “{{ camera.camera_name || camera.id }}”</h3>
        <button class="x" @click="tryClose" aria-label="Close">×</button>
      </header>

      <div class="body">
        <!-- 1. credentials -->
        <div v-if="step === 'credentials'" class="step">
          <p>Enter the camera’s ONVIF / RTSP credentials.</p>
          <input v-model="username" placeholder="Username" autocomplete="off" />
          <input v-model="password" type="password" placeholder="Password" @keyup.enter="next" />
        </div>

        <!-- 2. profile selection -->
        <div v-else-if="step === 'profile'" class="step">
          <p>This camera offers several stream profiles. Choose one:</p>
          <label v-for="p in profiles" :key="p.token" class="radio">
            <input type="radio" :value="p.token" v-model="selectedProfile" />
            <span>{{ p.encoding }} — {{ p.width }}×{{ p.height }}</span>
          </label>
        </div>

        <!-- 3. measuring -->
        <div v-else-if="step === 'measuring'" class="step center">
          <p>Measuring bitrate and capturing a snapshot…</p>
          <progress :value="progress" max="100"></progress>
          <p class="muted">{{ progress }}%</p>
        </div>

        <!-- 4. friendly name + snapshot -->
        <div v-else-if="step === 'name'" class="step">
          <img v-if="snapshot" :src="snapshot" class="snap" alt="camera snapshot" />
          <div v-else class="snap snap-empty">No snapshot available</div>
          <p>Give this camera a name:</p>
          <input v-model="friendlyName" placeholder="e.g. Front Door" @keyup.enter="next" />
        </div>

        <!-- 5. options -->
        <div v-else-if="step === 'options'" class="step">
          <label class="check">
            <input type="checkbox" v-model="motion" />
            <span>Enable motion detection</span>
          </label>
          <label class="field">
            <span>Retention (days)</span>
            <input type="number" min="1" max="365" v-model.number="retentionDays" />
          </label>
          <p class="muted">≈ {{ estStorage }} at {{ kbps }} kbps</p>
        </div>

        <!-- 6. configuring -->
        <div v-else-if="step === 'configuring'" class="step center">
          <p>Configuring camera…</p>
          <progress />
        </div>

        <p v-if="error" class="err">{{ error }}</p>
      </div>

      <footer>
        <button v-if="canBack" class="ghost" @click="back" :disabled="busy">Back</button>
        <span class="spacer"></span>
        <button v-if="step === 'error'" class="primary" @click="restart">Start over</button>
        <button
          v-else-if="showNext"
          class="primary"
          :disabled="!canNext || busy"
          @click="next"
        >{{ nextLabel }}</button>
      </footer>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { getCameraProfiles, measureCamera, configureCamera } from '../api.js'

const props = defineProps({ camera: { type: Object, required: true } })
const emit = defineEmits(['close', 'done'])

// A manually-added RTSP camera has no ONVIF profiles to pick and reuses the
// credentials captured when it was added — so skip straight to measuring.
const isManual = computed(() => !!props.camera.manual)

const step = ref('credentials')
const busy = ref(false)
const error = ref('')

// Backdrop-dismiss: only close when the press AND release both land on the
// overlay (so dragging from inside the dialog to outside doesn't close it).
const backdropPress = ref(false)
function onBackdropDown(e) { backdropPress.value = e.target === e.currentTarget }

const username = ref('')
const password = ref('')
const profiles = ref([])
const selectedProfile = ref('')
const progress = ref(0)
const byteRate = ref(0)
const videoCodec = ref('')
const snapshot = ref('')
const friendlyName = ref('')
const motion = ref(true)
const retentionDays = ref(3)

const kbps = computed(() => Math.round((byteRate.value * 8) / 1000))
const estStorage = computed(() => {
  const bytes = byteRate.value * 86400 * retentionDays.value
  if (bytes <= 0) return '—'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let v = bytes, i = 0
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++ }
  return `${v.toFixed(1)} ${units[i]}`
})

const canBack = computed(() => {
  if (step.value === 'name') return !isManual.value   // manual has no earlier step
  return ['profile', 'options'].includes(step.value)
})
const showNext = computed(() => ['credentials', 'profile', 'name', 'options'].includes(step.value))
const nextLabel = computed(() => (step.value === 'options' ? 'Start recording' : 'Next'))
const canNext = computed(() => {
  switch (step.value) {
    case 'credentials': return username.value.length > 0
    case 'profile': return !!selectedProfile.value
    case 'name': return friendlyName.value.trim().length > 0
    case 'options': return retentionDays.value >= 1
    default: return false
  }
})

function tryClose() { if (!busy.value) emit('close') }

function back() {
  error.value = ''
  if (step.value === 'profile') step.value = 'credentials'
  else if (step.value === 'name') step.value = profiles.value.length > 1 ? 'profile' : 'credentials'
  else if (step.value === 'options') step.value = 'name'
}

function restart() {
  error.value = ''
  progress.value = 0
  profiles.value = []
  selectedProfile.value = ''
  if (isManual.value) startMeasure()
  else step.value = 'credentials'
}

onMounted(() => {
  if (isManual.value) startMeasure()
})

async function next() {
  if (busy.value || !canNext.value) return
  error.value = ''
  if (step.value === 'credentials') return fetchProfiles()
  if (step.value === 'profile') return startMeasure()
  if (step.value === 'name') { step.value = 'options'; return }
  if (step.value === 'options') return configure()
}

async function fetchProfiles() {
  busy.value = true
  try {
    const list = await getCameraProfiles(props.camera.id, username.value, password.value)
    if (list.length === 0) throw new Error('Camera offered no recordable (H.264/H.265) profiles.')
    profiles.value = list
    if (list.length === 1) {
      selectedProfile.value = list[0].token
      await startMeasure()
    } else {
      step.value = 'profile'
    }
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = false
  }
}

async function startMeasure() {
  busy.value = true
  progress.value = 0
  step.value = 'measuring'
  try {
    const r = await measureCamera(
      props.camera.id, username.value, password.value, selectedProfile.value,
      (p) => { progress.value = p }
    )
    byteRate.value = r.byte_rate || 0
    videoCodec.value = r.video_codec || ''
    snapshot.value = r.jpeg_b64 ? `data:image/jpeg;base64,${r.jpeg_b64}` : ''
    if (!friendlyName.value) friendlyName.value = props.camera.camera_name || ''
    step.value = 'name'
  } catch (e) {
    error.value = e.message
    step.value = 'error'
  } finally {
    busy.value = false
  }
}

async function configure() {
  busy.value = true
  step.value = 'configuring'
  try {
    const payload = {
      camera_id: props.camera.id,
      friendly_name: friendlyName.value.trim(),
      do_motion_detection: motion.value,
      retention_days: retentionDays.value,
      byte_rate: byteRate.value,
    }
    // Only send credentials if the user entered them (ONVIF flow). A manual
    // camera keeps the credentials captured in the Add dialog.
    if (username.value) {
      payload.username = username.value
      payload.password = password.value
    }
    await configureCamera(payload)
    emit('done')
  } catch (e) {
    error.value = e.message
    step.value = 'options'
  } finally {
    busy.value = false
  }
}
</script>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}
.wizard {
  width: 420px;
  max-width: calc(100vw - 2rem);
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 8px;
  display: flex;
  flex-direction: column;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.85rem 1.1rem;
  border-bottom: 1px solid #363636;
}
header h3 { font-size: 1rem; font-weight: 600; }
.x {
  background: none;
  border: none;
  color: #888;
  font-size: 1.4rem;
  line-height: 1;
  cursor: pointer;
}
.x:hover { color: #ddd; }
.body { padding: 1.1rem; min-height: 120px; }
.step { display: flex; flex-direction: column; gap: 0.7rem; }
.step.center { align-items: center; text-align: center; }
.step input[type="text"],
.step input:not([type]),
.step input[type="password"],
.step input[type="number"] {
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  color: #eee;
  padding: 0.5rem 0.6rem;
  font-size: 0.9rem;
}
.radio, .check { display: flex; align-items: center; gap: 0.5rem; cursor: pointer; }
.field { display: flex; align-items: center; justify-content: space-between; gap: 0.7rem; }
.field input { width: 90px; }
.snap {
  width: 100%;
  aspect-ratio: 16 / 9;
  object-fit: contain;
  background: #000;
  border-radius: 4px;
}
.snap-empty { display: flex; align-items: center; justify-content: center; color: #555; }
progress { width: 100%; height: 8px; }
.muted { color: #888; font-size: 0.85rem; }
.err { color: #e05555; font-size: 0.85rem; margin-top: 0.5rem; }
footer {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  padding: 0.85rem 1.1rem;
  border-top: 1px solid #363636;
}
.spacer { flex: 1; }
button.primary {
  background: #2d6cdf;
  border: none;
  color: #fff;
  padding: 0.5rem 1rem;
  border-radius: 4px;
  font-size: 0.9rem;
  cursor: pointer;
}
button.primary:disabled { background: #333; color: #777; cursor: default; }
button.ghost {
  background: none;
  border: 1px solid #333;
  color: #ccc;
  padding: 0.5rem 1rem;
  border-radius: 4px;
  font-size: 0.9rem;
  cursor: pointer;
}
</style>
