<template>
  <div class="camera-view">
    <header class="header">
      <button class="back-btn" @click="$emit('close')">&#8592; Back</button>
      <span class="title">{{ camera.friendly_name || camera.camera_name }}</span>
    </header>

    <div class="player-wrap">
      <CameraPlayer :camera="camera" :iso-time="seekedTime" @timeupdate="onPlayerTime" />
    </div>

    <div class="timeline-bar">
      <TimelineBar
        :camera-id="camera.id"
        :camera-name="camera.friendly_name || camera.camera_name"
        :playhead-iso="playheadIso"
        @seek="onSeek"
        @live="onLive"
      />
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import TimelineBar from './TimelineBar.vue'
import CameraPlayer from './CameraPlayer.vue'

defineProps({ camera: Object })
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
</script>

<style scoped>
.camera-view {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #0a0a0a;
}

.header {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem 1.5rem;
  background: #1e1e1e;
  border-bottom: 1px solid #363636;
  flex-shrink: 0;
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
