import { test } from 'node:test'
import assert from 'node:assert/strict'
import { activeSection, sameTabs, isScrollKey } from '../src/utils/propsSections.js'

const page = (...tops) => tops.map((top, i) => ({ id: `s${i}`, top }))

test('no sections: none in view', () => {
  assert.equal(activeSection([], 60, false), null)
  assert.equal(activeSection([], 60, true), null)
})

test('the first section before any has reached the top, then each as it does', () => {
  assert.equal(activeSection(page(120, 500, 900), 60, false), 's0')
  assert.equal(activeSection(page(40, 420, 820), 60, false), 's0')
  assert.equal(activeSection(page(-300, 60, 460), 60, false), 's1')
  assert.equal(activeSection(page(-300, 61, 460), 60, false), 's0')
  assert.equal(activeSection(page(-900, -500, -20), 60, false), 's2')
})

test('scrolled to the bottom, the last section is in view though it never reached the top', () => {
  assert.equal(activeSection(page(-500, -100, 300), 60, true), 's2')
  assert.equal(activeSection(page(-500, -100, 300), 60, false), 's1')
})

test('the same tabs are the same ids and titles in the same order', () => {
  const t = [{ id: 'a', title: 'Recording' }, { id: 'b', title: 'Detection' }]
  assert.equal(sameTabs(t, [{ id: 'a', title: 'Recording' }, { id: 'b', title: 'Detection' }]), true)
  assert.equal(sameTabs(t, [t[0]]), false)
  assert.equal(sameTabs(t, [t[1], t[0]]), false)
  assert.equal(sameTabs(t, [{ id: 'a', title: 'Recording' }, { id: 'b', title: 'Doorbell' }]), false)
  assert.equal(sameTabs([], []), true)
})

test('the keys that scroll the page', () => {
  for (const k of ['ArrowUp', 'ArrowDown', 'PageUp', 'PageDown', 'Home', 'End', ' ']) assert.equal(isScrollKey(k), true, k)
  for (const k of ['Tab', 'Enter', 'a', 'Shift', 'ArrowLeft', 'Escape']) assert.equal(isScrollKey(k), false, k)
})
