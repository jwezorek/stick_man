# Animation stage 1

Spec: `docs/Animation.md`, limited to the four stage 1 requirements in the request.
All changes stay on the current branch, uncommitted.

- [x] Core: character-owned Default/named poses, root ID, ordered layers and typed actions;
  integer milliseconds, computed duration, JSON/archive round trips, missing-data fallback,
  and preservation through membership undo snapshots.
- [x] Model: undoable asset edits and pose application; animation-mode mutation guard.
- [x] Reusable Qt timeline: rows, items, integer time axis, palette, heads, selection,
  scrolling/zoom, snapping, drag/resize intentions and feedback; no Core dependencies or clock.
- [x] Animation browser: character/group/asset tree, create, rename, duplicate, delete,
  apply/default/base operations, selection-aware buttons and retained expansion.
- [x] Session: detached base-pose rig, preview artwork geometry, canvas banner, disabled
  timeline stub, locked editing and safe teardown on leaving/opening a project.
- [x] Verify Core round trips/legacy fallback, undo and mode guards, timeline interactions,
  browser entry/exit, full existing regression suite and application build.
