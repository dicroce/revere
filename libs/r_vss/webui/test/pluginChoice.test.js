// Run with: npm test (node's built-in test runner; no extra dependencies).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { choiceOptions, choiceAction, choiceSelected, lineWarn, pickChoice, PICK_RECHECK_MS } from '../src/utils/pluginChoice.js'

const line = {
  text: 'Porch: Live',
  choice: {
    value: 'live',
    action: 'mode:porch:',
    options: [
      { value: 'off', name: 'Off', subtitle: 'Not in Apple Home', cost: '', warn: false },
      { value: 'live', name: 'Live', subtitle: 'Live view and snapshots' },
      { value: 'apple_recording', name: 'Live + Apple alerts + Apple recording', subtitle: 'Clips in Apple Home', cost: 'May be paused at busy times', warn: true },
    ],
  },
}

test('every option with a value and a name is a row, in order, with the words it was sent', () => {
  const rows = choiceOptions(line)
  assert.deepEqual(rows.map((o) => o.value), ['off', 'live', 'apple_recording'])
  assert.deepEqual(rows[2], { value: 'apple_recording', name: 'Live + Apple alerts + Apple recording', subtitle: 'Clips in Apple Home', cost: 'May be paused at busy times', warn: true })
  // Words left out are empty, and warn is only ever true when it was sent as true.
  assert.deepEqual(rows[1], { value: 'live', name: 'Live', subtitle: 'Live view and snapshots', cost: '', warn: false })
  assert.equal(choiceOptions({ choice: { action: 'a:', options: [{ value: 'x', name: 'X', warn: 'yes' }] } })[0].warn, false)
})

test('the option whose value is the choice\'s is the selected one', () => {
  const rows = choiceOptions(line)
  assert.deepEqual(rows.map((o) => choiceSelected(line, o)), [false, true, false])
})

test('picking a row runs the choice\'s action followed by its value; the selected row runs nothing', () => {
  const [off, live, rec] = choiceOptions(line)
  assert.equal(choiceAction(line, off), 'mode:porch:off')
  assert.equal(choiceAction(line, rec), 'mode:porch:apple_recording')
  assert.equal(choiceAction(line, live), null)
})

test('a choice that cannot be drawn or acted on is left off, and the text shows alone', () => {
  assert.deepEqual(choiceOptions({ text: 't' }), [])
  assert.deepEqual(choiceOptions(null), [])
  assert.deepEqual(choiceOptions({ choice: { value: 'a', options: [{ value: 'a', name: 'A' }] } }), [])              // no action
  assert.deepEqual(choiceOptions({ choice: { action: 'm:', options: [] } }), [])
  assert.deepEqual(choiceOptions({ choice: { action: 'm:' } }), [])
  assert.deepEqual(choiceOptions({ choice: { action: 'm:', options: 'x' } }), [])
  assert.deepEqual(choiceOptions({ choice: { action: 3, options: [{ value: 'a', name: 'A' }] } }), [])
  // Options with no value or no name, or that are not objects, are skipped without dropping the rest.
  assert.deepEqual(choiceOptions({ choice: { action: 'm:', options: [null, 'x', { name: 'A' }, { value: 'b' }, { value: 'c', name: 'C' }] } }).map((o) => o.value), ['c'])
})

test('a line is in the warning tone only when it says warn: true', () => {
  assert.equal(lineWarn({ text: 't', warn: true }), true)
  assert.equal(lineWarn({ text: 't', warn: 'true' }), false)
  assert.equal(lineWarn({ text: 't' }), false)
  assert.equal(lineWarn(undefined), false)
})

// What pickChoice did, in order.
function harness({ runError = null, refreshError = null } = {}) {
  const calls = []
  return {
    calls,
    hooks: {
      run: async (action) => { calls.push(`run ${action}`); if (runError) throw runError },
      refresh: async () => { calls.push('refresh'); if (refreshError) throw refreshError },
      settle: () => { calls.push('settle') },
      wait: async (ms) => { calls.push(`wait ${ms}`) },
    },
  }
}

test('a pick that goes through: the action, the plugin read again with the rows drawn from it, and once more after a moment', async () => {
  const h = harness()
  const [off] = choiceOptions(line)
  const r = await pickChoice(line, off, h.hooks)
  assert.deepEqual(r, { ran: true, error: null })
  assert.deepEqual(h.calls, ['run mode:porch:off', 'refresh', 'settle', `wait ${PICK_RECHECK_MS}`, 'refresh', 'settle'])
})

test('a pick the plugin refuses puts the dot back: the rows are drawn from its lines again, and the error is handed back', async () => {
  const refused = new Error('That plugin isn\'t offering that action.')
  const h = harness({ runError: refused })
  const [off] = choiceOptions(line)
  const r = await pickChoice(line, off, h.hooks)
  assert.equal(r.ran, true)
  assert.equal(r.error, refused)
  // The browser had moved the dot to the clicked row: the rows are redrawn from the plugin's value after it is read.
  assert.deepEqual(h.calls, ['run mode:porch:off', 'refresh', 'settle'])
})

test('rows are redrawn even when reading the plugin fails', async () => {
  const h = harness({ refreshError: new Error('offline') })
  const [off] = choiceOptions(line)
  await assert.rejects(pickChoice(line, off, h.hooks), /offline/)
  assert.deepEqual(h.calls, ['run mode:porch:off', 'refresh', 'settle'])
})

test('the selected row asks for nothing', async () => {
  const h = harness()
  const [, live] = choiceOptions(line)
  assert.deepEqual(await pickChoice(line, live, h.hooks), { ran: false, error: null })
  assert.deepEqual(h.calls, [])
})
