// A doorbell's press setup in the camera properties (the Doorbell section): how it reports a press, what to type in a
// Reolink doorbell's app, and its press test ("Start test"). Worded as the desktop's (camera_properties_state.h).

// What a save sends for the press source (/update_camera_properties' "press_source"): kept once it's chosen, onvif
// being what an unset one means; nothing for a camera that isn't a doorbell, or from an older revere (no press_source
// in /cameras).
export function pressSourceChoice(camera, isDoorbell, source) {
  if (!camera || camera.press_source === undefined || !isDoorbell) return {}
  return source !== camera.press_source || source !== 'onvif' ? { press_source: source } : {}
}

// The sender's email to type in the Reolink app, when revere doesn't say (GET /doorbell_mail_setup's sender): an address
// on a domain of ours, which the app takes (it may refuse .local). The listener takes any.
export const MAIL_SENDER = 'doorbell@revere.video'

// The rows to type into the Reolink app (Settings › Email Alerts › Email Settings), named and ordered as the app has
// them: it has no user field (it logs in with the sender's email, and the listener takes any user with the password).
export function mailSetupRows(mail) {
  if (!mail) return []
  return [
    { key: 'server', name: 'SMTP server', value: mail.server, copy: true },
    { key: 'port', name: 'SMTP port', value: String(mail.port), copy: true },
    { key: 'tls', name: 'SSL or TLS', value: 'On' },
    { key: 'sender', name: "Sender's email", value: mail.sender || MAIL_SENDER, copy: true },
    { key: 'password', name: 'Password', value: mail.password, copy: true, secret: true },
    { key: 'recipient', name: 'Recipient', value: mail.recipient, copy: true },
  ]
}

// The email setup after one of its reads ends: the answer, or with have_login (the read left the login out) the answer
// with the password already shown. A read that began before New password was pressed (or before another camera was
// opened) is stale: its answer, or its failure, changes nothing, so an old password can't come back over a new one.
// failed: the read threw, which forgets a setup only when it was the read that would have shown the login.
export function mailSetupAfterRead(current, answer, { haveLogin, stale, failed = false }) {
  if (stale) return current
  if (failed) return haveLogin ? current : null
  return haveLogin ? { ...answer, password: current?.password } : answer
}

// The password as the row shows it: exactly as it is, all its characters together, as Copy copies it (a space or dash
// added for reading would be typed into the doorbell's app too); hidden until shown.
export const passwordShown = (password, reveal) => (reveal ? password || '' : '••••••••••••')

// Why the email setup couldn't be read, from the error api.js throws.
export function mailSetupError(e) {
  const msg = String(e?.message || e || '')
  if (/\(404\)|not a doorbell/i.test(msg)) return "This computer doesn't have an address the doorbell can reach yet, or this camera isn't a doorbell."
  if (/session expired|\(401\)/i.test(msg)) return 'Sign in again to see the email setup.'
  if (/failed to fetch|network|load failed/i.test(msg)) return "Couldn't reach Revere to read the email setup."
  return `Couldn't read the email setup: ${msg}`
}

export const pressSourceName = (source) => (source === 'reolink_email' ? 'Email' : 'ONVIF')

// A time as this browser shows it, "13:16:35".
export function clockText(ms) {
  const d = new Date(ms)
  const two = (n) => String(n).padStart(2, '0')
  return `${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`
}

// How far off a doorbell's clock may be before setup says so: its email's times would be off by that much.
export const CLOCK_OFF_MS = 3000

// "The doorbell's clock is 9 seconds slow. Turn on time sync (NTP) in the Reolink app.", or empty while the clock isn't
// known (GET /doorbell_presses' clock_offset_ms null) or is near enough.
export function clockOffLine(offsetMs) {
  if (offsetMs === null || offsetMs === undefined || Math.abs(offsetMs) < CLOCK_OFF_MS) return ''
  const secs = Math.round(Math.abs(offsetMs) / 1000)
  return `The doorbell's clock is ${secs} seconds ${offsetMs < 0 ? 'slow' : 'fast'}. Turn on time sync (NTP) in the Reolink app.`
}

// The press test's lines, one per source (GET /doorbell_presses' test): "ONVIF: arrived right away", "Email: arrived
// after 14 seconds", "ONVIF: didn't arrive", or while it runs "Email: not yet".
export function pressTestLines(test, clockOffsetMs = null) {
  if (!test) return []
  const lines = (test.sources || []).map((s) => {
    const name = pressSourceName(s.source)
    if (s.seen && s.after_ms < 1000) return `${name}: arrived right away`
    if (s.seen) {
      const secs = Math.round(s.after_ms / 1000)
      return `${name}: arrived after ${secs} ${secs === 1 ? 'second' : 'seconds'}`
    }
    return `${name}: ${test.open ? 'not yet' : "didn't arrive"}`
  })
  // An email that wasn't a press came during the test: the doorbell's Visitor alert is off, or it was the app's Test email.
  if (test.other_mail) lines.push("Got an email from the doorbell, but it wasn't a press.")
  const clock = clockOffLine(clockOffsetMs)
  if (clock) lines.push(clock)
  return lines
}

// What a finished test means for the choice; empty while it runs or before one.
export function pressTestVerdict(test, chosen) {
  if (!test || test.open) return ''
  const seen = (test.sources || []).filter((s) => s.seen)
  if (seen.some((s) => s.source === 'onvif')) {
    return chosen === 'onvif'
      ? 'The doorbell sent the press over ONVIF. Rings will arrive right away.'
      : 'The doorbell sent the press over ONVIF. Choose ONVIF so rings arrive right away.'
  }
  if (seen.length) return 'The doorbell only emailed the press. Keep Reolink email.'
  return chosen === 'onvif'
    ? 'No press arrived. If this is a Reolink doorbell, choose Reolink email and set it up.'
    : 'No press arrived. Check the email settings in the Reolink app, then test again.'
}

// How the doorbell's email is getting through (GET /doorbell_mail_setup and /doorbell_presses), a line at a time.
export function mailStatusLines(mail, presses, chosen) {
  if (chosen !== 'reolink_email') return []
  const out = []
  const saved = presses?.press_source
  const lastEmail = presses?.last_press_ms?.reolink_email
  if (saved !== 'reolink_email') out.push("Save to start receiving this doorbell's email.")
  else if (!mail?.listening) {
    out.push(mail?.last_error ? `Revere can't receive the doorbell's email: ${mail.last_error}` : "Revere is getting ready to receive the doorbell's email. This takes a few seconds.")
  } else if (lastEmail) out.push(`The doorbell last emailed a press at ${clockText(lastEmail)}.`)
  else out.push("Ready. The doorbell hasn't emailed a press yet. Press it once to check.")
  const other = mail?.other_mail || 0
  if (other) {
    out.push(`The doorbell sent ${other} ${other === 1 ? "email that wasn't a press" : "emails that weren't presses"}. In the Reolink app, set the email Alarm Schedule to Ring only.`)
  }
  const bad = mail?.bad_logins || 0
  if (bad) out.push(`The doorbell's email login failed ${bad} ${bad === 1 ? 'time' : 'times'}. Check the password in the Reolink app.`)
  const lockedMs = mail?.locked_for_ms || 0
  if (lockedMs > 0) {
    const mins = Math.ceil(lockedMs / 60000)
    out.push(`Too many wrong passwords from the doorbell. Locked for ${mins} more ${mins === 1 ? 'minute' : 'minutes'}.`)
  }
  return out
}
