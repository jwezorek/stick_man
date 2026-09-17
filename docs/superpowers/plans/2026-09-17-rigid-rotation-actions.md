# Rigid rotation actions

Spec: `docs/Animation.md`, narrowed by the user to rigid rotations with linear timing.
Work in the current branch; do not stage or commit.

- [x] Add deterministic Core rotation evaluation on detached topology. Reset to the base
  pose on each seek, process layers bottom to top and actions in time order, retain completed
  rotations, use `bone::rotate_by`, and report missing/unsupported targets.
- [x] Add an animation-only undo boundary while preserving the topology mutation guard.
- [x] Replace the timeline stub with a controller: action selection and numeric editing,
  root/tip pivot, milliseconds, layer insertion/movement, resize/delete, overlap rejection,
  play/pause/stop and scrubbing. No easing UI or non-linear evaluation.
- [x] Intercept Selection-tool pointer gestures during the session. Drag a bone or its tip
  to author a new rotation at the playhead using the chosen pivot and duration (1000 ms
  initially). Preview provisional data with the same evaluator; commit once on release;
  cancel on Escape, tool switch, seeking or leaving. Existing actions are edited through
  timeline controls. Live gesture-duration recording is deferred in this first pass.
- [x] Keep normal inspectors and structural tools locked, keep Selection/Pan/Zoom usable,
  avoid broadcasting working-rig pointers to project inspectors, and tear down callbacks
  before destroying the working rig.
- [x] Test absolute seeking, completion, root/tip pivots, invalid targets, action undo,
  canvas authoring/cancellation, playback/scrubbing and persistent rig isolation. Build
  the application and run the complete regression suite; inspect rendered UI.
