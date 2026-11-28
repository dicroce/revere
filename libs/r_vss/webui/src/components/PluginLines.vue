<template>
  <!-- A plugin's lines as the desktop draws them. A list row's detail is lines too, drawn by this component again. -->
  <template v-for="(group, gi) in groups(lines)" :key="gi">
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
    <!-- a choice: the summary, then a radio list of its options (the desktop draws the same) -->
    <div v-else-if="group.kind === 'choice'" class="choice-block">
      <p class="text" :class="{ warn: lineWarn(group.line) }">{{ group.line.text }}</p>
      <div class="choice" role="radiogroup" :aria-label="group.line.text" :key="radioKey">
        <label v-for="o in choiceOptions(group.line)" :key="o.value" class="choice-row" :class="{ selected: choiceSelected(group.line, o) }">
          <input type="radio" :name="pluginName + '-' + uid + '-' + gi" :value="o.value" :checked="choiceSelected(group.line, o)"
                 :disabled="busy" @change="emit('pick', group.line, o)" />
          <span class="choice-text">
            <span class="choice-name">{{ o.name }}</span>
            <span v-if="o.subtitle" class="choice-subtitle">{{ o.subtitle }}</span>
            <span v-if="o.cost" class="choice-cost" :class="{ warn: o.warn }">{{ o.cost }}</span>
          </span>
        </label>
      </div>
    </div>
    <!-- a list: a row for each, and the open row's detail in its place under Back -->
    <div v-else-if="group.kind === 'list'" class="list-block">
      <template v-if="shown(gi, group.line)">
        <button class="ghost small back" @click="close(gi)">&lsaquo; {{ backLabel(group.line) }}</button>
        <p class="list-title">{{ shown(gi, group.line).name }}</p>
        <p v-if="shown(gi, group.line).state" class="list-state" :class="shown(gi, group.line).tone">{{ shown(gi, group.line).state }}</p>
        <PluginLines :key="shown(gi, group.line).id" :lines="shown(gi, group.line).detail" :plugin-name="pluginName" :busy="busy" :radio-key="radioKey"
                     @pick="(l, o) => emit('pick', l, o)" @run="(a) => emit('run', a)" />
      </template>
      <div v-else class="list" role="list">
        <button v-for="r in listRows(group.line)" :key="r.id" class="list-row" role="listitem" @click="open(gi, r.id)">
          <span class="dot" :class="r.tone"></span>
          <span class="list-text">
            <span class="list-name">{{ r.name }}</span>
            <span v-if="r.value" class="list-value">{{ r.value }}</span>
          </span>
          <span v-if="r.state" class="list-state" :class="r.tone">{{ r.state }}</span>
          <span class="chevron" aria-hidden="true">&rsaquo;</span>
        </button>
      </div>
    </div>
    <!-- a line with a button: the plugin's action, after its question if it has one -->
    <div v-else-if="group.kind === 'action'" class="action-line">
      <p class="text" :class="{ warn: lineWarn(group.line) }">{{ group.line.text }}</p>
      <div v-if="asking === group.button.action" class="ask">
        <p class="text">{{ group.button.confirm }}</p>
        <div class="actions">
          <button class="ghost small" @click="asking = ''">Cancel</button>
          <button class="danger small" :disabled="busy" @click="run(group.button)">{{ group.button.label }}</button>
        </div>
      </div>
      <button v-else class="ghost small" :disabled="busy" @click="onButton(group.button)">{{ group.button.label }}</button>
    </div>
    <p v-else class="text" :class="{ warn: lineWarn(group.line) }">{{ group.line.text }}</p>
  </template>
</template>

<script setup>
import { ref } from 'vue'
import { choiceOptions, choiceSelected, lineWarn } from '../utils/pluginChoice.js'
import { listRows, backLabel, openedRow, actionButton } from '../utils/pluginRows.js'

defineProps({
  lines: { type: Array, default: () => [] },
  pluginName: { type: String, default: '' },
  busy: { type: Boolean, default: false },
  radioKey: { type: Number, default: 0 },
})
const emit = defineEmits(['pick', 'run'])

let nextUid = 0
const uid = nextUid++ + Math.random().toString(36).slice(2, 6)   // keeps one list's radio groups apart from another's

const copied = ref('')
const asking = ref('')       // the action whose question is showing
const openIds = ref({})      // the id of the row open in each list, by the list's place among the lines

// Only http(s) links are followed; anything else a plugin sends is inert.
function safeUrl(url) {
  return /^https?:\/\//i.test(url || '') ? url : undefined
}

function validTiles(tiles) {
  return (Array.isArray(tiles) ? tiles : []).filter((t) => t && t.label && t.value)
}

// The plugin's lines as the desktop draws them: consecutive links on one row,
// and lines the desktop would skip (no text, an empty grid, a QR that didn't
// encode, a list with no row) left out.
function groups(lines) {
  const out = []
  for (const l of lines) {
    if (!l || typeof l !== 'object') continue
    if (l.list !== undefined) {
      if (listRows(l).length) out.push({ kind: 'list', line: l })
    } else if (l.qr !== undefined) {
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
    } else if (choiceOptions(l).length) {
      out.push({ kind: 'choice', line: l })
    } else if (actionButton(l)) {
      out.push({ kind: 'action', line: l, button: actionButton(l) })
    } else {
      out.push({ kind: 'text', line: l })
    }
  }
  return out
}

function shown(gi, line) {
  return openedRow(listRows(line), openIds.value[gi])
}
function open(gi, id) {
  openIds.value = { ...openIds.value, [gi]: id }
}
function close(gi) {
  openIds.value = { ...openIds.value, [gi]: '' }
}

function onButton(button) {
  if (button.confirm) asking.value = button.action
  else run(button)
}
function run(button) {
  asking.value = ''
  emit('run', button.action)
}

// Copies as the Plugins section did: the clipboard, or execCommand over plain http.
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
</script>

<style scoped>
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
.text.warn, .choice-cost.warn { color: #e0a83a; }
/* The names, subtitles and cost lines wrap to the room there is and are never cut off. */
.choice { display: flex; flex-direction: column; gap: 0.1rem; margin-top: 0.15rem; }
.choice-row { display: flex; align-items: flex-start; gap: 0.55rem; padding: 0.35rem 0.4rem; border-radius: 4px; cursor: pointer; }
.choice-row:hover { background: #1a1a1a; }
.choice-row input { margin: 0.2rem 0 0; flex: none; accent-color: #4a9eff; }
/* A click leaves focus on the radio, and the browser rings it: that ring is for the keyboard, and the dot alone is the selection. */
.choice-row input:focus:not(:focus-visible) { outline: none; }
.choice-text { display: flex; flex-direction: column; min-width: 0; overflow-wrap: anywhere; white-space: normal; }
.choice-name { color: #eee; }
.choice-subtitle { color: #aaa; font-size: 0.8rem; }
.choice-cost { color: #888; font-size: 0.8rem; }
.tiles-title { font-weight: 600; color: #aaa; font-size: 0.8rem; }
.tiles { display: grid; grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); gap: 0.5rem; }
.tile { background: #161616; border: 1px solid #2c2c2c; border-radius: 6px; padding: 0.5rem 0.6rem; display: flex; flex-direction: column; }
.tile .label { font-size: 0.75rem; color: #888; }
.tile .value { font-size: 1rem; color: #eee; font-weight: 500; }
.tile .detail { font-size: 0.75rem; color: #888; }
.qr img { width: 148px; height: 148px; image-rendering: pixelated; margin-top: 0.3rem; }

.list-block { display: flex; flex-direction: column; gap: 0.45rem; }
.list { display: flex; flex-direction: column; gap: 0.2rem; }
.list-row {
  display: flex; align-items: center; gap: 0.7rem; width: 100%; text-align: left;
  padding: 0.5rem 0.6rem; background: none; border: 1px solid #2c2c2c; border-radius: 6px; color: inherit; font: inherit; cursor: pointer;
}
.list-row:hover { background: #1a1a1a; }
.list-text { display: flex; flex-direction: column; min-width: 0; flex: 1; overflow-wrap: anywhere; }
.list-name { color: #eee; font-weight: 500; }
.list-value { color: #888; font-size: 0.8rem; }
.list-state { font-size: 0.8rem; color: #888; }
.list-state.ok { color: #4caf72; }
.list-state.warn { color: #e0a83a; }
.list-state.bad { color: #e05555; }
.list-title { color: #eee; font-weight: 600; font-size: 1rem; margin: 0; }
.list-block > .list-state { margin: -0.3rem 0 0; }
.chevron { color: #888; font-size: 1.2rem; line-height: 1; }
.dot { width: 8px; height: 8px; border-radius: 50%; flex: none; background: #777; }
.dot.ok { background: #4caf72; }
.dot.warn { background: #e0a83a; }
.dot.bad { background: #e05555; }
.back { align-self: flex-start; }
.ask { display: flex; flex-direction: column; gap: 0.45rem; }
.actions { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; }

button.ghost { background: none; border: 1px solid #333; color: #ccc; border-radius: 4px; cursor: pointer; }
button.danger { background: #b3261e; border: 1px solid #b3261e; color: #fff; border-radius: 4px; cursor: pointer; }
button.small { padding: 0.35rem 0.75rem; font-size: 0.8rem; }
button:disabled { opacity: 0.5; cursor: default; }
</style>
