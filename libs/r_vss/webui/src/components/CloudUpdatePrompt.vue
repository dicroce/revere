<template>
  <!-- Revere Cloud was built for an older Revere and its update was found
       (GET /revere_cloud_install update_check "available"): ask once a browser
       session, and show the update through. -->
  <div v-if="open" class="overlay">
    <div class="dialog" role="dialog" aria-labelledby="cloud-update-title">
      <header>
        <h3 id="cloud-update-title">Update Revere Cloud</h3>
      </header>

      <div class="body">
        <template v-if="cloud.state === 'installing'">
          <p class="text">Updating Revere Cloud...</p>
        </template>
        <template v-else-if="cloud.state === 'installed'">
          <p class="text">Revere Cloud is updated and running.</p>
        </template>
        <template v-else-if="cloud.error_kind">
          <p class="danger">{{ errorText }}<span v-if="cloud.error_detail" class="muted"> ({{ cloud.error_detail }})</span></p>
        </template>
        <template v-else>
          <p class="text">This version of Revere needs a newer Revere Cloud, so Revere Cloud isn't running.
            An update<span v-if="cloud.update_version"> (from release {{ cloud.update_version }})</span> is available.</p>
          <p class="text">Update Revere Cloud now?</p>
          <p v-if="cloud.dev_warning" class="warning">{{ cloud.dev_warning }}</p>
          <p v-if="!cloud.terms_accepted" class="text">The Revere Cloud terms have changed. By updating you agree to the
            <a :href="safeUrl(cloud.terms_url)" target="_blank" rel="noopener noreferrer">Revere Cloud terms</a>
            (version {{ cloud.terms_version }}).</p>
        </template>
        <p v-if="startError" class="danger">{{ startError }}</p>
      </div>

      <footer>
        <span class="spacer"></span>
        <template v-if="cloud.state === 'installing'">
          <button class="ghost" @click="close">Hide</button>
        </template>
        <template v-else-if="cloud.state === 'installed'">
          <button class="primary" @click="close">Close</button>
        </template>
        <template v-else-if="cloud.error_kind">
          <button class="ghost" @click="close">Close</button>
          <button v-if="cloud.can_install" class="primary" :disabled="busy" @click="update(false)">Try again</button>
        </template>
        <template v-else>
          <button class="ghost" @click="notNow">Not now</button>
          <button v-if="cloud.can_install" class="primary" :disabled="busy" @click="update(!cloud.terms_accepted)">
            {{ cloud.terms_accepted ? 'Update' : 'Agree and update' }}
          </button>
        </template>
      </footer>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { getRevereCloudInstall, startRevereCloudInstall } from '../api.js'

const emit = defineEmits(['updated'])

const cloud = ref(null)
const open = ref(false)
const busy = ref(false)
const startError = ref('')

// "Not now" lasts the browser session, for that update.
const DISMISS_KEY = 'revere.cloudUpdateDismissed'

// The same words as the desktop (apps/revere cloud_download.cpp).
const CLOUD_ERRORS = {
  not_available: "Revere Cloud isn't available for this version of Revere yet. You can get it from the download page instead.",
  download_failed: "Revere Cloud couldn't be downloaded. Check this computer's internet connection and try again.",
  check_failed: "The downloaded Revere Cloud failed its integrity check, so it wasn't installed.",
  write_failed: "Revere Cloud couldn't be saved to the plugins folder.",
  load_failed: "Revere Cloud was installed but couldn't be started. Restart Revere to try again.",
}
const errorText = computed(() => CLOUD_ERRORS[cloud.value?.error_kind] || "Revere Cloud couldn't be updated.")

function dismissed(c) {
  try { return sessionStorage.getItem(DISMISS_KEY) === (c.update_version || 'any') } catch { return false }
}

let pollId = null
async function poll() {
  clearTimeout(pollId)
  let c
  try {
    c = await getRevereCloudInstall()
  } catch {
    return   // an older revere without the route, or signed out: nothing to ask
  }
  const was = cloud.value?.state
  cloud.value = c

  if (open.value) {
    if (c.state === 'installing') pollId = setTimeout(poll, 1000)
    else if (was === 'installing' && c.state === 'installed') emit('updated')
    return
  }
  if (c.state !== 'needs_update') return   // nothing to update (installed, not installed, ...)
  if (c.update_check === 'available' && c.can_install && !dismissed(c)) {
    open.value = true
    return
  }
  // The check revere starts at launch may still be running.
  if (c.update_check === 'not_checked' || c.update_check === 'checking') pollId = setTimeout(poll, 5000)
}

async function update(acceptTerms) {
  busy.value = true
  startError.value = ''
  try {
    cloud.value = await startRevereCloudInstall(acceptTerms ? cloud.value.terms_version : null)
  } catch (e) {
    startError.value = e.message
  }
  busy.value = false
  poll()
}

function notNow() {
  try { sessionStorage.setItem(DISMISS_KEY, cloud.value.update_version || 'any') } catch { /* private mode */ }
  close()
}

function close() {
  clearTimeout(pollId)
  open.value = false
}

// Only http(s) links are followed.
function safeUrl(url) {
  return /^https?:\/\//i.test(url || '') ? url : undefined
}

onMounted(poll)
onUnmounted(() => clearTimeout(pollId))
</script>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 120;
}
.dialog {
  width: 460px;
  max-width: calc(100vw - 2rem);
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 8px;
}
header { padding: 0.85rem 1.1rem; border-bottom: 1px solid #363636; }
header h3 { font-size: 1rem; font-weight: 600; }
.body { padding: 1.1rem; display: flex; flex-direction: column; gap: 0.7rem; }
.text { font-size: 0.85rem; color: #ccc; line-height: 1.45; }
.text a { color: #6ea0ff; }
.warning { font-size: 0.85rem; color: #e0b85a; line-height: 1.45; }
.danger { font-size: 0.85rem; color: #e08585; line-height: 1.45; }
.muted { color: #999; }
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
</style>
