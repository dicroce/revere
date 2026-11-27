<template>
  <div class="manage">
    <div v-if="error" class="error">{{ error }}</div>

    <div v-if="authed && !propsTarget" class="toolbar">
      <button class="primary small" @click="openAddRtsp">+ Add RTSP Camera</button>
    </div>

    <div v-if="!propsTarget" class="lists">
      <!-- discovered -->
      <section>
        <h2>Discovered <span class="count">{{ discovered.length }}</span></h2>
        <p v-if="discoveryWarning" class="discovery-warning">{{ discoveryWarning }}</p>
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

    <PluginsSection v-if="!propsTarget" :authed="authed" />

    <KeepAwakeSection v-if="!propsTarget" :authed="authed" />

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

    <!-- properties: edited in place of the lists, as in the desktop app's camera pane -->
    <section v-if="propsTarget" class="props-pane">
      <header class="props-head">
        <button class="ghost small back" :disabled="propsBusy" @click="cancelProps">← Cameras</button>
        <h2>{{ propsTarget.friendly_name || propsTarget.camera_name || propsTarget.id }}</h2>
        <p class="cam-title">Camera properties</p>
      </header>

      <!-- One link per section below, read from the page (so a section added there has its link): stays at the top while
           the sections scroll, and marks the one in view -->
      <nav v-if="propsTabs.length" class="props-tabs" aria-label="Sections">
        <a
          v-for="t in propsTabs"
          :key="t.id"
          :href="'#' + t.id"
          :class="{ on: t.id === propsActiveTab }"
          :aria-current="t.id === propsActiveTab ? 'location' : undefined"
          @click.prevent="goToSection(t.id)"
        >{{ t.title }}</a>
      </nav>

      <div class="props-sections">
        <h4 class="section">Recording</h4>
        <div class="props-section">
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
          <div class="action-row">
            <p class="blurb">Move this camera's recording files to another folder or disk.</p>
            <button class="ghost small" :disabled="propsBusy" @click="openStorage('move')">Move storage…</button>
          </div>
          <div class="action-row">
            <p class="blurb">Resize the recording file to hold more or fewer days of video. Clears this camera's existing recordings.</p>
            <button class="ghost small" :disabled="propsBusy" @click="openStorage('retention')">Change retention…</button>
          </div>
        </div>

        <h4 class="section">Detection</h4>
        <div class="props-section">
          <label class="check option">
            <input type="checkbox" v-model="propsMotion" :disabled="propsBusy" />
            <span>
              <span class="title">Motion detection</span>
              <span class="desc">Finds movement in this camera's video and marks it as motion events you can search and review.</span>
            </span>
          </label>

          <div>
            <fieldset class="tiers" :class="{ off: !propsMotion }" :disabled="propsBusy || !propsMotion">
              <legend>Motion and objects from</legend>
              <div class="pills">
                <label v-for="t in tierChoices" :key="t.value" class="pill" :class="{ on: propsTier === t.value }">
                  <input type="radio" name="motion_tier" :value="t.value" v-model="propsTier" />
                  {{ t.title }}
                </label>
              </div>
              <p class="blurb">{{ tierChoices.find((t) => t.value === propsTier)?.description }}</p>
              <p v-if="tierStatus" class="blurb">{{ tierStatus }}</p>
            </fieldset>
          </div>

          <div class="prune">
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
          </div>
        </div>

        <h4 class="section">Doorbell</h4>
        <div class="props-section">
          <label class="check option">
            <input type="checkbox" v-model="propsDoorbell" :disabled="propsBusy" />
            <span>
              <span class="title">Doorbell</span>
              <span class="desc">Pressing the button saves an event and notifies your phone, even if motion detection is off.</span>
            </span>
          </label>

          <template v-if="propsDoorbell">
            <div>
              <fieldset class="tiers" :disabled="propsBusy">
                <legend>How the doorbell tells Revere about a press</legend>
                <div class="pills">
                  <label class="pill" :class="{ on: propsPressSource === 'onvif' }">
                    <input type="radio" name="press_source" value="onvif" v-model="propsPressSource" />ONVIF
                  </label>
                  <label class="pill" :class="{ on: propsPressSource === 'reolink_email' }">
                    <input type="radio" name="press_source" value="reolink_email" v-model="propsPressSource" />Reolink email
                  </label>
                </div>
                <p v-if="propsPressSource === 'onvif'" class="blurb">The doorbell sends each press to Revere over ONVIF, right away. Most doorbells support this.</p>
                <p v-else class="blurb">For Reolink doorbells that don't send presses over ONVIF. The doorbell emails each press to this computer instead. The email stays on your network.</p>
                <p v-if="doorPresses?.suggest_onvif && propsPressSource === 'reolink_email'" class="warn">This doorbell now sends presses over ONVIF too. Switch to ONVIF so rings arrive right away.</p>
              </fieldset>
            </div>

            <div v-if="propsPressSource === 'reolink_email'" class="mail-setup">
              <span class="title">In the Reolink app</span>
              <p class="blurb">Open Settings › Email Alerts › Email Settings and enter:</p>
              <p v-if="!doorMail" class="blurb">{{ doorMailError || 'Loading the email settings…' }}</p>
              <dl v-else class="mail-fields">
                <template v-for="f in mailFields" :key="f.key">
                  <dt>{{ f.name }}</dt>
                  <dd>
                    <code>{{ f.secret ? passwordShown(f.value, doorReveal) : f.value }}</code>
                    <span class="mail-buttons">
                      <button v-if="f.secret" class="ghost small" @click="doorReveal = !doorReveal">{{ doorReveal ? 'Hide' : 'Show' }}</button>
                      <button v-if="f.secret" class="ghost small" :disabled="propsBusy || doorNewPassword" @click="doorConfirmNew = true">New password</button>
                      <button v-if="f.copy" class="ghost small" @click="copyField(f)">{{ copiedField === f.key ? 'Copied' : 'Copy' }}</button>
                    </span>
                  </dd>
                </template>
              </dl>
              <div v-if="doorMail && doorConfirmNew" class="new-password">
                <p class="warn">The doorbell will stop sending presses until you enter the new password in the Reolink app.</p>
                <button class="small" :disabled="doorNewPassword" @click="newPassword">Make a new password</button>
                <button class="ghost small" :disabled="doorNewPassword" @click="doorConfirmNew = false">Cancel</button>
              </div>
              <p v-if="doorMail" class="blurb">Then set the email Alarm Schedule to Ring only. Otherwise the doorbell also emails when it sees motion.</p>
              <p v-if="doorMail && doorMailError" class="warn">{{ doorMailError }}</p>
              <p class="blurb">The doorbell sends its email about 15 seconds after the press, so the ring arrives late.</p>
              <p class="blurb">The doorbell sends at most one email about every 10 minutes, and has no setting to change that. A second press in that time doesn't reach Revere.</p>
              <p v-for="line in mailLines" :key="line" class="blurb">{{ line }}</p>
            </div>

            <div class="press-check">
              <span class="title">Test it</span>
              <p v-if="doorTest?.open" class="blurb">Press the doorbell now. {{ testSecondsLeft }} {{ testSecondsLeft === 1 ? 'second' : 'seconds' }} left.</p>
              <div v-else class="action-row">
                <p class="blurb">Click Start test, then press the doorbell within a minute. Revere shows how the press arrived and how long it took. Your phone won't ring during the test.</p>
                <button class="ghost small" :disabled="propsBusy || doorTesting" @click="pressTest">Start test</button>
              </div>
              <p v-for="line in testLines" :key="line" class="test-line">{{ line }}</p>
              <p v-if="testVerdict" class="blurb">{{ testVerdict }}</p>
              <p v-if="doorTest && !doorTest.open && doorPresses && doorPresses.press_source !== propsPressSource" class="blurb">The test checks the saved choice. Save first to test the other one.</p>
            </div>
          </template>
        </div>
      </div>

      <p v-if="propsBusy" class="muted">Saving… (restarting camera)</p>
      <p v-if="propsError" class="err">{{ propsError }}</p>

      <footer class="props-foot">
        <template v-if="confirmingDiscard">
          <span class="discard-ask">Discard changes?</span>
          <span class="spacer"></span>
          <button class="ghost" @click="confirmingDiscard = false">Keep editing</button>
          <button class="danger-solid" @click="discardProps">Discard changes</button>
        </template>
        <template v-else>
          <span class="spacer"></span>
          <button class="ghost" :disabled="propsBusy" @click="cancelProps">Cancel</button>
          <button class="primary" :disabled="propsBusy || !propsDirty || (propsPrune && !hoursOk)" @click="confirmProps">Save</button>
        </template>
      </footer>
    </section>

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
import { ref, computed, watch, nextTick, onMounted, onUpdated, onUnmounted } from 'vue'
import {
  isAuthed, getCameras, getDiscoveryStatus,
  removeCamera, updateCameraProperties, forgetCamera, addRtspCamera,
  getDoorbellPresses, getDoorbellMailSetup, startDoorbellPressTest, newDoorbellMailLogin,
} from '../api.js'
import KeepAwakeSection from './KeepAwakeSection.vue'
import RecordWizard from './RecordWizard.vue'
import StorageDialog from './StorageDialog.vue'
import PluginsSection from './PluginsSection.vue'
import { doorbellChoice } from '../utils/cameraKind.js'
import { pressSourceChoice, pressTestLines, pressTestVerdict, mailStatusLines, mailSetupRows, mailSetupAfterRead, passwordShown, mailSetupError } from '../utils/doorbellSetup.js'
import { unsaved } from '../utils/unsaved.js'
import { activeSection, sameTabs, isScrollKey } from '../utils/propsSections.js'

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
// The properties page's row of section links: one per section heading on the page, read from it (readPropsTabs), and
// the one in view (the sticky row is as tall as PROPS_TABS_LEAD says, with a little room).
const propsTabs = ref([])
const propsActiveTab = ref(null)
// The clicked link, kept marked until the user scrolls: a section too near the end of the page to reach the top would
// else lose the mark to its neighbour.
let propsPinned = null
const PROPS_TABS_LEAD = 72
function readPropsTabs() {
  if (!propsTarget.value) {
    if (propsTabs.value.length) propsTabs.value = []
    return
  }
  const found = [...document.querySelectorAll('.props-sections > h4.section')].map((h, i) => {
    h.id = `props-section-${i}`
    return { id: h.id, title: h.textContent.trim() }
  })
  if (!sameTabs(found, propsTabs.value)) propsTabs.value = found
  updateActiveTab()
}
function updateActiveTab() {
  const headings = propsTabs.value
    .map(t => ({ id: t.id, el: document.getElementById(t.id) }))
    .filter(h => h.el)
    .map(h => ({ id: h.id, top: h.el.getBoundingClientRect().top }))
  if (propsPinned && propsTabs.value.some(t => t.id === propsPinned)) {
    propsActiveTab.value = propsPinned
    return
  }
  propsPinned = null
  const atBottom = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 2
  propsActiveTab.value = activeSection(headings, PROPS_TABS_LEAD, atBottom && window.scrollY > 0)
}
function goToSection(id) {
  const el = document.getElementById(id)
  if (!el) return
  const calm = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches
  el.scrollIntoView({ behavior: calm ? 'auto' : 'smooth', block: 'start' })
  propsPinned = id
  propsActiveTab.value = id
}
// The user scrolling (the wheel, a touch, a scroll key, the scroll bar) ends it; the smooth scroll to a link doesn't.
function userScrolled(e) {
  if (e.type === 'keydown' && !isScrollKey(e.key)) return
  if (e.type === 'mousedown' && e.target !== document.documentElement) return
  propsPinned = null
  updateActiveTab()
}
const USER_SCROLL_EVENTS = ['wheel', 'touchmove', 'keydown', 'mousedown']
// The sections are as the page has them once it has drawn: when the pane opens, and after each later update (a
// section that shows only for some cameras, or while a switch is on).
watch(propsTarget, async () => { propsPinned = null; await nextTick(); readPropsTabs() })
onUpdated(readPropsTabs)
onMounted(() => {
  window.addEventListener('scroll', updateActiveTab, { passive: true })
  for (const t of USER_SCROLL_EVENTS) window.addEventListener(t, userScrolled, { passive: true })
})
onUnmounted(() => {
  window.removeEventListener('scroll', updateActiveTab)
  for (const t of USER_SCROLL_EVENTS) window.removeEventListener(t, userScrolled)
})
const propsDoorbell = ref(false)
// A doorbell's press source ("onvif" or "reolink_email"), and what the Doorbell section shows about its presses
// (read every 2 s while it shows; reading changes nothing on the camera, so it's never an unsaved change).
const propsPressSource = ref('onvif')
const doorPresses = ref(null)
const doorMail = ref(null)
const doorMailError = ref('')
const doorReveal = ref(false)
const doorConfirmNew = ref(false)
const doorNewPassword = ref(false)
const doorTesting = ref(false)
const copiedField = ref('')
const nowMs = ref(Date.now())
let doorTimer = null
// Moved on by New password and by opening a camera: a read of the email setup that began before either is stale.
let doorMailEpoch = 0
const propsMotion = ref(true)
const propsPrune = ref(false)
const propsHours = ref(24)
const propsTier = ref('auto')
// What the camera had when the pane opened, so Save waits for a change and leaving asks about one.
const propsBase = ref(null)
const confirmingDiscard = ref(false)
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
}

async function onStorageDone() {
  const id = storageCamera.value?.id
  storageCamera.value = null
  await refresh()
  // The pane stays open, on the camera as it is now (its file may have moved).
  const fresh = cameras.value.find((c) => c.id === id)
  if (fresh && propsTarget.value) propsTarget.value = fresh
}

// Where motion and object classes come from (r_vss::r_tier_controller), worded
// as in the desktop's camera properties.
const tierChoices = [
  { value: 'auto', title: 'Automatic', description: "The camera's own detection once Revere has checked it reports what Revere sees; Revere's otherwise." },
  { value: '1', title: 'Revere', description: 'Revere finds motion and objects in the video. Uses the most CPU.' },
  { value: '2', title: 'Camera', description: "The camera's motion and its person, vehicle and animal events. Uses the least CPU." },
  { value: '3', title: 'Camera motion, Revere objects', description: "The camera's motion events; Revere finds the objects in them." },
]
const propsDirty = computed(() => {
  const b = propsBase.value
  if (!propsTarget.value || !b) return false
  return propsDoorbell.value !== b.doorbell || propsMotion.value !== b.motion || propsPrune.value !== b.prune || propsTier.value !== b.tier ||
    (propsPrune.value && propsHours.value !== b.hours) || (propsDoorbell.value && propsPressSource.value !== b.pressSource)
})

const doorTest = computed(() => doorPresses.value?.test || null)
const testLines = computed(() => pressTestLines(doorTest.value))
const testVerdict = computed(() => pressTestVerdict(doorTest.value, propsPressSource.value))
const testSecondsLeft = computed(() => Math.max(0, Math.ceil(((doorTest.value?.ends_ms || 0) - nowMs.value) / 1000)))
const mailLines = computed(() => mailStatusLines(doorMail.value, doorPresses.value, propsPressSource.value))
// What to type in the Reolink app, a field at a time.
const mailFields = computed(() => mailSetupRows(doorMail.value))

async function readDoor() {
  const cam = propsTarget.value
  if (!cam || !propsDoorbell.value) return
  nowMs.value = Date.now()
  try { doorPresses.value = await getDoorbellPresses(cam.id) } catch { doorPresses.value = null }
  if (propsPressSource.value === 'reolink_email') {
    // The login once; after that, the listener's status without it.
    const haveLogin = !!doorMail.value?.password
    const epoch = doorMailEpoch
    try {
      const m = await getDoorbellMailSetup(cam.id, { login: !haveLogin })
      const stale = epoch !== doorMailEpoch
      doorMail.value = mailSetupAfterRead(doorMail.value, m, { haveLogin, stale })
      if (!stale) doorMailError.value = ''
    } catch (e) {
      const stale = epoch !== doorMailEpoch
      doorMail.value = mailSetupAfterRead(doorMail.value, null, { haveLogin, stale, failed: true })
      if (!stale) doorMailError.value = mailSetupError(e)
    }
  }
}
function stopDoorReads() {
  if (doorTimer) clearInterval(doorTimer)
  doorTimer = null
}
// Read while the Doorbell section shows: the pane open on a doorbell.
watch([propsTarget, propsDoorbell, propsPressSource], () => {
  stopDoorReads()
  if (propsTarget.value && propsDoorbell.value) {
    readDoor()
    doorTimer = setInterval(readDoor, 2000)
  }
})
onUnmounted(stopDoorReads)

// "New password", confirmed: a new email password, shown at once.
async function newPassword() {
  if (!propsTarget.value) return
  doorNewPassword.value = true
  doorMailEpoch++
  try {
    doorMail.value = await newDoorbellMailLogin(propsTarget.value.id)
    doorMailError.value = ''
    doorReveal.value = true
    doorConfirmNew.value = false
  } catch (e) {
    doorMailError.value = mailSetupError(e)
  } finally {
    doorNewPassword.value = false
  }
}

async function pressTest() {
  if (!propsTarget.value) return
  doorTesting.value = true
  try {
    await startDoorbellPressTest(propsTarget.value.id)
    await readDoor()
  } catch (e) {
    propsError.value = e.message
  } finally {
    doorTesting.value = false
  }
}

async function copyField(f) {
  await copyText(f.value)
  copiedField.value = f.key
  setTimeout(() => { if (copiedField.value === f.key) copiedField.value = '' }, 1500)
}
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

const discoveryWarning = ref('')

async function refresh() {
  try {
    cameras.value = await getCameras()
    error.value = null
  } catch (e) {
    error.value = `Could not reach Revere: ${e.message}`
  }
  // On its own: an older Revere without /discovery_status still lists cameras.
  try {
    discoveryWarning.value = (await getDiscoveryStatus()).warning || ''
  } catch (e) {
    discoveryWarning.value = ''
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
  propsDoorbell.value = camera.kind === 'doorbell'
  propsPressSource.value = camera.press_source || 'onvif'
  doorPresses.value = null
  doorMailEpoch++
  doorMail.value = null
  doorMailError.value = ''
  doorReveal.value = false
  doorConfirmNew.value = false
  propsMotion.value = !!camera.do_motion_detection
  propsPrune.value = !!camera.do_motion_pruning
  propsHours.value = camera.min_continuous_recording_hours ?? 24
  propsTier.value = camera.motion_tier_setting || 'auto'
  // Pruning needs motion detection, as the watch below enforces.
  propsBase.value = {
    doorbell: propsDoorbell.value,
    motion: propsMotion.value,
    prune: propsMotion.value && propsPrune.value,
    hours: propsHours.value,
    tier: propsTier.value,
    pressSource: propsPressSource.value,
  }
  confirmingDiscard.value = false
  propsError.value = ''
  copied.value = false
}

// navigator.clipboard needs a secure context (https or localhost). The web UI
// is served over plain http on 127.0.0.1, which counts as secure, but keep the
// execCommand fallback for browsers/contexts where it is still unavailable.
async function copyPath(text) {
  await copyText(text)
  copied.value = true
  setTimeout(() => { copied.value = false }, 1500)
}

async function copyText(text) {
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
}

function cancelProps() {
  if (propsBusy.value) return
  if (propsDirty.value) {
    confirmingDiscard.value = true
    return
  }
  propsTarget.value = null
}

function discardProps() {
  confirmingDiscard.value = false
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
      // Kept as a choice only once it differs from what the camera's model suggests, or was chosen before.
      ...doorbellChoice(propsTarget.value, propsDoorbell.value),
      ...pressSourceChoice(propsTarget.value, propsDoorbell.value, propsPressSource.value),
    })
    propsTarget.value = null
    await refresh()
  } catch (e) {
    propsError.value = e.message
  } finally {
    propsBusy.value = false
  }
}

// What the tabs and Sign out ask about before leaving (App.vue).
watch(propsDirty, (d) => { unsaved.value = d })
// Escape goes back, asking first when there are unsaved changes; not while a dialog is over the properties.
function onKey(e) {
  if (e.key !== 'Escape' || !propsTarget.value || storageCamera.value || removeTarget.value) return
  if (confirmingDiscard.value) { confirmingDiscard.value = false; return }
  cancelProps()
}

onMounted(() => {
  window.addEventListener('keydown', onKey)
  refresh()
  // New cameras appear via discovery; assigned cameras leave the discovered
  // list once recording starts — poll so the lists stay live.
  pollId = setInterval(refresh, 5000)
})

onUnmounted(() => {
  clearInterval(pollId)
  window.removeEventListener('keydown', onKey)
  unsaved.value = false
})
</script>

<style scoped>
.manage { display: flex; flex-direction: column; gap: 1.5rem; }

.toolbar { display: flex; }

.discovery-warning {
  margin: 0 0 0.75rem;
  padding: 0.5rem 0.75rem;
  border-left: 3px solid #f59e0b;
  background: rgba(245, 158, 11, 0.12);
  color: #fbbf24;
  font-size: 0.85rem;
  line-height: 1.4;
}
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

/* camera properties: in place of the lists */
.props-pane { display: flex; flex-direction: column; gap: 1rem; }
.props-head h2 { font-size: 1.1rem; font-weight: 600; }
.props-head .back { margin-bottom: 0.6rem; }
.cam-title { font-size: 0.85rem; color: #aaa; margin-top: 0.2rem; }
/* The row of section links: fixed at the top while the sections scroll, the one in view marked */
.props-tabs {
  position: sticky;
  top: 0;
  z-index: 5;
  max-width: 46rem;
  display: flex;
  flex-wrap: wrap;
  gap: 0.2rem 1.4rem;
  padding: 0.5rem 0 0;
  background: #0a0a0a;
  border-bottom: 1px solid #262626;
}
.props-tabs a {
  padding: 0.25rem 0 0.45rem;
  font-size: 0.85rem;
  font-weight: 600;
  color: #888;
  text-decoration: none;
  border-bottom: 2px solid transparent;
  margin-bottom: -1px;
}
.props-tabs a:hover { color: #ddd; border-bottom-color: #444; }
.props-tabs a.on { color: #f5f5f5; border-bottom-color: #4a90e2; }
.props-tabs a:focus-visible { outline: 2px solid #4a90e2; outline-offset: 2px; }
/* One column of sections, as wide as reads well: each a title over a card of rows with a line between them */
.props-sections { display: flex; flex-direction: column; max-width: 46rem; min-width: 0; }
.props-section {
  display: flex;
  flex-direction: column;
  min-width: 0;
  margin-bottom: 1.5rem;
  background: #141414;
  border: 1px solid #2a2a2a;
  border-radius: 6px;
}
.props-section > * { padding: 0.85rem 1rem; }
.props-section > * + * { border-top: 1px solid #262626; }
.props-section > .prune { display: flex; flex-direction: column; gap: 0.6rem; }
/* Text at the left and its button at the right, or under the text when it's narrow */
.action-row { display: flex; align-items: center; flex-wrap: wrap; gap: 0.5rem 1rem; }
.action-row .blurb { flex: 1 1 16rem; margin: 0; }
.action-row button { flex: none; }
.props-pane .check { display: flex; align-items: center; gap: 0.5rem; cursor: pointer; }
.props-pane .field { display: flex; align-items: center; gap: 0.6rem; }
.props-pane .field input {
  width: 90px;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  color: #eee;
  padding: 0.4rem 0.5rem;
  font-size: 0.9rem;
}
.props-pane .loc { display: flex; align-items: center; flex-wrap: wrap; gap: 0.4rem 1rem; font-size: 0.8rem; word-break: break-all; }
.props-pane .err { color: #e05555; font-size: 0.85rem; }
.props-pane .blurb { font-size: 0.85rem; color: #999; line-height: 1.45; }
.props-pane .option { align-items: flex-start; }
.props-pane .option input { margin-top: 0.2rem; flex: none; }
.props-pane .option .title { display: block; font-size: 0.9rem; }
.props-pane .option .desc { display: block; font-size: 0.8rem; color: #999; line-height: 1.4; margin-top: 0.1rem; }
.props-pane .tiers { border: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 0.5rem; }
.props-pane .tiers legend { font-size: 0.8rem; color: #bbb; margin-bottom: 0.4rem; padding: 0; }
.pills { display: flex; flex-wrap: wrap; gap: 0.4rem; }
.pill {
  border: 1px solid #333;
  background: #1e1e1e;
  color: #bbb;
  border-radius: 4px;
  padding: 0.3rem 0.7rem;
  font-size: 0.85rem;
  cursor: pointer;
}
.pill.on { background: #e8e8e8; border-color: #e8e8e8; color: #111; }
.pill input { position: absolute; opacity: 0; pointer-events: none; }
.pill:focus-within { outline: 2px solid #4a90e2; outline-offset: 1px; }
.props-pane .off { opacity: 0.5; }
.props-pane .off, .props-pane .off .option, .props-pane .off .pill { cursor: default; }
.props-pane .field.indent { margin-left: 1.5rem; font-size: 0.85rem; color: #bbb; flex-wrap: wrap; }
.props-pane .section {
  scroll-margin-top: 3.2rem;
  font-size: 0.8rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  color: #888;
  margin-bottom: 0.5rem;
}
.props-pane .loc .title { flex: none; font-size: 0.9rem; color: #ddd; }
.props-pane .storage-actions { display: flex; gap: 0.6rem; flex-wrap: wrap; }
.props-pane .path-row { flex: 1 1 18rem; display: flex; align-items: center; gap: 0.6rem; min-width: 0; }
.props-pane .path-row code {
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
/* A doorbell's press setup (the Doorbell section) */
.props-pane .mail-setup, .props-pane .press-check { display: flex; flex-direction: column; align-items: stretch; gap: 0.35rem; }
.props-pane .mail-setup .title, .props-pane .press-check .title { font-size: 0.9rem; }
.props-pane .mail-setup .blurb, .props-pane .press-check .blurb { margin: 0; }
.props-pane .mail-fields {
  display: grid;
  grid-template-columns: max-content minmax(0, 1fr);
  gap: 0.35rem 0.7rem;
  align-items: center;
  margin: 0.2rem 0;
  width: 100%;
}
.props-pane .mail-fields dt { font-size: 0.85rem; color: #999; }
.props-pane .mail-fields dd { margin: 0; display: flex; align-items: center; gap: 0.4rem; flex-wrap: wrap; min-width: 0; }
.props-pane .mail-fields code { font-size: 0.85rem; color: #ddd; word-break: break-all; min-width: 0; }
.props-pane .mail-buttons { display: inline-flex; gap: 0.3rem; margin-left: auto; }
.props-pane .new-password { display: flex; flex-wrap: wrap; align-items: center; gap: 0.4rem; margin: 0.4rem 0; }
.props-pane .new-password .warn { flex-basis: 100%; margin: 0; }
.props-pane .test-line { margin: 0; font-size: 0.9rem; }
.props-pane .warn { margin: 0; font-size: 0.85rem; color: #e0a040; line-height: 1.45; }
/* Cancel and Save stay at the bottom while the sections scroll */
.props-foot {
  max-width: 46rem;
  position: sticky;
  bottom: 0;
  display: flex;
  align-items: center;
  gap: 0.6rem;
  padding: 0.85rem 0;
  background: #0a0a0a;
  border-top: 1px solid #363636;
}
.props-foot .spacer { flex: 1; }
.props-foot .discard-ask { font-size: 0.9rem; }
.props-foot button:not(.small) { padding: 0.45rem 1rem; font-size: 0.9rem; }
</style>
