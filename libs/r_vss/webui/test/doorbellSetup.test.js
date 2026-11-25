// Run with: npm test (node's built-in test runner; no extra dependencies).
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { pressSourceChoice, pressTestLines, pressTestVerdict, mailStatusLines, clockText, inGroupsOf4, mailSetupError, MAIL_SENDER } from '../src/utils/doorbellSetup.js'

test('the press source is sent for a doorbell once chosen, never for a camera or to an older revere', () => {
  assert.deepEqual(pressSourceChoice({ press_source: 'onvif' }, true, 'onvif'), {})
  assert.deepEqual(pressSourceChoice({ press_source: 'onvif' }, true, 'reolink_email'), { press_source: 'reolink_email' })
  assert.deepEqual(pressSourceChoice({ press_source: 'reolink_email' }, true, 'reolink_email'), { press_source: 'reolink_email' })
  assert.deepEqual(pressSourceChoice({ press_source: 'reolink_email' }, true, 'onvif'), { press_source: 'onvif' })
  assert.deepEqual(pressSourceChoice({ press_source: 'onvif' }, false, 'reolink_email'), {})
  assert.deepEqual(pressSourceChoice({}, true, 'reolink_email'), {})
  assert.deepEqual(pressSourceChoice(null, true, 'reolink_email'), {})
})

test("the press test's lines: at once, how late, not yet while it runs, no once it's over", () => {
  const sources = [{ source: 'onvif', seen: true, after_ms: 300 }, { source: 'reolink_email', seen: false }]
  assert.deepEqual(pressTestLines({ open: true, sources }), ['ONVIF: yes, at once', 'Email: not yet'])
  assert.deepEqual(pressTestLines({ open: false, sources: [{ source: 'onvif', seen: false }, { source: 'reolink_email', seen: true, after_ms: 14400 }] }),
    ['ONVIF: no', 'Email: yes, 14 s after'])
  assert.deepEqual(pressTestLines(null), [])
})

test("the test's verdict, once it's over", () => {
  assert.equal(pressTestVerdict(null, 'onvif'), '')
  assert.equal(pressTestVerdict({ open: true, sources: [{ source: 'onvif', seen: true }] }, 'onvif'), '')
  const onvif = { open: false, sources: [{ source: 'onvif', seen: true }] }
  assert.match(pressTestVerdict(onvif, 'onvif'), /at once/)
  assert.match(pressTestVerdict(onvif, 'reolink_email'), /choose ONVIF/)
  assert.match(pressTestVerdict({ open: false, sources: [{ source: 'onvif', seen: false }, { source: 'reolink_email', seen: true }] }, 'reolink_email'), /keep Reolink email/)
  assert.match(pressTestVerdict({ open: false, sources: [{ source: 'onvif', seen: false }] }, 'onvif'), /choose Reolink email/)
  assert.match(pressTestVerdict({ open: false, sources: [{ source: 'reolink_email', seen: false }] }, 'reolink_email'), /Check the email settings/)
})

test("the listener's status, a line at a time", () => {
  assert.deepEqual(mailStatusLines({}, { press_source: 'onvif' }, 'onvif'), [])
  assert.match(mailStatusLines({}, { press_source: 'onvif' }, 'reolink_email')[0], /^Save/)
  const saved = { press_source: 'reolink_email', last_press_ms: { reolink_email: null } }
  assert.match(mailStatusLines({ listening: false }, saved, 'reolink_email')[0], /within a few seconds/)
  assert.match(mailStatusLines({ listening: false, last_error: 'the port is in use' }, saved, 'reolink_email')[0], /the port is in use/)
  assert.match(mailStatusLines({ listening: true }, saved, 'reolink_email')[0], /No ring by email yet/)
  const rung = { press_source: 'reolink_email', last_press_ms: { reolink_email: 1764018345000 } }
  const lines = mailStatusLines({ listening: true, other_mail: 1, bad_logins: 2 }, rung, 'reolink_email')
  assert.equal(lines[0], `Last ring by email: ${clockText(1764018345000)}.`)
  assert.match(lines[1], /^1 email that wasn't a ring.*Ring only/)
  assert.match(lines[2], /refused 2 times/)
  assert.match(clockText(1764018345000), /^\d\d:\d\d:\d\d$/)
})

test('a password shown in groups of 4, and a real sender address', () => {
  assert.equal(inGroupsOf4('Xw7tQ2mLp9RzV4nBc8Hd'), 'Xw7t Q2mL p9Rz V4nB c8Hd')
  assert.equal(inGroupsOf4('abcde'), 'abcd e')
  assert.equal(inGroupsOf4('abcd'), 'abcd')
  assert.equal(inGroupsOf4(''), '')
  assert.match(MAIL_SENDER, /^[^@ ]+@[^@ ]+$/)
})

test("the email setup's error says what went wrong", () => {
  assert.match(mailSetupError(new Error('Request failed (404)')), /no network address/)
  assert.match(mailSetupError(new Error('Not a doorbell.')), /no network address/)
  assert.match(mailSetupError(new Error('Session expired — please log in again.')), /Sign in again/)
  assert.match(mailSetupError(new TypeError('Failed to fetch')), /Couldn't reach Revere/)
  assert.equal(mailSetupError(new Error('Request failed (500)')), "Couldn't read the email setup: Request failed (500)")
})
