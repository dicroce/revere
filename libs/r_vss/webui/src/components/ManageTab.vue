<template>
  <div class="manage">
    <div v-if="error" class="error">{{ error }}</div>

    <div v-if="authed" class="toolbar">
      <button class="primary small" @click="openAddRtsp">+ Add RTSP Camera</button>
    </div>

    <div class="lists">
      <!-- discovered -->
      <section>
        <h2>Discovered <span class="count">{{ discovered.length }}</span></h2>
        <p v-if="discovered.length === 0" class="empty">No new cameras discovered.</p>
        <ul v-else class="cam-list">
          <li v-for="c in discovered" :key="c.id">
            <span class="dot new"></span>
            <span class="cam-name">{{ c.camera_name || c.id }}</span>
            <span class="cam-ip">{{ c.ipv4 }}</span>
            <button
              class="primary small"
              :disabled="!authed"
              :title="authed ? 'Configure this camera for recording' : 'Sign in to record'"
              @click="startRecord(c)"
            >Record</button>
            <button
              class="ghost small"
              :disabled="!authed || forgetting === c.id"
              :title="authed ? 'Forget this camera' : 'Sign in to manage'"
              @click="doForget(c)"
            >{{ forgetting === c.id ? '…' : 'Forget' }}</button>
          </li>
        </ul>
      </section>

      <!-- recording -->
      <section>
        <h2>Recording <span class="count">{{ recording.length }}</span></h2>
        <p v-if="recording.length === 0" class="empty">No cameras are recording yet.</p>
        <ul v-else class="cam-list">
          <li v-for="c in recording" :key="c.id">
            <span
              class="dot"
              :class="c.receiving_video ? 'rec-ok' : 'rec-bad'"
              :title="c.receiving_video ? 'Connected' : (c.stream_failed ? 'Stream failed' : 'Not connected')"
            ></span>
            <span class="cam-name">{{ c.friendly_name || c.camera_name || c.id }}</span>
            <span class="cam-ip">{{ c.ipv4 }}</span>
            <span class="cam-codec">{{ (c.video_codec || '').toUpperCase() }}</span>
            <span
              v-if="c.manufacturer || c.model || c.firmware_version"
              class="cam-model"
            >{{ [c.manufacturer, c.model].filter(Boolean).join(' ') }}{{ c.firmware_version ? ' · fw ' + c.firmware_version : '' }}</span>
            <button
              class="ghost small"
              :disabled="!authed"
              :title="authed ? 'Edit camera settings' : 'Sign in to manage'"
              @click="askProperties(c)"
            >Properties</button>
            <button
              class="ghost small danger"
              :disabled="!authed"
              :title="authed ? 'Stop recording and remove' : 'Sign in to manage'"
              @click="askRemove(c)"
            >Remove</button>
            <!-- An address change: held (confirm on the recorder) or followed. -->
            <p
              v-if="c.camera_move"
              class="cam-move"
              :class="c.camera_move.status === 'held' ? 'held' : 'followed'"
            >{{ c.camera_move.message }}<template v-if="c.camera_move.status === 'held'"> Choose in the Revere app on this computer.</template></p>
          </li>
        </ul>
      </section>
    </div>

    <PluginsSection :authed="authed" />

    <KeepAwakeSection :authed="authed" />

    <RecordWizard
      v-if="wizardCamera"
      :camera="wizardCamera"
      @close="wizardCamera = null"
      @done="onRecorded"
    />

    <StorageDialog
      v-if="storageCamera"
      :camera="storageCamera"
      :mode="storageMode"
      @close="storageCamera = null"
      @done="onStorageDone"
    />

    <!-- remove confirmation -->
    <div v-if="removeTarget" class="overlay" @mousedown="onBackdropDown" @mouseup.self="backdropPress && cancelRemove()">
      <div class="dialog">
        <header><h3>Remove Camera</h3></header>
        <div class="body">
          <p>Remove camera: <strong>{{ removeTarget.friendly_name || removeTarget.camera_name || removeTarget.id }}</strong></p>
          <p>Do you want to delete the camera’s storage files?</p>
          <p class="muted">This will permanently delete video recordings and motion detection data.</p>
          <label class="check">
            <input type="checkbox" v-model="deleteFiles" :disabled="removeBusy" />
            <span>Delete storage files (.nts, .mdb, .mdnts, .db)</span>
          </label>
          <p v-if="removeBusy" class="muted">Removing… {{ deleteFiles ? '(deleting files)' : '' }}</p>
          <p v-if="removeError" class="err">{{ removeError }}</p>
        </div>
        <footer>
          <button class="ghost" :disabled="removeBusy" @click="cancelRemove">Cancel</button>
          <span class="spacer"></span>
          <button
            :class="deleteFiles ? 'danger-solid' : 'primary'"
            :disabled="removeBusy"
            @click="confirmRemove"
          >{{ deleteFiles ? 'Remove & Delete Files' : 'Remove (Keep Files)' }}</button>
        </footer>
      </div>
    </div>

    <!-- properties -->
    <div v-if="propsTarget" class="overlay" @mousedown="onBackdropDown" @mouseup.self="backdropPress && cancelProps()">
      <div class="dialog props">
        <header>
          <h3>Camera Properties</h3>
          <p class="cam-title">{{ propsTarget.friendly_name || propsTarget.camera_name || propsTarget.id }}</p>
        </header>
        <div class="body">
          <label class="check option">
            <input type="checkbox" v-model="propsMotion" :disabled="propsBusy" />
            <span>
              <span class="title">Motion detection</span>
              <span class="desc">Finds movement in this camera's video and marks it as motion events you can search and review.</span>
            </span>
          </label>

          <fieldset class="tiers" :class="{ off: !propsMotion }" :disabled="propsBusy || !propsMotion">
            <legend>Motion and objects from</legend>
            <label v-for="t in tierChoices" :key="t.value" class="check option">
              <input type="radio" name="motion_tier" :value="t.value" v-model="propsTier" />
              <span>
                <span class="title">{{ t.title }}</span>
                <span class="desc">{{ t.description }}</span>
              </span>
            </label>
            <p v-if="tierStatus" class="blurb">{{ tierStatus }}</p>
          </fieldset>

          <label class="check option" :class="{ off: !propsMotion }">
            <input type="checkbox" v-model="propsPrune" :disabled="propsBusy || !propsMotion" />
            <span>
              <span class="title">Keep only motion in older video</span>
              <span class="desc">Video older than the time below is deleted unless it has motion in it, so motion is kept for longer in the same space.</span>
            </span>
          </label>
          <label class="field indent" :class="{ off: !propsPrune }">
            <span>Keep continuous video for at least</span>
            <input type="number" min="1" max="999999" step="1" v-model.number="propsHours" :disabled="propsBusy || !propsPrune" />
            <span>hours</span>
          </label>
          <p v-if="propsPrune && !hoursOk" class="err">Enter a whole number of hours, 1 or more.</p>

          <h4 class="section">Storage</h4>
          <div class="loc">
            <span class="title">Recording file</span>
            <div class="path-row">
              <code>{{ propsTarget.record_file_path || '—' }}</code>
              <button
                v-if="propsTarget.record_file_path"
                class="ghost small"
                @click="copyPath(propsTarget.record_file_path)"
              >{{ copied ? 'Copied' : 'Copy path' }}</button>
            </div>
          </div>
          <div class="storage-actions">
            <button class="ghost small" :disabled="propsBusy" @click="openStorage('move')">Move storage…</button>
            <button class="ghost small" :disabled="propsBusy" @click="openStorage('retention')">Change retention…</button>
          </div>

          <p v-if="propsBusy" class="muted">Saving… (restarting camera)</p>
          <p v-if="propsError" class="err">{{ propsError }}</p>
        </div>
        <footer>
          <button class="ghost" :disabled="propsBusy" @click="cancelProps">Cancel</button>
          <span class="spacer"></span>
          <button class="primary" :disabled="propsBusy || (propsPrune && !hoursOk)" @click="confirmProps">Ok</button>
        </footer>
      </div>
    </div>

    <!-- add RTSP source camera -->
    <div v-if="addOpen" class="overlay" @mousedown="onBackdropDown" @mouseup.self="backdropPress && cancelAdd()">
      <div class="dialog">
        <header><h3>Add RTSP Source Camera</h3></header>
        <div class="body">
          <p class="blurb">RTSP Source Cameras in the Revere system are cameras that are not discoverable but do have an RTSP interface. Please configure your network to not change the IP addresses of these cameras.</p>
          <label class="field-row"><span>Camera Model</span><input v-model="addFields.camera_name" /></label>
          <label class="field-row"><span>IPv4</span><input v-model="addFields.ipv4" placeholder="192.168.0.x" /></label>
          <label class="field-row"><span>RTSP URL</span><input v-model="addFields.rtsp_url" placeholder="rtsp://…" @keyup.enter="confirmAdd" /></label>
          <label class="field-row"><span>RTSP Username</span><input v-model="addFields.rtsp_username" autocomplete="off" /></label>
          <label class="field-row"><span>RTSP Password</span><input type="password" v-model="addFields.rtsp_password" /></label>
          <p v-if="addBusy" class="muted">Adding…</p>
          <p v-if="addError" class="err">{{ addError }}</p>
        </div>
        <footer>
          <button class="ghost" :disabled="addBusy" @click="cancelAdd">Cancel</button>
          <span class="spacer"></span>
          <button class="primary" :disabled="addBusy || !addFields.rtsp_url" @click="confirmAdd">Ok</button>
        </footer>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import {
  isAuthed, getCameras,
  removeCamera, updateCameraProperties, forgetCamera, addRtspCamera,
} from '../api.js'
import KeepAwakeSection from './KeepAwakeSection.vue'
import RecordWizard from './RecordWizard.vue'
import StorageDialog from './StorageDialog.vue'
import PluginsSection from './PluginsSection.vue'

const cameras = ref([])
const error = ref(null)

// Backdrop-dismiss helper: only close a dialog when the press AND release both
// land on the overlay itself, so a drag that starts inside the dialog (e.g.
// selecting text) and releases outside doesn't close it.
const backdropPress = ref(false)
function onBackdropDown(e) { backdropPress.value = e.target === e.currentTarget }

const wizardCamera = ref(null)
const forgetting = ref(null)

const removeTarget = ref(null)
const deleteFiles = ref(false)
const removeBusy = ref(false)
const removeError = ref('')

const propsTarget = ref(null)
const propsMotion = ref(true)
const propsPrune = ref(false)
const propsHours = ref(24)
const propsTier = ref('auto')
const propsBusy = ref(false)
const propsError = ref('')
const copied = ref(false)

// Move storage / Change retention, opened from Camera Properties (which they
// replace, as on the desktop).
const storageCamera = ref(null)
const storageMode = ref('move')

function openStorage(mode) {
  storageMode.value = mode
  storageCamera.value = propsTarget.value
  propsTarget.value = null
}

function onStorageDone() {
  storageCamera.value = null
  refresh()
}

// Where motion and object classes come from (r_vss::r_tier_controller), worded
// as in the desktop's Camera properties dialog.
const tierChoices = [
  { value: 'auto', title: 'Automatic', description: "The camera's own detection once Revere has checked it reports what Revere sees; Revere's otherwise." },
  { value: '1', title: 'Revere', description: 'Revere finds motion and objects in the video. Uses the most CPU.' },
  { value: '2', title: 'Camera', description: "The camera's motion and its person, vehicle and animal events. Uses the least CPU." },
  { value: '3', title: 'Camera motion, Revere objects', description: "The camera's motion events; Revere finds the objects in them." },
]
const tierNames = ['', "Revere's detection", "the camera's detection", "the camera's motion and Revere's objects"]

// What the camera uses now and why, from /cameras' motion_tier status (live,
// so it follows tier changes while the dialog is open).
const tierStatus = computed(() => {
  if (!propsTarget.value) return ''
  const live = cameras.value.find((c) => c.id === propsTarget.value.id) || propsTarget.value
  const mt = live.motion_tier
  if (!mt || !(mt.tier >= 1 && mt.tier <= 3)) return ''
  return `Using ${tierNames[mt.tier]}: ${mt.reason}.`
})

const hoursOk = computed(() => Number.isInteger(propsHours.value) && propsHours.value >= 1 && propsHours.value <= 999999)

// Pruning keeps only motion, so it needs motion detection (as on the desktop).
watch(propsMotion, (on) => { if (!on) propsPrune.value = false })

const addOpen = ref(false)
const addBusy = ref(false)
const addError = ref('')
const addFields = ref({ camera_name: '', ipv4: '', rtsp_url: '', rtsp_username: '', rtsp_password: '' })

const authed = computed(() => isAuthed())
const recording = computed(() => cameras.value.filter((c) => c.state === 'assigned'))
const discovered = computed(() => cameras.value.filter((c) => c.state === 'discovered'))

let pollId = null

async function refresh() {
  try {
    cameras.value = await getCameras()
    error.value = null
  } catch (e) {
    error.value = `Could not reach Revere: ${e.message}`
  }
}

function startRecord(camera) {
  if (!authed.value) return
  wizardCamera.value = camera
}

async function doForget(camera) {
  if (!authed.value || forgetting.value) return
  forgetting.value = camera.id
  try {
    await forgetCamera(camera.id)
    await refresh()
  } catch (e) {
    error.value = e.message
  } finally {
    forgetting.value = null
  }
}

function openAddRtsp() {
  addFields.value = { camera_name: '', ipv4: '', rtsp_url: '', rtsp_username: '', rtsp_password: '' }
  addError.value = ''
  addOpen.value = true
}

function cancelAdd() {
  if (addBusy.value) return
  addOpen.value = false
}

async function confirmAdd() {
  if (!addFields.value.rtsp_url) return
  addBusy.value = true
  addError.value = ''
  try {
    await addRtspCamera({ ...addFields.value })
    addOpen.value = false
    await refresh()
  } catch (e) {
    addError.value = e.message
  } finally {
    addBusy.value = false
  }
}

function onRecorded() {
  wizardCamera.value = null
  refresh()
}

function askRemove(camera) {
  if (!authed.value) return
  removeTarget.value = camera
  deleteFiles.value = false
  removeError.value = ''
}

function cancelRemove() {
  if (removeBusy.value) return
  removeTarget.value = null
}

async function confirmRemove() {
  removeBusy.value = true
  removeError.value = ''
  try {
    await removeCamera(removeTarget.value.id, deleteFiles.value)
    removeTarget.value = null
    await refresh()
  } catch (e) {
    removeError.value = e.message
  } finally {
    removeBusy.value = false
  }
}

function askProperties(camera) {
  if (!authed.value) return
  propsTarget.value = camera
  propsMotion.value = !!camera.do_motion_detection
  propsPrune.value = !!camera.do_motion_pruning
  propsHours.value = camera.min_continuous_recording_hours ?? 24
  propsTier.value = camera.motion_tier_setting || 'auto'
  propsError.value = ''
  copied.value = false
}

// navigator.clipboard needs a secure context (https or localhost). The web UI
// is served over plain http on 127.0.0.1, which counts as secure, but keep the
// execCommand fallback for browsers/contexts where it is still unavailable.
async function copyPath(text) {
  try {
    await navigator.clipboard.writeText(text)
  } catch {
    const ta = document.createElement('textarea')
    ta.value = text
    ta.style.position = 'fixed'
    ta.style.opacity = '0'
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
  }
  copied.value = true
  setTimeout(() => { copied.value = false }, 1500)
}

function cancelProps() {
  if (propsBusy.value) return
  propsTarget.value = null
}

async function confirmProps() {
  propsBusy.value = true
  propsError.value = ''
  try {
    await updateCameraProperties(propsTarget.value.id, {
      do_motion_detection: propsMotion.value,
      do_motion_pruning: propsPrune.value,
      min_continuous_recording_hours: hoursOk.value ? propsHours.value : (propsTarget.value.min_continuous_recording_hours ?? 24),
      motion_tier: propsTier.value,
    })
    propsTarget.value = null
    await refresh()
  } catch (e) {
    propsError.value = e.message
  } finally {
    propsBusy.value = false
  }
}

onMounted(() => {
  refresh()
  // New cameras appear via discovery; assigned cameras leave the discovered
  // list once recording starts — poll so the lists stay live.
  pollId = setInterval(refresh, 5000)
})

onUnmounted(() => clearInterval(pollId))
</script>

<style scoped>
.manage { display: flex; flex-direction: column; gap: 1.5rem; }

.toolbar { display: flex; }
.field-row { display: flex; align-items: center; gap: 0.75rem; }
.field-row span { width: 130px; flex: none; color: #bbb; font-size: 0.85rem; }
.field-row input {
  flex: 1;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  color: #eee;
  padding: 0.4rem 0.5rem;
  font-size: 0.9rem;
}

.lists {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1.5rem;
  align-items: start;
}

@media (max-width: 700px) {
  .lists { grid-template-columns: 1fr; }
}

.muted { color: #888; }

section h2 {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.6rem;
  display: flex;
  align-items: center;
  gap: 0.5rem;
}
.count {
  font-size: 0.75rem;
  color: #888;
  background: #222;
  border-radius: 10px;
  padding: 0.05rem 0.5rem;
}
.cam-list { list-style: none; display: flex; flex-direction: column; gap: 0.4rem; }
.cam-list li {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 0.75rem;
  padding: 0.55rem 0.75rem;
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 6px;
}
.dot { width: 8px; height: 8px; border-radius: 50%; flex: none; }
.dot.rec-ok { background: #4caf72; }
.dot.rec-bad { background: #e05555; }
.dot.new { background: #2d6cdf; }
.cam-name { font-weight: 500; }
.cam-ip { color: #888; font-size: 0.85rem; }
.cam-codec { color: #666; font-size: 0.75rem; }
.cam-move { flex-basis: 100%; margin: 0.25rem 0 0; font-size: 0.85rem; line-height: 1.4; }
.cam-move.held { color: #e0a84a; }
.cam-move.followed { color: #999; }
.cam-list li > .primary, .cam-list li > .cam-codec { margin-left: auto; }
.cam-list li > .primary { margin-left: auto; }

.empty { color: #888; font-size: 0.9rem; }
.error { color: #e05555; font-size: 0.9rem; }

button.primary {
  background: #2d6cdf;
  border: none;
  color: #fff;
  border-radius: 4px;
  cursor: pointer;
}
button.primary:disabled { background: #333; color: #777; cursor: default; }
button.ghost {
  background: none;
  border: 1px solid #333;
  color: #ccc;
  border-radius: 4px;
  cursor: pointer;
}
button.small { padding: 0.35rem 0.75rem; font-size: 0.8rem; }
button.ghost.danger { border-color: #5a2a2a; color: #e08585; }
button.ghost.danger:hover:not(:disabled) { border-color: #e05555; color: #f0a0a0; }
button.danger-solid {
  background: #b43232;
  border: none;
  color: #fff;
  padding: 0.5rem 1rem;
  border-radius: 4px;
  font-size: 0.9rem;
  cursor: pointer;
}
button.danger-solid:hover:not(:disabled) { background: #c83c3c; }
button:disabled { cursor: default; opacity: 0.6; }

/* remove confirmation dialog */
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}
.dialog {
  width: 440px;
  max-width: calc(100vw - 2rem);
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 8px;
}
.dialog header { padding: 0.85rem 1.1rem; border-bottom: 1px solid #363636; }
.dialog header h3 { font-size: 1rem; font-weight: 600; }
.dialog .body { padding: 1.1rem; display: flex; flex-direction: column; gap: 0.6rem; }
.dialog .check { display: flex; align-items: center; gap: 0.5rem; cursor: pointer; }
.dialog .field { display: flex; align-items: center; gap: 0.6rem; }
.dialog .field input {
  width: 90px;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  color: #eee;
  padding: 0.4rem 0.5rem;
  font-size: 0.9rem;
}
.dialog .loc { font-size: 0.8rem; word-break: break-all; }
.dialog .loc span { color: #aaa; }
.dialog .err { color: #e05555; font-size: 0.85rem; }
.dialog .blurb { font-size: 0.85rem; color: #999; line-height: 1.45; }
.dialog footer {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  padding: 0.85rem 1.1rem;
  border-top: 1px solid #363636;
}
.dialog .spacer { flex: 1; }
/* style.css zeroes padding and only .small sets it, so footer buttons were bare text */
.dialog footer button:not(.small) { padding: 0.45rem 1rem; font-size: 0.9rem; }

/* camera properties */
.dialog.props { width: 480px; max-height: calc(100vh - 2rem); display: flex; flex-direction: column; }
.dialog.props .body { overflow-y: auto; gap: 0.8rem; }
.dialog .cam-title { font-size: 0.85rem; color: #aaa; margin-top: 0.2rem; }
.dialog .option { align-items: flex-start; }
.dialog .option input { margin-top: 0.2rem; flex: none; }
.dialog .option .title { display: block; font-size: 0.9rem; }
.dialog .option .desc { display: block; font-size: 0.8rem; color: #999; line-height: 1.4; margin-top: 0.1rem; }
.dialog .tiers {
  border: none;
  margin: 0 0 0 1.5rem;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}
.dialog .tiers legend { font-size: 0.8rem; color: #bbb; margin-bottom: 0.4rem; padding: 0; }
.dialog .off { opacity: 0.5; }
.dialog .off, .dialog .off .option { cursor: default; }
.dialog .field.indent { margin-left: 1.5rem; font-size: 0.85rem; color: #bbb; flex-wrap: wrap; }
.dialog .section {
  font-size: 0.8rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  color: #888;
  border-top: 1px solid #363636;
  padding-top: 0.8rem;
  margin-top: 0.2rem;
}
.dialog .loc .title { display: block; font-size: 0.85rem; color: #bbb; margin-bottom: 0.3rem; }
.dialog .storage-actions { display: flex; gap: 0.6rem; flex-wrap: wrap; }
.dialog .path-row { display: flex; align-items: center; gap: 0.6rem; }
.dialog .path-row code {
  flex: 1;
  min-width: 0;
  font-size: 0.8rem;
  color: #aaa;
  word-break: break-all;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  padding: 0.35rem 0.5rem;
}
</style>
