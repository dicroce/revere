<template>
  <section class="keep-awake">
    <h2>Keep this computer awake</h2>
    <p v-if="!authed" class="empty">Sign in to see and change this.</p>
    <p v-else-if="error" class="error">{{ error }}</p>

    <div v-else-if="state" class="card">
      <div class="row">
        <div class="text">
          <span class="name">Keep this computer awake</span>
          <span class="hint">While it's plugged in, the computer won't go to sleep on its own, so recording doesn't stop. The display still turns off.</span>
        </div>
        <label class="switch" :title="state.enabled ? 'Let the computer sleep' : 'Keep the computer awake'">
          <input
            type="checkbox"
            :checked="state.enabled"
            :disabled="busy"
            @change="change({ enabled: $event.target.checked })"
          />
          <span class="track"><span class="knob"></span></span>
        </label>
      </div>

      <div class="row sub">
        <div class="text">
          <span class="name">Also on battery</span>
          <span class="hint">Keep a laptop awake on battery too. It will record until the battery runs out.</span>
        </div>
        <label class="switch">
          <input
            type="checkbox"
            :checked="state.on_battery"
            :disabled="busy || !state.enabled"
            @change="change({ on_battery: $event.target.checked })"
          />
          <span class="track"><span class="knob"></span></span>
        </label>
      </div>

      <p class="status">
        <span class="dot" :class="status.tone"></span>
        <span>{{ status.text }}</span>
      </p>
      <p v-if="changeError" class="error">{{ changeError }}</p>

      <div v-if="state.events.length" class="events">
        <p class="events-title">Recent sleeps</p>
        <ul>
          <li v-for="(e, i) in state.events.slice(0, 5)" :key="i">
            <span class="what">{{ e.what === 'asleep' ? 'Asleep' : "Revere didn't run" }}</span>
            <span class="when">{{ when(e) }}</span>
          </li>
        </ul>
        <p class="muted">Video wasn't recorded during these.</p>
      </div>

      <details class="lid">
        <summary>Recording with the lid closed</summary>
        <p>Closing a laptop's lid puts it to sleep, whatever this setting says. To keep recording with it closed:</p>
        <ul>
          <li><b>Mac:</b> connect power, an external display and a keyboard or mouse. A Mac then stays awake with its lid closed.</li>
          <li><b>Windows:</b> Control Panel → Power Options → Choose what closing the lid does → When plugged in: Do nothing.</li>
          <li><b>Linux:</b> in <code>/etc/systemd/logind.conf</code>, set <code>HandleLidSwitchExternalPower=ignore</code>, then restart (some desktops have their own lid setting too).</li>
        </ul>
      </details>
    </div>
  </section>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { getKeepAwake, setKeepAwake } from '../api.js'

const props = defineProps({ authed: { type: Boolean, default: false } })

const state = ref(null)
const error = ref('')
const changeError = ref('')
const busy = ref(false)

const status = computed(() => {
  const s = state.value
  if (!s.enabled) return { tone: 'neutral', text: 'Off: the computer sleeps as usual.' }
  if (s.problem) return { tone: 'error', text: `The computer may still go to sleep: ${s.problem}` }
  if (s.unavailable) return { tone: 'neutral', text: "Not available on this desktop: it has no suspend inhibitor to ask, so its own power settings decide when it sleeps." }
  if (s.held) return { tone: 'ok', text: "On: the computer won't go to sleep on its own." }
  if (s.power === 'battery') return { tone: 'neutral', text: 'On battery: the computer sleeps as usual.' }
  return { tone: 'neutral', text: 'Starting…' }
})

function duration(ms) {
  const min = Math.round(ms / 60000)
  if (min < 1) return `${Math.max(1, Math.round(ms / 1000))} s`
  if (min < 120) return `${min} min`
  return `${Math.round(min / 60)} h`
}

function when(e) {
  const start = new Date(e.start_ms)
  const sameDay = start.toDateString() === new Date().toDateString()
  const t = sameDay
    ? start.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    : start.toLocaleString([], { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
  return `${t}, ${duration(e.end_ms - e.start_ms)}`
}

async function refresh() {
  if (!props.authed) {
    state.value = null
    return
  }
  try {
    state.value = await getKeepAwake()
    error.value = ''
  } catch (e) {
    error.value = e.message
  }
}

async function change(changes) {
  busy.value = true
  changeError.value = ''
  try {
    state.value = await setKeepAwake(changes)
    // The request is held or released just after the reply: show it.
    setTimeout(refresh, 1000)
  } catch (e) {
    changeError.value = e.message
    await refresh()
  }
  busy.value = false
}

let pollId = null
watch(() => props.authed, refresh)
onMounted(() => {
  refresh()
  // The power source changes on its own (a laptop unplugged): poll.
  pollId = setInterval(refresh, 10000)
})
onUnmounted(() => clearInterval(pollId))
</script>

<style scoped>
.keep-awake h2 {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.6rem;
}
.empty, .muted { color: #888; font-size: 0.85rem; }
.error { color: #e05555; font-size: 0.85rem; }

.card {
  padding: 0.7rem 0.85rem;
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 6px;
  display: flex;
  flex-direction: column;
  gap: 0.7rem;
}
.row { display: flex; align-items: flex-start; gap: 1rem; }
.row.sub { padding-left: 1rem; }
.text { display: flex; flex-direction: column; gap: 0.15rem; flex: 1; min-width: 0; }
.name { font-weight: 500; }
.hint { color: #999; font-size: 0.85rem; line-height: 1.4; }

.status { display: flex; align-items: center; gap: 0.5rem; font-size: 0.85rem; color: #bbb; }
.dot { width: 8px; height: 8px; border-radius: 50%; flex: none; }
.dot.ok { background: #4caf72; }
.dot.error { background: #e05555; }
.dot.neutral { background: #777; }

.events-title { font-weight: 600; color: #aaa; font-size: 0.8rem; margin-bottom: 0.3rem; }
.events ul { list-style: none; display: flex; flex-direction: column; gap: 0.2rem; margin-bottom: 0.3rem; }
.events li { display: flex; gap: 0.75rem; font-size: 0.85rem; }
.events .what { color: #ddd; min-width: 8.5rem; }
.events .when { color: #999; }

.lid { font-size: 0.85rem; color: #bbb; line-height: 1.45; }
.lid summary { cursor: pointer; color: #4a9eff; }
.lid p { margin: 0.4rem 0 0.3rem; }
.lid ul { padding-left: 1.2rem; display: flex; flex-direction: column; gap: 0.25rem; }
.lid code { font-size: 0.8rem; background: #111; border: 1px solid #333; border-radius: 3px; padding: 0 0.25rem; word-break: break-all; }

.switch { flex: none; display: flex; align-items: center; cursor: pointer; padding-top: 0.15rem; }
.switch input { position: absolute; opacity: 0; width: 0; height: 0; }
.track {
  width: 32px;
  height: 18px;
  border-radius: 9px;
  background: #3a3a3a;
  position: relative;
  transition: background 0.15s;
}
.knob {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #ddd;
  transition: transform 0.15s;
}
.switch input:checked + .track { background: #2d6cdf; }
.switch input:checked + .track .knob { transform: translateX(14px); }
.switch input:focus-visible + .track { outline: 2px solid #4a9eff; outline-offset: 2px; }
.switch input:disabled + .track { opacity: 0.5; }
</style>
