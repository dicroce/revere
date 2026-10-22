<template>
  <section class="plugins">
    <h2>Plugins <span v-if="authed" class="count">{{ plugins.length }}</span></h2>
    <p v-if="!authed" class="empty">Sign in to see and manage plugins.</p>
    <p v-else-if="error" class="error">{{ error }}</p>
    <p v-else-if="loaded && plugins.length === 0" class="empty">No system plugins loaded.</p>

    <div v-for="p in plugins" :key="p.name" class="plugin">
      <div class="head">
        <span class="dot" :class="p.status ? p.status.tone : 'neutral'"></span>
        <span class="name">{{ p.display_name || shortName(p.name) }}</span>
        <span v-if="p.version" class="version">{{ p.version }}</span>
        <span class="state">{{ p.status ? p.status.label : (p.enabled ? 'Enabled' : 'Disabled') }}</span>
        <label class="switch" :title="p.enabled ? 'Turn this plugin off' : 'Turn this plugin on'">
          <input
            type="checkbox"
            :checked="p.enabled"
            :disabled="busy === p.name"
            @change="toggle(p, $event.target.checked)"
          />
          <span class="track"><span class="knob"></span></span>
          <span class="switch-label">Enabled</span>
        </label>
      </div>

      <div v-if="p.lines.length" class="lines">
        <template v-for="(group, gi) in groups(p.lines)" :key="gi">
          <!-- links that follow each other share a row, as on the desktop -->
          <p v-if="group.kind === 'links'" class="links">
            <a v-for="(l, li) in group.lines" :key="li" :href="safeUrl(l.url)" target="_blank" rel="noopener noreferrer">{{ l.text }}</a>
          </p>
          <div v-else-if="group.kind === 'copy'" class="copy">
            <span class="text">{{ group.line.text }}</span>
            <code>{{ group.line.copy }}</code>
            <button class="ghost small" @click="copy(group.line.copy)">{{ copied === group.line.copy ? 'Copied' : 'Copy' }}</button>
          </div>
          <div v-else-if="group.kind === 'tiles'" class="tiles-block">
            <p v-if="group.line.title" class="tiles-title">{{ group.line.title }}</p>
            <div class="tiles">
              <div v-for="(t, ti) in validTiles(group.line.tiles)" :key="ti" class="tile" :title="t.hint || ''">
                <span class="label">{{ t.label }}</span>
                <span class="value">{{ t.value }}</span>
                <span v-if="t.detail" class="detail">{{ t.detail }}</span>
              </div>
            </div>
          </div>
          <div v-else-if="group.kind === 'qr'" class="qr">
            <p class="muted">Or scan with your phone:</p>
            <img :src="'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(group.line.qr_svg)" alt="QR code" />
          </div>
          <p v-else class="text">{{ group.line.text }}</p>
        </template>
      </div>
      <p v-if="toggleError && toggleError.name === p.name" class="error">{{ toggleError.message }}</p>
    </div>
  </section>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted } from 'vue'
import { getSystemPlugins, setSystemPluginEnabled } from '../api.js'

const props = defineProps({ authed: { type: Boolean, default: false } })

const plugins = ref([])
const loaded = ref(false)
const error = ref('')
const busy = ref(null)
const toggleError = ref(null)
const copied = ref('')

// "revere_cloud_system_plugin" -> "revere_cloud", for a plugin that doesn't
// name itself (as the desktop's plugin_short_name).
function shortName(name) {
  return name.replace(/_system_plugin$/, '')
}

// Only http(s) links are followed; anything else a plugin sends is inert.
function safeUrl(url) {
  return /^https?:\/\//i.test(url || '') ? url : undefined
}

function validTiles(tiles) {
  return (Array.isArray(tiles) ? tiles : []).filter((t) => t && t.label && t.value)
}

// The plugin's lines as the desktop draws them: consecutive links on one row,
// and lines the desktop would skip (no text, an empty grid, a QR that didn't
// encode) left out.
function groups(lines) {
  const out = []
  for (const l of lines) {
    if (l.qr !== undefined) {
      if (l.qr_svg) out.push({ kind: 'qr', line: l })
    } else if (l.tiles !== undefined) {
      if (validTiles(l.tiles).length) out.push({ kind: 'tiles', line: l })
    } else if (!l.text) {
      continue
    } else if (l.url) {
      const last = out[out.length - 1]
      if (last && last.kind === 'links') last.lines.push(l)
      else out.push({ kind: 'links', lines: [l] })
    } else if (l.copy) {
      out.push({ kind: 'copy', line: l })
    } else {
      out.push({ kind: 'text', line: l })
    }
  }
  return out
}

async function refresh() {
  if (!props.authed) {
    plugins.value = []
    return
  }
  try {
    plugins.value = await getSystemPlugins()
    error.value = ''
  } catch (e) {
    error.value = e.message
  } finally {
    loaded.value = true
  }
}

async function toggle(p, enabled) {
  busy.value = p.name
  toggleError.value = null
  try {
    await setSystemPluginEnabled(p.name, enabled)
  } catch (e) {
    toggleError.value = { name: p.name, message: e.message }
  }
  busy.value = null
  await refresh()
}

// navigator.clipboard needs a secure context (https or localhost); fall back
// to execCommand over plain http.
async function copy(text) {
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
  copied.value = text
  setTimeout(() => { if (copied.value === text) copied.value = '' }, 1500)
}

let pollId = null
watch(() => props.authed, refresh)
onMounted(() => {
  refresh()
  // A plugin's status changes on its own (connecting, a pairing code): poll.
  pollId = setInterval(refresh, 5000)
})
onUnmounted(() => clearInterval(pollId))
</script>

<style scoped>
.plugins h2 {
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
.empty, .muted { color: #888; font-size: 0.9rem; }
.error { color: #e05555; font-size: 0.85rem; }

.plugin {
  padding: 0.7rem 0.85rem;
  background: #1e1e1e;
  border: 1px solid #363636;
  border-radius: 6px;
  display: flex;
  flex-direction: column;
  gap: 0.6rem;
}
.plugin + .plugin { margin-top: 0.4rem; }
.head { display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap; }
.name { font-weight: 500; }
.version { color: #666; font-size: 0.75rem; }
.state { color: #aaa; font-size: 0.85rem; }

.dot { width: 8px; height: 8px; border-radius: 50%; flex: none; }
.dot.ok { background: #4caf72; }
.dot.warning { background: #e0a83a; }
.dot.error { background: #e05555; }
.dot.neutral { background: #777; }

.switch { margin-left: auto; display: flex; align-items: center; gap: 0.45rem; cursor: pointer; font-size: 0.85rem; color: #ccc; }
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

.lines { display: flex; flex-direction: column; gap: 0.45rem; font-size: 0.85rem; color: #bbb; line-height: 1.45; }
.links { display: flex; flex-wrap: wrap; gap: 0.3rem 1rem; }
.links a { color: #4a9eff; }
.copy { display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap; }
.copy code {
  font-size: 0.8rem;
  color: #ddd;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  padding: 0.25rem 0.5rem;
  word-break: break-all;
}
.tiles-title { font-weight: 600; color: #aaa; font-size: 0.8rem; }
.tiles { display: grid; grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); gap: 0.5rem; }
.tile { background: #161616; border: 1px solid #2c2c2c; border-radius: 6px; padding: 0.5rem 0.6rem; display: flex; flex-direction: column; }
.tile .label { font-size: 0.75rem; color: #888; }
.tile .value { font-size: 1rem; color: #eee; font-weight: 500; }
.tile .detail { font-size: 0.75rem; color: #888; }
.qr img { width: 148px; height: 148px; image-rendering: pixelated; margin-top: 0.3rem; }

button.ghost {
  background: none;
  border: 1px solid #333;
  color: #ccc;
  border-radius: 4px;
  cursor: pointer;
}
button.small { padding: 0.35rem 0.75rem; font-size: 0.8rem; }
</style>
