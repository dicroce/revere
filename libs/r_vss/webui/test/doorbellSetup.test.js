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

test("the press test's lines: right away, how late, not yet while it runs, didn't arrive once it's over", () => {
  const sources = [{ source: 'onvif', seen: true, after_ms: 300 }, { source: 'reolink_email', seen: false }]
  assert.deepEqual(pressTestLines({ open: true, sources }), ['ONVIF: arrived right away', 'Email: not yet'])
  assert.deepEqual(pressTestLines({ open: false, sources: [{ source: 'onvif', seen: false }, { source: 'reolink_email', seen: true, after_ms: 14400 }] }),
    ["ONVIF: didn't arrive", 'Email: arrived after 14 seconds'])
  assert.deepEqual(pressTestLines({ open: false, sources: [{ source: 'reolink_email', seen: true, after_ms: 1200 }] }), ['Email: arrived after 1 second'])
  assert.deepEqual(pressTestLines(null), [])
})

test("the test's verdict, once it's over", () => {
  assert.equal(pressTestVerdict(null, 'onvif'), '')
  assert.equal(pressTestVerdict({ open: true, sources: [{ source: 'onvif', seen: true }] }, 'onvif'), '')
  const onvif = { open: false, sources: [{ source: 'onvif', seen: true }] }
  assert.equal(pressTestVerdict(onvif, 'onvif'), 'The doorbell sent the press over ONVIF. Rings will arrive right away.')
  assert.equal(pressTestVerdict(onvif, 'reolink_email'), 'The doorbell sent the press over ONVIF. Choose ONVIF so rings arrive right away.')
  assert.equal(pressTestVerdict({ open: false, sources: [{ source: 'onvif', seen: false }, { source: 'reolink_email', seen: true }] }, 'reolink_email'), 'The doorbell only emailed the press. Keep Reolink email.')
  assert.equal(pressTestVerdict({ open: false, sources: [{ source: 'onvif', seen: false }] }, 'onvif'), 'No press arrived. If this is a Reolink doorbell, choose Reolink email and set it up.')
  assert.equal(pressTestVerdict({ open: false, sources: [{ source: 'reolink_email', seen: false }] }, 'reolink_email'), 'No press arrived. Check the email settings in the Reolink app, then test again.')
})

test("the doorbell's email status, a line at a time", () => {
  assert.deepEqual(mailStatusLines({}, { press_source: 'onvif' }, 'onvif'), [])
  assert.equal(mailStatusLines({}, { press_source: 'onvif' }, 'reolink_email')[0], "Save to start receiving this doorbell's email.")
  const saved = { press_source: 'reolink_email', last_press_ms: { reolink_email: null } }
  assert.equal(mailStatusLines({ listening: false }, saved, 'reolink_email')[0], "Revere is getting ready to receive the doorbell's email. This takes a few seconds.")
  assert.equal(mailStatusLines({ listening: false, last_error: 'the port is in use' }, saved, 'reolink_email')[0], "Revere can't receive the doorbell's email: the port is in use")
  assert.equal(mailStatusLines({ listening: true }, saved, 'reolink_email')[0], "Ready. The doorbell hasn't emailed a press yet. Press it once to check.")
  const rung = { press_source: 'reolink_email', last_press_ms: { reolink_email: 1764018345000 } }
  const lines = mailStatusLines({ listening: true, other_mail: 1, bad_logins: 2 }, rung, 'reolink_email')
  assert.equal(lines[0], `The doorbell last emailed a press at ${clockText(1764018345000)}.`)
  assert.equal(lines[1], "The doorbell sent 1 email that wasn't a press. In the Reolink app, set the email Alarm Schedule to Ring only.")
  assert.equal(lines[2], "The doorbell's email login failed 2 times. Check the user name and password in the Reolink app.")
  const more = mailStatusLines({ listening: true, other_mail: 3, bad_logins: 1 }, rung, 'reolink_email')
  assert.match(more[1], /sent 3 emails that weren't presses\./)
  assert.match(more[2], /failed 1 time\./)
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
  assert.equal(mailSetupError(new Error('Request failed (404)')), "This computer doesn't have an address the doorbell can reach yet, or this camera isn't a doorbell.")
  assert.equal(mailSetupError(new Error('Not a doorbell.')), "This computer doesn't have an address the doorbell can reach yet, or this camera isn't a doorbell.")
  assert.match(mailSetupError(new Error('Session expired — please log in again.')), /Sign in again/)
  assert.match(mailSetupError(new TypeError('Failed to fetch')), /Couldn't reach Revere/)
  assert.equal(mailSetupError(new Error('Request failed (500)')), "Couldn't read the email setup: Request failed (500)")
})
