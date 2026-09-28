# Animation 2.0

## Overview

Animation 2.0 models skeletal animation as a sequence of authored poses over a pose domain (a connected, constraint-closed skeletal region), with solver-assisted interpolation between those poses.

The central authoring object is no longer an action. An animation defines:

- a pose domain that it owns;
- a sequence of pose keyframes for that region;
- a duration and interpolation policy between adjacent poses;
- optional positional node tracks that constrain motion between poses; and
- optional artwork-state tracks evaluated on the same animation clock.

At runtime or during editor playback, exact keyframe times use the stored pose directly. Between keyframes, the system interpolates the skeletal state, then uses the nonlinear pose solver to seek positional targets while respecting the skeleton's persistent constraints and favoring the reference pose.

The intended result is a direct-manipulation animation system in which users pose the character at meaningful endpoints and add explicit motion constraints only when the path between those endpoints matters.

---

## Pose Domains

Every animation owns one **pose domain** within one rooted skeleton.

A pose domain is a connected set of skeletal degrees of freedom that is **closed under persistent constraint dependencies**. Given its incoming attachment frame, the domain can be posed and solved without changing any degrees of freedom outside it. Excluded descendants follow their updated attachments while retaining their local poses, unless they are themselves animated.

In the normal case, ownership is expressed as a connected set of bones. Graph connectivity alone is insufficient: persistent constraints that couple skeletal degrees of freedom cannot be split across a domain boundary.

### Domain validity

A valid pose domain obeys these rules:

- **Connected:** its owned bones form one connected region.
- **Constraint-closed:** all degrees of freedom coupled by a persistent constraint belong to the same domain. Closure includes indirect dependencies through other constraints.
- **Fixed incoming frame:** the domain inherits its attachment frame; solving the domain cannot modify that frame.
- **Outgoing attachments:** excluded descendants may follow the domain through forward kinematics (FK). Ordinary parent-child attachment does not itself violate constraint closure.

For example, a rigid triangle cannot be divided between domains. A constraint coupling the rotations of two bones requires the participating degrees of freedom to remain together. An ordinary arm-to-torso attachment may be a domain boundary. A local joint limit measured against the fixed incoming attachment frame does not by itself require ownership of the parent bone; a constraint requiring that parent to participate in the solve does.

A proposed boundary that splits a constraint dependency is invalid. The editor must require a valid domain rather than silently allowing the solver to change unowned degrees of freedom.

A newly created animation defaults to the entire skeleton. Users only need to restrict the region when they want the animation to compose with other animations that control different parts of the character.

Examples include:

- a whole-body walk;
- a legs-only locomotion animation;
- an arm swing;
- a head turn;
- a tail motion.

If one animation is intended to control two branches of a skeleton, the region must also contain the bones connecting those branches. For example, an animation that directly owns both arms must also own the intervening torso/shoulder structure needed to make the region connected.

An animation cannot span two disconnected rooted skeletons. If a character contains multiple disconnected skeletons, each must be animated by a separate animation.

### Editing the domain

The animation editor provides an **Edit Animation Region** command.

The default pose domain is the entire skeleton. Edit Animation Region enters a temporary mode in which the user can restrict or expand the pose domain. The resulting domain must satisfy connectivity and constraint closure.

The pose domain is persistent animation data and is separate from ordinary editor selection. Once the domain has been established, users can freely select individual nodes and bones inside it while authoring poses. References to the animation's region below mean this pose domain.

---

## Animation Composition

Skeletal animations can compose when their pose domains have disjoint ownership and each domain is constraint-closed. Domains are evaluated in upstream-to-downstream attachment order.

For example:

- a legs-only walk can compose with an arm-swing animation;
- a head-turn animation can compose with a lower-body animation;
- two animations that both own the same shoulder or arm bones cannot compose directly.

Composition is based on ownership of skeletal degrees of freedom, not on whether one animation happens to move world-space positions belonging to another animation.

An upstream animation may move or rotate the attachment point of a downstream animated region. The downstream animation inherits that already-evaluated attachment frame and then applies its own animation relative to it.

Persistent constraints must not couple owned degrees of freedom across domain boundaries. Apparently separate regions that are coupled by such constraints cannot be independently animated domains.

These rules intentionally restrict composition. For example, a two-arm animation that must own the connecting torso cannot compose with a separate animation owning that same torso. Overlapping-domain blending is outside this design. Disjoint skeletal ownership also does not resolve conflicts between artwork tracks targeting the same property; artwork composition needs its own policy.

---

## Animation Coordinate Frame

Every animation has an automatically determined coordinate frame. The user does not select a reference frame.

The animation frame is the incoming frame at the root of the animation's connected region, evaluated after any upstream animation has been applied but before the animation itself is evaluated.

This produces two important cases.

### Non-root animation

If the animation does not contain the character root, its frame is the current attachment frame supplied by the parent of the animated region.

For example, an arm animation inherits the current shoulder attachment frame after torso or locomotion animation has been evaluated. A hand trajectory authored for that arm therefore moves naturally with upstream body motion.

A pin in that arm animation is fixed relative to this incoming frame, not to the world. It will move with the shoulder. Keeping a hand fixed against a world-space object while the torso moves requires a domain that includes the relevant upstream motion, or a future feature beyond this frame model.

### Root animation

If the animation contains the character root, the incoming character-root frame becomes the animation space for that animation.

That frame remains fixed while the animation's own root translation or rotation is evaluated. The animation may move the character root within animation space, but it does not move the frame in which its own positional tracks are defined.

This allows behaviors such as planted feet during locomotion: the character root may advance while a foot target remains fixed in the animation's incoming root frame.

The general rule is:

> An animation may inherit movement of its frame from upstream animation, but it never moves its own frame.

---

## Pose Keyframes

The primary authored units of skeletal animation are pose keyframes.

A pose keyframe stores the state of the animation's owned skeletal region at a point in time. Conceptually, this includes:

- local bone rotations;
- any root transform owned by the animation;
- other topology pose state that is semantically part of the skeletal configuration.

Pose keyframes represent endpoint poses, not commands that transform one pose into another.

### Exact keyframe evaluation

At an exact keyframe timestamp, the stored pose is applied directly within the current incoming attachment frame. It is not projected through the transition solver, and transition-local paths or pins do not override it. Exactness refers to the domain's stored skeletal state; its world-space placement still inherits upstream animation.

This rule applies equally to playback, scrubbing, and selecting a pose. A disagreement between transition targets and an endpoint pose may cause a discontinuity at the keyframe. That is an accepted tradeoff: the system does not silently alter an authored keyframe to remove the discontinuity.

The user authors a pose by directly manipulating the skeleton on the canvas. The current IK/constraint solver acts as a posing tool and is allowed to change only the degrees of freedom owned by the active animation region.

### Region-limited manipulation

While editing an animation pose:

- bones inside the animation region are active solver degrees of freedom;
- the incoming attachment frame of the region is supplied by upstream topology and is not changed by the animation;
- excluded subtrees attached to the active region retain their local pose and follow their attachment points normally;
- upstream bones outside the region do not participate in the solve.

This requires the selection/manipulation tool to support domain-limited IK. The solver problem can be constructed from only the pose domain, with its incoming attachment frame fixed. An explicit mask parameter is not required; the requirement is that only owned degrees of freedom participate in the solve.

After applying a stored pose or solving an interpolated pose, excluded descendants are updated through FK while preserving their local poses. If a descendant has its own animation, that animation is then evaluated relative to the updated attachment. This is sufficient because valid domain boundaries do not split coupled constraints.

This manipulation primitive is useful independently of animation, but while editing an animation its allowed region is automatically the current animation domain.

---

## Pose Transitions

Adjacent keyframes define a pose transition.

A transition owns the timing and any additional constraints needed to describe how the character moves between its endpoint poses.

Conceptually a transition contains:

- duration;
- easing/interpolation policy;
- zero or more positional node tracks;
- zero or more pins/contact constraints.

The absolute time of a pose is derived from the durations of preceding transitions. Users primarily edit the duration of the motion from one pose to the next rather than manually assigning every keyframe an absolute timestamp.

---

## Pose Interpolation and Constraint Projection

For a time strictly between two pose keyframes, evaluation proceeds in two stages. Exact keyframe timestamps bypass these stages and use the stored pose directly.

### 1. Reference pose

The system interpolates between the endpoint poses to produce an unconstrained reference pose.

Local/articulation angles are interpolated in skeletal space. Root translation and rotation, when owned by the animation, are interpolated separately.

The initial angle-interpolation rule is shortest-path interpolation. Endpoint orientations alone do not encode winding or a full turn: identical endpoint orientations represent no rotation. Additional intermediate poses can describe larger rotations. Explicit turn counts or action-style rotation commands are not part of the initial model.

The transition's easing curve determines the interpolation parameter.

### 2. Solved pose

The reference pose is then passed to the nonlinear solver.

The solver seeks the active positional targets while maintaining valid skeletal structure and favoring a pose close to the reference. Its inputs include:

- positional node targets active at that time;
- persistent node pins where applicable;
- persistent bone rotation constraints;
- rigid-triangle or other structural constraints;
- animation-region ownership boundaries.

The interpolated pose is therefore a preferred pose rather than a guaranteed final pose.

The solver's objective should continue to favor solutions close to the incoming/reference pose, so unconstrained parts of the region naturally follow the authored interpolation while constrained parts move only as much as needed to satisfy the active requirements.

### Determinism and continuity

For the same authored data and upstream inputs, evaluation must remain deterministic: playback, pausing, and scrubbing to the same time produce the same pose. Each solve starts from the interpolated reference pose, not the previously displayed frame. Upstream state is likewise evaluated for the requested time.

The initial implementation uses independent solves from these reference poses. It does not require the old action evaluator's continuation machinery. Temporal continuity is a quality to assess in practice, not a guarantee implied by determinism: nearby times can select different solver solutions, and exact keyframes can introduce discontinuities. Additional continuity machinery should be considered only if testing demonstrates a need, while preserving deterministic seeking.

Unreachable targets initially use the existing solver's best-effort behavior and result reporting. A positional target is not a promise of exact reachability, and solver failure must not be treated as a successfully satisfied target. Persistent structural constraints and domain ownership remain requirements; transition targets do not authorize changing them.

---

## Positional Node Tracks

A transition may contain positional tracks for one or more nodes.

A node track specifies a desired node position as a function of normalized transition time.

Examples include:

- a hand following a curved path;
- a foot remaining planted;
- a head point following a controlled trajectory;
- several simultaneous effectors describing a coordinated motion.

The solver handles all active positional targets together as a multi-effector constrained pose problem.

### Pins

A pin is the simplest form of positional track: a node target whose position remains constant for some or all of a transition.

Pins and moving effector paths therefore share the same underlying concept: a positional target evaluated over time.

A planted-foot constraint, for example, is a constant target in the animation's automatically determined coordinate frame.

The UI may continue to present pins and paths as distinct authoring operations even though they can share a common Core representation.

---

## Pose Strip

The primary skeletal-animation UI is a **Pose Strip** rather than an action timeline.

The Pose Strip displays the animation as an ordered sequence of pose thumbnails connected by timed transitions.

Each pose thumbnail is a small rendered stick-figure view of the stored keyframe pose. The thumbnail should provide enough whole-character context to make the pose understandable even when the animation owns only a restricted region.

A conceptual layout is:

```text
┌────────┐       400 ms       ┌────────┐       250 ms       ┌────────┐
│ Pose 1 │────────────────────│ Pose 2 │────────────────────│ Pose 3 │
└────────┘                    └────────┘                    └────────┘
```

The horizontal presentation may use transition width to communicate duration, but exact duration remains directly editable numerically.

### Selecting a pose

Selecting a pose:

- places the editor at that keyframe;
- displays the stored pose;
- enables direct pose manipulation;
- shows pose-related properties.

### Selecting a transition

Selecting the area between two poses:

- selects the transition;
- exposes its duration and easing;
- displays positional tracks and pins;
- enables transition-specific path/constraint editing.

Pose editing and transition editing are therefore distinct authoring states.

---

## Creating and Inserting Poses

The Pose Strip provides a control for creating a new pose.

At the end of an animation, **Add Pose** appends a new keyframe. The new pose should normally begin as a copy of the current/end pose so the user can manipulate it into the next desired state.

A pose may also be inserted inside an existing transition.

The editor maintains a playhead for scrubbing and playback. When the playhead is inside a transition, **Insert Pose** creates a new keyframe at that location.

The newly inserted pose is initialized from the fully evaluated animation pose at the playhead time.

The existing transition duration is split proportionally so total animation duration is preserved.

For example:

```text
Pose A -------- 800 ms -------- Pose B
                 ^
              playhead
```

may become:

```text
Pose A --- 300 ms --- New Pose --- 500 ms --- Pose B
```

Any positional tracks, pins, or other transition-local data are divided or preserved across the resulting two transitions as appropriate.

Insertion preserves total duration and captures the evaluated pose at the insertion time. It does **not** promise to preserve the surrounding motion. Splitting easing, introducing a new reference-pose endpoint, and solving the resulting transitions can all change the motion before and after the new pose.

A convenient **Insert at Midpoint** operation may also be provided.

---

## Playback and Scrubbing

The animation editor maintains a single animation clock and playhead.

The playhead is shared by all animation-authoring views.

Users can:

- play and pause the animation;
- scrub to an arbitrary time;
- select exact poses;
- inspect and edit transitions;
- insert a pose at the current playhead location.

The canvas always shows the fully evaluated state at the current animation time, including both topology animation and artwork state.

Playback controls should live outside any tab that presents one particular aspect of the animation.

---

## Artwork Animation

Artwork state is animated on the same animation clock but is not forced to use skeletal pose keyframes.

This is necessary for discrete visual changes that may happen several times during a single skeletal transition, such as:

- eye open → eye half-closed → eye closed → eye half-closed → eye open;
- mouth-shape changes;
- switching a hand or facial sprite;
- toggling a semantic artwork state.

Creating a full skeletal pose keyframe merely to host one of these changes is not required.

### Artwork tracks

An animation may contain discrete artwork-property tracks.

Each track targets a semantic artwork property or sprite binding and contains time-keyed values.

Between keys, a discrete property holds its most recent value.

Conceptually:

```text
Eyes

open            half       closed       half        open
 ●---------------●------------●------------●-----------●
```

The initial implementation can focus on sprite-state changes, while leaving room for additional artwork properties later.

### Timing edits

The policy for artwork keys when skeletal transition durations change is intentionally deferred until the editing workflow can be tried. Keys may remain at absolute times or move with the affected skeletal timing; neither behavior is promised here. Sharing a playback clock does not settle this editing policy.

---

## Animation Editor Tabs

The bottom animation editor is organized as multiple views over the same animation and the same playhead.

At minimum:

- **Poses** — the Pose Strip and skeletal transition authoring;
- **Artwork** — the artwork-property timeline.

The existing timeline widget can be repurposed for the Artwork tab. Its rows represent artwork/property tracks, and its keys represent discrete state changes.

It no longer needs to represent skeletal transformation actions.

The Poses and Artwork tabs are synchronized through the shared animation clock:

- scrubbing either view updates the same playhead;
- playback advances both;
- the canvas renders the combined topology and artwork result.

This allows each kind of animation data to use the visualization best suited to it: pose thumbnails for skeletal animation and a conventional keyed timeline for sparse artwork changes.

---

## Animation Data Model

The exact C++ representation can follow existing Core conventions, but conceptually the model is:

```cpp
struct animation {
    object_id id;
    std::string name;

    pose_domain domain;

    std::vector<pose_keyframe> poses;
    std::vector<pose_transition> transitions;

    std::vector<artwork_track> artwork_tracks;
};
```

with one transition between each adjacent pair of pose keyframes:

```text
pose[0] -> transition[0] -> pose[1]
pose[1] -> transition[1] -> pose[2]
...
```

A transition may conceptually contain:

```cpp
struct pose_transition {
    duration duration;
    easing easing;

    std::vector<node_track> node_tracks;
};
```

where a node track represents either a moving positional target or a constant pin target.

Artwork tracks are keyed against the animation's time axis rather than requiring a one-to-one relationship with pose transitions.

The saved format should preserve semantic authoring data rather than solver caches or transient editor state.

Core remains responsible for domain validation, evaluation, persistence, and persistent-reference integrity. Editor selection and temporary manipulation state must not supply hidden playback inputs. Existing detached-topology editing and undo infrastructure can continue to support this model.

Changes to rig topology or persistent constraints can invalidate a previously valid domain or pose. Such edits and project loading must validate the resulting animation data; they must not silently retain dangling references or permit a domain to solve across an invalid boundary. The precise repair workflow is an implementation decision.

### Deferred policies

This document does not yet specify:

- loop seams and root-motion accumulation across repeated playback;
- behavior outside the authored time range;
- artwork-key retiming after skeletal duration edits and conflicts between composed artwork tracks;
- conversion or compatibility handling for saved action-based animations;
- how existing keyframes are initialized or reconciled when their pose domain expands or shrinks.

These policies should be made explicit as the corresponding features are implemented. They do not change the exact-keyframe, domain-ownership, or deterministic-evaluation rules above.

---

## Intended Authoring Workflow

A typical whole-body animation is authored as follows:

1. Create a new animation.
2. The animation initially owns the entire skeleton.
3. Manipulate the skeleton into the first desired pose.
4. Add another pose.
5. Manipulate the skeleton into the next desired pose.
6. Set the duration and easing of the transition.
7. Add positional paths or pins only where the interpolated motion requires additional control.
8. Continue adding or inserting poses as needed.
9. Use the Artwork tab for sprite-state changes that occur independently of skeletal keyframes.
10. Scrub or play the animation to evaluate the combined result.

For a composable partial-body animation:

1. Create the animation normally.
2. Use **Edit Animation Region** to restrict it to a valid, constraint-closed pose domain.
3. Author poses exactly as for a whole-body animation.
4. During posing and playback, solver manipulation is restricted to the animation-owned region.
5. Compose the animation with other animations whose valid pose domains have disjoint ownership, evaluating upstream domains first.

The overall authoring model remains centered on direct manipulation: users create poses by posing the character, and the solver supplies valid constrained motion between those authored states.

---

## Implementation Status — Animation V2 Phase 3A

Phase 2 implements persistent skeletal pose keyframes and static Pose Strip authoring. [Phase 2.5](animation-phase25.md) adds Pose Strip timing and transport. Phase 3A adds deterministic Core **reference pose** sampling only. The canvas still shows the selected editing pose; playback does not apply samples to it. The **Solved pose** stage, paths/pins, easing authoring, playhead insertion, and canvas animation remain future work.

Retained authoring behavior:

- Each animation owns an ordered sequence of persistent `pose_keyframe` objects with stable IDs and optional names.
- Unnamed keyframes are displayed positionally as `Pose 1`, `Pose 2`, and so on; those generated labels are not persisted.
- Animation keyframes use a skeletal pose representation distinct from standalone named-pose assets. The stored state consists of root-node translations plus local bone rotations, so keyframes are endpoint skeletal state rather than old action-system commands or world-node snapshots.
- The current editor authors the character's full rig: capture stores each root node's world position and each bone's local rotation in radians. Existing assets can have several root entries. The incoming-frame and single-skeleton pose-domain design above remains future work; this phase does not migrate or reinterpret that data.
- One transition record is retained between every adjacent pair of keyframes. Transition identity and duration persist so insertion/deletion/undo keep the sequence structurally consistent. Core sampling uses those durations; transition editing UI is not implemented yet.
- Selecting a keyframe applies its stored skeletal pose directly to the detached Animation Mode topology. No interpolation, transition projection, or playback evaluation occurs.
- Canvas posing of the selected keyframe is captured back into that keyframe through Animation Mode undo commands. Commands identify the owning character/animation/keyframe by stable IDs, so undo after selecting another card restores and selects the frame that was actually edited.
- **Add Pose** captures the current working pose for an empty animation; otherwise it appends a copy of the final keyframe. **Duplicate** inserts a copy after the selected keyframe with a new ID. Rename may set or clear the optional name. Delete supports the final remaining keyframe and selects the next keyframe when possible, otherwise the previous one.
- Pose Strip thumbnails are rendered from temporary posed topologies rather than screenshots of the live canvas. They use a common framing/scale, resolve current character artwork against each stored skeletal pose, honor artwork/bone visibility, and force bones on when no artwork is available.
- **Show previous pose** is editor-only state. When enabled, the preceding authored keyframe is drawn as a faint skeleton beneath the selected pose. It does not wrap from the first keyframe and does not participate in hit testing, selection, solving, or constraints.
- Keyframes, transition records, names, and skeletal pose data round-trip through project persistence. Phase 1 animations containing only `id` and `name` remain valid and load as animations with an empty keyframe sequence.
- Whole-character skeletal-ID remapping updates keyframe root-node and bone references. Thumbnail caches, ghost state, and selection remain derived editor state and are never serialized.

Animation Mode keeps its separate per-session undo stack. Persistent keyframe authoring is committed to document history as one document edit when Animation Mode ends, while individual keyframe operations remain independently undoable/redoable inside the session.

### Phase 3A reference sampling contract

`sm::sample_reference_pose(const animation&, double time_seconds)` returns an
optional `reference_pose_sample`: a copied `skeletal_pose` and tagged location.
`reference_keyframe` identifies the reached keyframe; `reference_transition`
identifies the transition, both endpoint keyframes, and linear normalized progress.
It has no Qt, clock, topology, selection, or previous-sample inputs. Sampling
creates no persistent IDs, undo commands, or project writes.

- Empty sequences return no sample as a normal outcome. A single keyframe is
  returned for every finite time. Finite times outside the sequence clamp to the
  first/final keyframe. Nonfinite requested times throw `std::invalid_argument`,
  including for empty sequences. Clamping is this API's initial local policy,
  not a decision about future looping or root-motion accumulation.
- Keyframe times are cumulative transition-duration sums in sequence order,
  using the same `double` arithmetic as Core total duration and Pose Strip timing.
  Exact equality returns stored scalars directly, including non-normalized angles;
  there is no snapping epsilon. If floating-point addition collapses timestamps,
  the last keyframe at that timestamp wins, as in the Pose Strip.
- Strictly inside a transition, `u = (time - start) / duration`. Every root
  position component interpolates linearly by stable ID. Every local bone angle
  normalizes its endpoints with `normalize_angle`, follows `angular_distance`'s
  shortest signed arc, and normalizes the interior result to `[-pi, pi]`.
  At an exact half-turn the signed normalized endpoint difference decides the
  direction: `0 -> pi` travels positively, `0 -> -pi` negatively (with reversed
  directions on the return paths). Equivalent orientations imply no full turn;
  only floating-point roundoff can remain. No winding, easing, lengths, or scales
  are interpolated.
- Before returning any sample, shared existing local animation checks reject
  malformed transition counts, nonpositive/nonfinite durations, nonfinite total
  duration, nil/duplicate asset IDs, nil skeletal references, and nonfinite pose
  values. Sampling also requires identical root and bone ID sets throughout the
  sequence. All these failures throw `std::invalid_argument`, including when the
  requested time would select an exact/clamped endpoint. Project/load validation
  remains authoritative for rig membership and topology integrity.

An interior **reference pose may violate persistent constraints**. It is not a
finished constraint-valid animation evaluation, and this API neither solves nor
applies it to a topology. Existing constraint enforcement remains unchanged.
No sampling is connected to `time_changed`; Phase 2.5 canvas isolation remains
the user-visible playback boundary.

Core regression coverage in `animation_v2_phase3a` includes cumulative timing,
exact and adjacent endpoints, wrap and half-turn angles, multiple roots/bones,
history/insertion-order independence, malformed input, source immutability, and
serialization round trips. Existing Phase 2/2.5 authoring and canvas-isolation
tests remain in place.
