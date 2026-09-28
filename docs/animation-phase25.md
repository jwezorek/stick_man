# Phase 2.5: Pose Strip timing

This document records the historical Phase 2.5 boundary. **Phase 3C supersedes
the stationary-canvas behavior below**: explicit transport preview now displays
Core constrained skeletal samples at the same time as the Pose Strip. The
selected editing topology, editing selection, stored keyframes, and thumbnail
sources remain isolated. See [current implementation status](animation.md#implementation-status--animation-v2-phase-3c)
for the preview lifecycle, read-only behavior, and failure handling. Scrubbing
and artwork-state animation remain deferred.

The Animation Editor now plays the Pose Strip's timing. The canvas continues to
show the selected editing pose. Playback does not select keyframes, apply poses,
evaluate actions, solve IK, or update artwork on the canvas.

- **Play / Pause** starts, freezes, and resumes the clock. Play at the end restarts.
- **|<** stops and returns to the beginning; **>|** stops at the final pose.
- Playback stops at the final keyframe without looping.
- The time display measures only the sum of Core transition durations.
- The selection border identifies the editing pose. A tinted background, inner
  outline, and top accent identify the most recently reached playback pose.
- The existing Rename action edits an optional name without changing the ID.
  Unnamed cards derive `Pose N` from their current sequence position.

`ui::animation_playback` owns a monotonic `QElapsedTimer` and a 16 ms Qt timer.
Timer events sample elapsed time; they never accumulate nominal timer intervals.
Its `time_changed` signal is the extension point for later animation evaluation.
The clock contains no model or canvas reference and is not serialized.

`pose_strip_layout` derives cumulative rectangles and keyframe times from Core.
Transitions normally use 160 pixels per second, with a 12-pixel minimum. Cards
consume no time. At a keyframe instant, the playhead jumps to the card's right
edge and the entire card becomes current. Only animations that exceed Qt's
absolute widget-size limit reduce the pixel scale; the viewport never compresses
the sequence. Duration tooltips retain the exact stored value, while inline
labels use concise formatting and disappear when space is insufficient.

The editor compares ordered keyframe IDs and transition IDs/durations on model
refresh. A structural or timing change stops and resets the clock, including
changes made by undo/redo. Selection, renaming, and thumbnail refresh alone do
not reset it. Entering/leaving an animation resets transient playback state.
Scrolling reveals the clipped playhead or newly reached card with normal Qt
edge scrolling and defers while the user is dragging the scrollbar.

The existing Core insertion/deletion policies remain unchanged: duplication
inserts a default transition before the duplicate, preserving the old outgoing
transition toward its destination; deletion removes the incoming transition
(the outgoing transition for the first pose). Core's ordered transition vector
remains authoritative. Phase 2 has no keyframe reorder UI to extend here.

Verification covers geometry boundaries, minimum-width timing, names and IDs,
serialization, transition associations, sequence changes, pause/resume, event
loop stalls, completion, scrolling, and unchanged canvas/project state. The
broader animation design in `animation.md` still describes future canvas
evaluation and scrubbing; those are not implemented by this phase.
