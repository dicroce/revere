// Run with: npm test (node's built-in test runner; no extra dependencies).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { doorbellChoice } from '../src/utils/cameraKind.js'

test('a switch left as the model suggests sends nothing', () => {
  assert.deepEqual(doorbellChoice({ kind: 'doorbell', kind_setting: null }, true), {})
  assert.deepEqual(doorbellChoice({ kind: 'camera', kind_setting: null }, false), {})
})

test('a switch changed from the suggestion is sent', () => {
  assert.deepEqual(doorbellChoice({ kind: 'camera', kind_setting: null }, true), { kind: 'doorbell' })
  assert.deepEqual(doorbellChoice({ kind: 'doorbell', kind_setting: null }, false), { kind: 'camera' })
})

test('once chosen, the choice is always sent', () => {
  assert.deepEqual(doorbellChoice({ kind: 'doorbell', kind_setting: 'doorbell' }, true), { kind: 'doorbell' })
  assert.deepEqual(doorbellChoice({ kind: 'camera', kind_setting: 'camera' }, false), { kind: 'camera' })
  assert.deepEqual(doorbellChoice({ kind: 'doorbell', kind_setting: 'doorbell' }, false), { kind: 'camera' })
})

test('an older revere, with no kind, gets nothing', () => {
  assert.deepEqual(doorbellChoice({}, true), {})
  assert.deepEqual(doorbellChoice(null, true), {})
})
