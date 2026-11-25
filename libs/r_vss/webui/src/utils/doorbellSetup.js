// A doorbell's press setup in the camera properties (the Doorbell section): how it reports a press, what to type in a
// Reolink doorbell's app, and "Press the doorbell now". Worded as the desktop's (camera_properties_state.h).

// What a save sends for the press source (/update_camera_properties' "press_source"): kept once it's chosen, onvif
// being what an unset one means; nothing for a camera that isn't a doorbell, or from an older revere (no press_source
// in /cameras).
export function pressSourceChoice(camera, isDoorbell, source) {
  if (!camera || camera.press_source === undefined || !isDoorbell) return {}
  return source !== camera.press_source || source !== 'onvif' ? { press_source: source } : {}
}

export const pressSourceName = (source) => (source === 'reolink_email' ? 'Email' : 'ONVIF')

// A time as this browser shows it, "13:16:35".
export function clockText(ms) {
  const d = new Date(ms)
  const two = (n) => String(n).padStart(2, '0')
  return `${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`
}

// The press test's lines, one per source (GET /doorbell_presses' test): "ONVIF: yes, at once", "Email: yes, 14 s
// after", "ONVIF: no", or while it runs "Email: not yet".
export function pressTestLines(test) {
  if (!test) return []
  return (test.sources || []).map((s) => {
    const name = pressSourceName(s.source)
    if (s.seen) return `${name}: ${s.after_ms < 1000 ? 'yes, at once' : `yes, ${Math.round(s.after_ms / 1000)} s after`}`
    return `${name}: ${test.open ? 'not yet' : 'no'}`
  })
}

// What a finished test means for the choice; empty while it runs or before one.
export function pressTestVerdict(test, chosen) {
  if (!test || test.open) return ''
  const seen = (test.sources || []).filter((s) => s.seen)
  if (seen.some((s) => s.source === 'onvif')) {
    return chosen === 'onvif'
      ? 'This doorbell reports its presses over ONVIF: rings come at once.'
      : 'This doorbell reported the press over ONVIF: choose ONVIF for rings at once.'
  }
  if (seen.length) return 'Only the email reported the press: keep Reolink email.'
  return chosen === 'onvif'
    ? 'Nothing reported the press. If this is a Reolink doorbell, choose Reolink email and set it up.'
    : 'Nothing reported the press. Check the email settings in the Reolink app, then try again.'
}

// How the email listener is doing for this doorbell (GET /doorbell_mail_setup and /doorbell_presses), a line at a time.
export function mailStatusLines(mail, presses, chosen) {
  if (chosen !== 'reolink_email') return []
  const out = []
  const saved = presses?.press_source
  const lastEmail = presses?.last_press_ms?.reolink_email
  if (saved !== 'reolink_email') out.push("Save, and Revere starts listening for this doorbell's email.")
  else if (!mail?.listening) {
    out.push(mail?.last_error ? `Revere can't listen for its email: ${mail.last_error}` : 'Revere starts listening for its email within a few seconds.')
  } else if (lastEmail) out.push(`Last ring by email: ${clockText(lastEmail)}.`)
  else out.push('Listening. No ring by email yet: press the doorbell once.')
  const other = mail?.other_mail || 0
  if (other) {
    out.push(`${other} ${other === 1 ? "email that wasn't a ring" : "emails that weren't rings"} arrived: in the Reolink app, set the email Alarm Schedule to Ring only.`)
  }
  const bad = mail?.bad_logins || 0
  if (bad) out.push(`Its login was refused ${bad} ${bad === 1 ? 'time' : 'times'}: check the user and password in the Reolink app.`)
  return out
}
