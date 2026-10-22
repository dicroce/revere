<template>
  <div class="overlay" @mousedown="onBackdropDown" @mouseup.self="backdropPress && close()">
    <div class="dialog">
      <header>
        <h3>{{ mode === 'move' ? 'Move storage' : 'Change retention' }}</h3>
        <p class="cam-title">{{ cameraName }}</p>
      </header>

      <!-- choosing -->
      <div v-if="state === 'form'" class="body">
        <template v-if="mode === 'move'">
          <label class="label" for="move-dir">Move to</label>
          <input id="move-dir" v-model="newDir" type="text" placeholder="/mnt/storage/revere" spellcheck="false" />
          <p class="hint">A folder on the machine Revere runs on.</p>

          <label class="check option">
            <input type="checkbox" v-model="startFresh" />
            <span>
              <span class="title">Start fresh</span>
              <span class="desc">Don't copy the existing recordings; create empty recording files in the new folder.</span>
            </span>
          </label>
          <template v-if="startFresh">
            <p class="danger">This camera's existing recordings will be permanently deleted.</p>
            <label class="check">
              <input type="checkbox" v-model="deleteConfirmed" />
              <span>Delete the existing recordings</span>
            </label>
          </template>
        </template>

        <template v-else>
          <p v-if="info === null" class="muted">Loading…</p>
          <template v-else-if="info.byte_rate > 0">
            <p class="text">
              Current bitrate: {{ Math.floor((info.byte_rate * 8) / 1024) }} kbps. The recording file is sized for the days of video to keep at this bitrate.
            </p>
            <label class="field">
              <span>Keep video for</span>
              <input v-model.number="days" type="number" min="0.1" step="0.1" />
              <span>days</span>
            </label>
            <p v-if="newSizeText" class="text">New file size: {{ newSizeText }}.</p>
          </template>
          <template v-else>
            <p class="danger">This camera isn't receiving video, so there's no bitrate to size the file from.</p>
            <p class="text">Retention can be changed once the camera is recording.</p>
          </template>

          <p class="danger">Resizing recreates the recording file, which permanently deletes this camera's existing recordings.</p>
          <label class="check">
            <input type="checkbox" v-model="deleteConfirmed" :disabled="!info || !(info.byte_rate > 0)" />
            <span>Delete the existing recordings</span>
          </label>
        </template>

        <p v-if="error" class="err">{{ error }}</p>
      </div>

      <!-- running -->
      <div v-else-if="state === 'running'" class="body">
        <p class="text">{{ runningText }}</p>
        <progress :value="op.progress" max="1"></progress>
        <p v-if="op.status" class="muted">{{ op.status }}</p>
      </div>

      <!-- failed -->
      <div v-else class="body">
        <p class="danger">{{ mode === 'move' ? "Couldn't move the recording files." : "Couldn't resize the recording file." }}</p>
        <p class="muted">{{ op.status }}</p>
      </div>

      <footer>
        <template v-if="state === 'form'">
          <button class="ghost" :disabled="starting" @click="close">Cancel</button>
          <span class="spacer"></span>
          <button
            :class="destructive ? 'danger-solid' : 'primary'"
            :disabled="starting || !canStart"
            @click="start"
          >{{ mode === 'move' ? 'Move' : 'Resize' }}</button>
        </template>
        <template v-else-if="state === 'running'">
          <span class="spacer"></span>
          <button class="ghost" :disabled="!cancellable || cancelling" @click="cancel">{{ cancelling ? 'Cancelling…' : 'Cancel' }}</button>
        </template>
        <template v-else>
          <span class="spacer"></span>
          <button class="primary" @click="close">Close</button>
        </template>
      </footer>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { getStorageInfo, moveStorage, changeRetention, getStorageOp, cancelStorageOp } from '../api.js'

// mode: 'move' (Move storage, including Start fresh) or 'retention'.
const props = defineProps({
  camera: { type: Object, required: true },
  mode: { type: String, required: true },
})
const emit = defineEmits(['close', 'done'])

const cameraName = computed(() => props.camera.friendly_name || props.camera.camera_name || props.camera.id)

const state = ref('form')       // form | running | failed
const error = ref('')
const starting = ref(false)
const cancelling = ref(false)
const op = ref({ progress: 0, status: '' })

const newDir = ref('')
const startFresh = ref(false)
const deleteConfirmed = ref(false)

const info = ref(null)
const days = ref(1)
const newSizeText = ref('')

const destructive = computed(() => props.mode === 'retention' || startFresh.value)
const canStart = computed(() => {
  if (props.mode === 'move')
    return newDir.value.trim() !== '' && (!startFresh.value || deleteConfirmed.value)
  return !!info.value && info.value.byte_rate > 0 && days.value > 0 && deleteConfirmed.value
})
const runningText = computed(() => {
  if (props.mode === 'retention') return 'Resizing the recording file...'
  return startFresh.value ? 'Creating new recording files...' : 'Moving the recording files...'
})
// Only a move's copy to another drive can stop part way.
const cancellable = computed(() => props.mode === 'move' && !startFresh.value)

// Backdrop-dismiss only when the press and release both land on the overlay,
// and never while an operation runs.
const backdropPress = ref(false)
function onBackdropDown(e) { backdropPress.value = e.target === e.currentTarget }

function close() {
  if (state.value === 'running' || starting.value) return
  emit('close')
}

// The file size the chosen days would give, as the desktop shows it.
let sizeTimer = null
watch(days, (d) => {
  clearTimeout(sizeTimer)
  newSizeText.value = ''
  if (!(d > 0) || !info.value || !(info.value.byte_rate > 0)) return
  sizeTimer = setTimeout(async () => {
    try {
      newSizeText.value = (await getStorageInfo(props.camera.id, d)).new_file_size_text || ''
    } catch { /* the preview is only informative */ }
  }, 250)
})

let pollId = null
async function poll() {
  try {
    const s = await getStorageOp()
    op.value = s
    if (s.running) return
    clearInterval(pollId)
    pollId = null
    if (s.error) state.value = 'failed'
    else emit('done')
  } catch (e) {
    // Keep polling: the operation runs on regardless of a dropped request.
    op.value = { ...op.value, status: e.message }
  }
}

async function start() {
  if (!canStart.value) return
  starting.value = true
  error.value = ''
  try {
    if (props.mode === 'move')
      await moveStorage(props.camera.id, newDir.value.trim(), startFresh.value, deleteConfirmed.value)
    else
      await changeRetention(props.camera.id, days.value, deleteConfirmed.value)
    op.value = { progress: 0, status: '' }
    state.value = 'running'
    pollId = setInterval(poll, 500)
  } catch (e) {
    error.value = e.message
  } finally {
    starting.value = false
  }
}

async function cancel() {
  cancelling.value = true
  try { await cancelStorageOp() } catch { /* the poll reports the outcome */ }
}

onMounted(async () => {
  if (props.mode !== 'retention') return
  try {
    info.value = await getStorageInfo(props.camera.id)
    // Start from what the file holds now, as the desktop does.
    days.value = info.value.retention_days > 0 ? Math.round(info.value.retention_days * 10) / 10 : 1
  } catch (e) {
    info.value = { byte_rate: 0 }
    error.value = e.message
  }
})

onUnmounted(() => {
  clearInterval(pollId)
  clearTimeout(sizeTimer)
})
</script>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 110;
}
.dialog {
  width: 440px;
  max-width: calc(100vw - 2rem);
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 8px;
}
header { padding: 0.85rem 1.1rem; border-bottom: 1px solid #363636; }
header h3 { font-size: 1rem; font-weight: 600; }
.cam-title { font-size: 0.85rem; color: #aaa; margin-top: 0.2rem; }
.body { padding: 1.1rem; display: flex; flex-direction: column; gap: 0.7rem; }
.label { font-size: 0.85rem; color: #bbb; }
input[type="text"], input[type="number"] {
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  color: #eee;
  padding: 0.45rem 0.55rem;
  font-size: 0.9rem;
}
input[type="number"] { width: 90px; }
.hint, .muted { font-size: 0.8rem; color: #999; }
.text { font-size: 0.85rem; color: #ccc; line-height: 1.45; }
.danger { font-size: 0.85rem; color: #e08585; line-height: 1.45; }
.err { color: #e05555; font-size: 0.85rem; }
.check { display: flex; align-items: center; gap: 0.5rem; cursor: pointer; font-size: 0.9rem; }
.option { align-items: flex-start; }
.option input { margin-top: 0.2rem; flex: none; }
.option .title { display: block; }
.option .desc { display: block; font-size: 0.8rem; color: #999; line-height: 1.4; margin-top: 0.1rem; }
.field { display: flex; align-items: center; gap: 0.6rem; font-size: 0.85rem; color: #bbb; }
progress { width: 100%; height: 8px; }
footer {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  padding: 0.85rem 1.1rem;
  border-top: 1px solid #363636;
}
.spacer { flex: 1; }
button { border-radius: 4px; cursor: pointer; padding: 0.45rem 1rem; font-size: 0.9rem; }
button.primary { background: #2d6cdf; border: none; color: #fff; }
button.primary:disabled { background: #333; color: #777; cursor: default; }
button.ghost { background: none; border: 1px solid #333; color: #ccc; }
button.danger-solid { background: #b43232; border: none; color: #fff; }
button.danger-solid:hover:not(:disabled) { background: #c83c3c; }
button.danger-solid:disabled { background: #333; color: #777; cursor: default; }
button:disabled { cursor: default; opacity: 0.6; }
</style>
