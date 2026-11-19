// Which sign-in screen to show, from GET /auth_status
// ({ password_set, appliance, local }; older Revere sends only password_set).
//
//   'not_set_up' — an appliance with no local password yet: it's set up from
//                  the Revere app (scan the sticker), never here.
//   'create'     — first run on a normal install: create the local password.
//   'login'      — sign in. On an appliance the password is managed in the
//                  Revere app, so there is nothing to change here.
export const CLAIM_URL = 'https://app.revere.video/claim'

export function authMode(status) {
  const passwordSet = status?.password_set !== false
  const appliance = status?.appliance === true
  if (appliance && !passwordSet) return 'not_set_up'
  if (!passwordSet) return 'create'
  return 'login'
}
