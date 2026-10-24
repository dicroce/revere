<template>
  <div class="camera-view" ref="viewRoot">
    <header class="header">
      <button class="back-btn" @click="$emit('close')">&#8592; Back</button>
      <span class="title">{{ camera.friendly_name || camera.camera_name }}</span>

      <!-- Viewing actions (snapshot / fullscreen / shortcut hint). -->
      <div class="actions">
        <button class="act-btn" title="Save snapshot (S)" :disabled="snapshotBusy" @click="takeSnapshot">
          <svg viewBox="0 0 24 24" width="18" height="18"><path d="M4 7h3l2-2h6l2 2h3a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1z" fill="none" stroke="currentColor" stroke-width="1.8"/><circle cx="12" cy="13" r="3.5" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>
        </button>
        <button v-if="fullscreenSupported" class="act-btn" :title="isFullscreen ? 'Exit fullscreen (F)' : 'Fullscreen (F)'" @click="toggleFullscreen">
          <svg v-if="!isFullscreen" viewBox="0 0 24 24" width="18" height="18"><path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>
          <svg v-else viewBox="0 0 24 24" width="18" height="18"><path d="M9 4v5H4M15 9V4h5M20 15h-5v5M4 15h5v5" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>
        </button>
        <button class="act-btn" title="Keyboard shortcuts (?)" :class="{ active: showHelp }" @click="showHelp = !showHelp">?</button>
      </div>

      <div v-if="showHelp" class="help" @click="showHelp = false">
        <div class="help-row"><kbd>Space</kbd><span>Play / pause</span></div>
        <div class="help-row"><kbd>&#8592;</kbd> <kbd>&#8594;</kbd><span>Pan timeline</span></div>
        <div class="help-row"><kbd>L</kbd><span>Go live</span></div>
        <div class="help-row"><kbd>M</kbd><span>Mute / unmute</span></div>
        <div class="help-row"><kbd>S</kbd><span>Save snapshot</span></div>
        <div class="help-row"><kbd>F</kbd><span>Fullscreen</span></div>
      </div>
    </header>

    <div class="player-wrap">
      <CameraPlayer ref="player" :camera="camera" :iso-time="seekedTime" @timeupdate="onPlayerTime" />
    </div>

    <div class="timeline-bar">
      <TimelineBar
        ref="timeline"
        :camera-id="camera.id"
        :camera-name="camera.friendly_name || camera.camera_name"
        :playhead-iso="playheadIso"
        :live="seekedTime === null"
        @seek="onSeek"
        @live="onLive"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import TimelineBar from './TimelineBar.vue'
import CameraPlayer from './CameraPlayer.vue'
import { apiFetch } from '../api.js'

const props = defineProps({ camera: Object })
defineEmits(['close'])

// null = live; an ISO string = a seeked recorded position. CameraPlayer reads
// this directly to switch between live refresh, scrub still, and video playback.
const seekedTime = ref(null)

// Absolute ISO of the frame currently being played, emitted by CameraPlayer; the
// timeline uses it to advance the playhead marker during playback.
const playheadIso = ref(null)

function onSeek(isoString) { seekedTime.value = isoString }
function onLive()          { seekedTime.value = null; playheadIso.value = null }
function onPlayerTime(iso) { playheadIso.value = iso }

// --- snapshot / fullscreen / keyboard --------------------------------------
const viewRoot    = ref(null)   // fullscreen target: player + timeline together
const player      = ref(null)   // CameraPlayer (exposes togglePlay / toggleMute)
const timeline    = ref(null)   // TimelineBar  (exposes shiftBack / shiftForward / goLive)
const showHelp    = ref(false)
const snapshotBusy = ref(false)

// Wall-clock of the frame on screen: the playing frame if video is up, else the
// scrubbed position, else "now" (same 5 s lag CameraCard uses for live stills).
function currentFrameIso() {
  if (playheadIso.value) return playheadIso.value
  if (seekedTime.value)  return seekedTime.value
  return new Date(Date.now() - 5000).toISOString()
}

function safeFileBase(name) {
  return (name || 'camera').trim().replace(/[^a-zA-Z0-9_-]+/g, '_').replace(/^_+|_+$/g, '') || 'camera'
}
function fmtForFile(d) {
  // 2026-05-23_16-03-09 — filesystem-safe variant of the ISO timestamp.
  return d.toISOString().replace('T', '_').replace(/[:.]/g, '-').replace(/Z$/, '')
}

// Fetch /jpg at the current frame (full camera resolution when known) and hand
// it to the browser as a download. Goes through fetch + blob + object URL so the
// bearer token (apiFetch) is honoured, unlike a bare <a href="/jpg..."> link.
async function takeSnapshot() {
  if (snapshotBusy.value) return
  snapshotBusy.value = true
  try {
    const iso = currentFrameIso()
    const w = Number(props.camera.width)  || 1920
    const h = Number(props.camera.height) || 1080
    const url = `/jpg?camera_id=${props.camera.id}`
              + `&start_time=${encodeURIComponent(iso)}&width=${w}&height=${h}`
    const res  = await apiFetch(url)
    const blob = await res.blob()
    const name = `${safeFileBase(props.camera.friendly_name || props.camera.camera_name)}_${fmtForFile(new Date(iso))}.jpg`
    const href = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = href
    a.download = name
    document.body.appendChild(a)
    a.click()
    a.remove()
    setTimeout(() => URL.revokeObjectURL(href), 1000)
  } catch (err) {
    alert('Snapshot failed: ' + (err && err.message ? err.message : err))
  } finally {
    snapshotBusy.value = false
  }
}

const fullscreenSupported = typeof document !== 'undefined'
  && !!(document.fullscreenEnabled || document.webkitFullscreenEnabled)
const isFullscreen = ref(false)

function fsElement() { return document.fullscreenElement || document.webkitFullscreenElement || null }
function onFullscreenChange() { isFullscreen.value = fsElement() === viewRoot.value }

function toggleFullscreen() {
  if (!fullscreenSupported) return
  const el = viewRoot.value
  if (!el) return
  if (fsElement()) {
    const exit = document.exitFullscreen || document.webkitExitFullscreen
    exit && exit.call(document)
  } else {
    const req = el.requestFullscreen || el.webkitRequestFullscreen
    req && Promise.resolve(req.call(el)).catch(() => {})
  }
}

// Only when focus isn't in a text/select control, so typing elsewhere (e.g. a
// future search box) isn't hijacked. Escape in fullscreen is the browser's.
function isEditable(el) {
  if (!el) return false
  const tag = el.tagName
  return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || el.isContentEditable
}

function onKeyDown(e) {
  if (e.defaultPrevented || e.ctrlKey || e.metaKey || e.altKey) return
  if (isEditable(e.target)) return
  switch (e.key) {
    case ' ':
      // Mirrors the on-screen play overlay, which only exists in seeked playback.
      if (seekedTime.value !== null && player.value) player.value.togglePlay()
      break
    case 'ArrowLeft':  timeline.value && timeline.value.shiftBack();    break
    case 'ArrowRight': timeline.value && timeline.value.shiftForward(); break
    case 'l': case 'L': timeline.value && timeline.value.goLive();      break
    case 'm': case 'M': player.value && player.value.toggleMute();      break
    case 's': case 'S': takeSnapshot();                                 break
    case 'f': case 'F': toggleFullscreen();                             break
    case '?':           showHelp.value = !showHelp.value;               break
    default: return
  }
  e.preventDefault()
}

onMounted(() => {
  window.addEventListener('keydown', onKeyDown)
  document.addEventListener('fullscreenchange', onFullscreenChange)
  document.addEventListener('webkitfullscreenchange', onFullscreenChange)
})
onUnmounted(() => {
  window.removeEventListener('keydown', onKeyDown)
  document.removeEventListener('fullscreenchange', onFullscreenChange)
  document.removeEventListener('webkitfullscreenchange', onFullscreenChange)
  if (fsElement() === viewRoot.value) {
    const exit = document.exitFullscreen || document.webkitExitFullscreen
    exit && exit.call(document)
  }
})
</script>

<style scoped>
.camera-view {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #0a0a0a;
}

/* In fullscreen the element is sized to the screen; let the flex column fill it. */
.camera-view:fullscreen,
.camera-view:-webkit-full-screen {
  width: 100%;
  height: 100%;
}

.header {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem 1.5rem;
  background: #1e1e1e;
  border-bottom: 1px solid #363636;
  flex-shrink: 0;
  position: relative;
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

.back-btn:hover { background: #363636; color: #fff; }

.title {
  font-size: 1.1rem;
  font-weight: 500;
}

.actions {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 0.35rem;
}

.act-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  min-width: 30px;
  height: 30px;
  padding: 0 0.4rem;
  background: none;
  border: 1px solid #3a3a3a;
  color: #ccc;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}
.act-btn:hover { background: #363636; color: #fff; }
.act-btn:disabled { opacity: 0.4; cursor: default; }
.act-btn.active { border-color: #4caf50; color: #4caf50; }

.help {
  position: absolute;
  top: calc(100% + 6px);
  right: 1.5rem;
  z-index: 10;
  background: #1e1e1e;
  border: 1px solid #3a3a3a;
  border-radius: 6px;
  padding: 0.5rem 0.75rem;
  display: grid;
  gap: 0.3rem;
  font-size: 0.8rem;
  color: #ccc;
  box-shadow: 0 4px 16px rgba(0,0,0,0.5);
}
.help-row { display: flex; align-items: center; gap: 0.5rem; }
.help-row span { margin-left: auto; padding-left: 1rem; color: #aaa; }
kbd {
  display: inline-block;
  min-width: 1.4em;
  text-align: center;
  padding: 0 0.35em;
  border: 1px solid #555;
  border-bottom-width: 2px;
  border-radius: 3px;
  background: #2a2a2a;
  color: #eee;
  font-family: inherit;
  font-size: 0.75rem;
}

.player-wrap {
  flex: 1;
  background: #000;
  position: relative;
  overflow: hidden;
  min-height: 0;
}

.timeline-bar {
  height: 112px;
  flex-shrink: 0;
  background: #111;
  border-top: 1px solid #363636;
}
</style>
