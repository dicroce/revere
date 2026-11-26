// A plugin's "choice" status line, as the web draws it (the desktop's plugin_status_lines draws the same):
//   { text, warn?, choice: { value, action, options: [{ value, name, subtitle?, cost?, warn? }] } }
// `text` is the one-line summary. The options are a radio list: the name, the subtitle under it and the cost line
// under that, in the warning tone when the option's warn is true. The option whose value is choice.value is the
// selected one; picking another runs the plugin's action: choice.action followed by that option's value.
//
// A choice with no action, or no option with both a value and a name, can't be drawn or acted on: it is left off
// and the text shows alone, as an older revere shows it.

const str = (v) => (typeof v === 'string' ? v : '')

// The options that can be drawn, in order; empty when the line has no usable choice.
export function choiceOptions(line) {
  const c = line && line.choice
  if (!c || typeof c !== 'object' || !str(c.action)) return []
  if (!Array.isArray(c.options)) return []
  return c.options
    .filter((o) => o && typeof o === 'object' && str(o.value) && str(o.name))
    .map((o) => ({ value: o.value, name: o.name, subtitle: str(o.subtitle), cost: str(o.cost), warn: o.warn === true }))
}

export function choiceSelected(line, option) {
  return str(line && line.choice && line.choice.value) === option.value
}

// What picking the option runs (system_plugin_action); null for the option already selected, which changes nothing.
export function choiceAction(line, option) {
  if (choiceSelected(line, option)) return null
  return line.choice.action + option.value
}

export function lineWarn(line) {
  return !!line && line.warn === true
}

// How long after a pick the plugin's lines are read a second time. The pick is queued for the plugin's UI thread, so the
// first read right after can still show the old selection.
export const PICK_RECHECK_MS = 700

// Picks an option and leaves the radio list showing what the plugin says, not what was clicked.
//   run(action)  asks revere to run the plugin's action (throws when it is refused)
//   refresh()    reads the plugin's lines again
//   settle()     draws the rows from those lines again. The browser has already moved the dot to the clicked row, and
//                Vue only rewrites a radio's `checked` when its bound value changes, so a pick that is refused, or not
//                applied yet, would leave the dot on the wrong row; settle() re-creates the rows from the state.
//   wait(ms)     resolves after ms
// Returns { ran: false } for the row already selected, else { ran: true, error } where error is what run threw, if it did.
export async function pickChoice(line, option, { run, refresh, settle, wait }) {
  const action = choiceAction(line, option)
  if (!action) return { ran: false, error: null }
  let error = null
  try {
    await run(action)
  } catch (e) {
    error = e
  }
  const reread = async () => {
    try {
      await refresh()
    } finally {
      settle()
    }
  }
  await reread()
  if (!error) {
    await wait(PICK_RECHECK_MS)
    await reread()
  }
  return { ran: true, error }
}
