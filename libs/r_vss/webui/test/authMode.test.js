// Run with: npm test (node's built-in test runner; no extra dependencies).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { authMode, CLAIM_URL } from '../src/utils/authMode.js'

test('an appliance with no password shows the not-set-up page', () => {
  assert.equal(authMode({ password_set: false, appliance: true, local: false }), 'not_set_up')
  // Even on the box itself: the password comes from the cloud, not a form.
  assert.equal(authMode({ password_set: false, appliance: true, local: true }), 'not_set_up')
})

test('an appliance with a password signs in', () => {
  assert.equal(authMode({ password_set: true, appliance: true, local: false }), 'login')
})

test('normal installs are unchanged', () => {
  assert.equal(authMode({ password_set: false, appliance: false, local: true }), 'create')
  assert.equal(authMode({ password_set: true, appliance: false, local: true }), 'login')
  // Older Revere: no appliance field.
  assert.equal(authMode({ password_set: false }), 'create')
  assert.equal(authMode({ password_set: true }), 'login')
})

test('an unreadable status falls back to sign-in', () => {
  assert.equal(authMode(null), 'login')
  assert.equal(authMode({}), 'login')
})

test('the claim link', () => {
  assert.equal(CLAIM_URL, 'https://app.revere.video/claim')
})
