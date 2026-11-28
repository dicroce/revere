// A plugin's "list" status line, as the web draws it (the desktop's plugin_status_lines draws the same):
//   { list: { back?, rows: [{ id, name, value?, state?, tone?, detail?: [lines] }] } }
// Each row is one line of the list: the name over what it's set to, its state on the right (in its tone), and a
// chevron. Picking a row shows its detail, a list of lines drawn as the plugin's own are, under a Back button;
// Back, or the row going away, shows the list again.
//
// A row with no id or no name, or one whose id came before, is left out; a list with no row can't be drawn.

const str = (v) => (typeof v === 'string' ? v : '')

// The rows that can be drawn, in order; empty when the line has no usable list.
export function listRows(line) {
  const l = line && line.list
  if (!l || typeof l !== 'object' || !Array.isArray(l.rows)) return []
  const seen = new Set()
  const out = []
  for (const r of l.rows) {
    if (!r || typeof r !== 'object' || !str(r.id) || !str(r.name) || seen.has(r.id)) continue
    seen.add(r.id)
    out.push({
      id: r.id,
      name: r.name,
      value: str(r.value),
      state: str(r.state),
      tone: rowTone(r.tone),
      detail: Array.isArray(r.detail) ? r.detail : [],
    })
  }
  return out
}

// "ok", "warn" or "bad"; anything else is neutral.
export function rowTone(tone) {
  return tone === 'ok' || tone === 'warn' || tone === 'bad' ? tone : 'neutral'
}

export function backLabel(line) {
  return str(line && line.list && line.list.back) || 'Back'
}

// The row with the id that's open, or null: no id, or a row that's no longer there, shows the list.
export function openedRow(rows, openId) {
  if (!openId) return null
  return rows.find((r) => r.id === openId) || null
}

// A line's button, when it has one: an action and a label for it, and the question asked first, if any.
export function actionButton(line) {
  if (!line || !str(line.action) || !str(line.action_label)) return null
  return { action: line.action, label: line.action_label, confirm: str(line.confirm) }
}
