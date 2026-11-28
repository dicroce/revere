// Whether the camera properties have edits that aren't saved, so leaving the Config tab (another tab, signing out)
// can ask first. ManageTab sets it; App reads it.
import { ref } from 'vue'

export const unsaved = ref(false)

// Runs `go` at once, or after the caller has asked "Discard changes?" when there are unsaved edits: the caller shows
// the question for `pending` and calls it on Discard.
export function leaveOrAsk(go, pending) {
  if (!unsaved.value) {
    go()
    return
  }
  pending.value = go
}
