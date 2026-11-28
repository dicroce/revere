// Run with: npm test (node's built-in test runner; no extra dependencies).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { listRows, rowTone, backLabel, openedRow, actionButton } from '../src/utils/pluginRows.js'

const line = {
  list: {
    back: 'Apple Home',
    rows: [
      { id: 'a', name: 'Front Door', value: 'Live', state: 'In Apple Home', tone: 'ok', detail: [{ text: 'Front Door detail' }] },
      { id: 'b', name: 'Garage', value: 'Off', state: 'Not added yet', tone: 'warn', detail: [{ text: 'Garage detail', action: 'reset:b', action_label: 'Remove' }] },
    ],
  },
}

test('each row with an id and a name is drawn, in order, with the words it was sent', () => {
  const rows = listRows(line)
  assert.deepEqual(rows.map((r) => r.id), ['a', 'b'])
  assert.deepEqual(rows[0], { id: 'a', name: 'Front Door', value: 'Live', state: 'In Apple Home', tone: 'ok', detail: [{ text: 'Front Door detail' }] })
  assert.equal(backLabel(line), 'Apple Home')
})

test('rows that cannot be drawn are left out, and a repeated id is drawn once', () => {
  const rows = listRows({ list: { rows: [{ id: 'a' }, { name: 'no id' }, 3, null, { id: 'b', name: 'B', detail: 'text' }, { id: 'b', name: 'B again' }] } })
  assert.deepEqual(rows.map((r) => [r.id, r.name]), [['b', 'B']])
  assert.deepEqual(rows[0].detail, [])
  assert.equal(rows[0].value, '')
  assert.equal(rows[0].tone, 'neutral')
})

test('no usable list is no rows', () => {
  assert.deepEqual(listRows({ list: { rows: [] } }), [])
  assert.deepEqual(listRows({ list: { rows: 'a' } }), [])
  assert.deepEqual(listRows({ list: [] }), [])
  assert.deepEqual(listRows({ text: 'x' }), [])
  assert.deepEqual(listRows(null), [])
})

test('a tone is ok, warn or bad, and anything else is neutral', () => {
  assert.deepEqual(['ok', 'warn', 'bad', 'green', '', undefined, 3].map(rowTone), ['ok', 'warn', 'bad', 'neutral', 'neutral', 'neutral', 'neutral'])
  assert.equal(backLabel({ list: { rows: [] } }), 'Back')
})

test('the open row is the one with the id; one that went away shows the list', () => {
  const rows = listRows(line)
  assert.equal(openedRow(rows, 'b').name, 'Garage')
  assert.equal(openedRow(rows, 'gone'), null)
  assert.equal(openedRow(rows, ''), null)
  assert.equal(openedRow(rows, null), null)
})

test('a button needs both an action and its label; the question is optional', () => {
  assert.deepEqual(actionButton({ text: 't', action: 'a', action_label: 'Go' }), { action: 'a', label: 'Go', confirm: '' })
  assert.deepEqual(actionButton({ text: 't', action: 'a', action_label: 'Go', confirm: 'Sure?' }).confirm, 'Sure?')
  assert.equal(actionButton({ text: 't', action: 'a' }), null)
  assert.equal(actionButton({ text: 't', action_label: 'Go' }), null)
  assert.equal(actionButton({ text: 't', action: 3, action_label: true }), null)
  assert.equal(actionButton(null), null)
})
