// The camera properties page's row of section links: which section is in view, and whether the row changed.

// The section in view, given each section's heading as `{ id, top }` (its distance from the top of the window, in
// page order): the last one that has reached `lead` (the sticky row's height and a little more); the last when the
// page is scrolled to its bottom (a short last section never reaches the top); the first before any has; null when
// there are none.
export function activeSection(headings, lead, atBottom) {
  if (!headings.length) return null
  if (atBottom) return headings[headings.length - 1].id
  let active = headings[0].id
  for (const h of headings) if (h.top <= lead) active = h.id
  return active
}

// Whether two lists of tabs are the same (ids and titles), so re-reading the page after it updates doesn't update it again.
export function sameTabs(a, b) {
  return a.length === b.length && a.every((t, i) => t.id === b[i].id && t.title === b[i].title)
}
