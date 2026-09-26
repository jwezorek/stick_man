# Animation 2.0

## Overview

Animation 2.0 models skeletal animation as a sequence of authored poses over a connected region of a skeleton, with solver-assisted interpolation between those poses.

The central authoring object is no longer an action. An animation defines:

- a connected skeletal region that it owns;
- a sequence of pose keyframes for that region;
- a duration and interpolation policy between adjacent poses;
- optional positional node tracks that constrain motion between poses; and
- optional artwork-state tracks evaluated on the same animation clock.

At runtime or during editor playback, the system interpolates the skeletal state between adjacent poses, then uses the nonlinear pose solver to find the nearest valid pose satisfying positional targets and the skeleton's persistent constraints.

The intended result is a direct-manipulation animation system in which users pose the character at meaningful endpoints and add explicit motion constraints only when the path between those endpoints matters.

---

## Animation Domain

Every animation owns one connected region of one rooted skeleton.

The region is defined by the skeletal degrees of freedom that the animation is allowed to author and solve. In the normal case this is a connected set of bones.

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

The default animation region is the entire skeleton. Edit Animation Region enters a temporary mode in which the user can restrict or expand the connected region owned by the animation.

The animation region is persistent animation data and is separate from ordinary editor selection. Once the region has been established, users can freely select individual nodes and bones inside it while authoring poses.

---

## Animation Composition

Animations can be evaluated simultaneously when their owned skeletal regions do not overlap.

For example:

- a legs-only walk can compose with an arm-swing animation;
- a head-turn animation can compose with a lower-body animation;
- two animations that both own the same shoulder or arm bones cannot compose directly.

Composition is based on ownership of skeletal degrees of freedom, not on whether one animation happens to move world-space positions belonging to another animation.

An upstream animation may move or rotate the attachment point of a downstream animated region. The downstream animation inherits that already-evaluated attachment frame and then applies its own animation relative to it.

Persistent structural constraints may make apparently separate regions mathematically coupled. Animation-domain validation and solver setup must account for any constraint that crosses a proposed region boundary.

---

## Animation Coordinate Frame

Every animation has an automatically determined coordinate frame. The user does not select a reference frame.

The animation frame is the incoming frame at the root of the animation's connected region, evaluated after any upstream animation has been applied but before the animation itself is evaluated.

This produces two important cases.

### Non-root animation

If the animation does not contain the character root, its frame is the current attachment frame supplied by the parent of the animated region.

For example, an arm animation inherits the current shoulder attachment frame after torso or locomotion animation has been evaluated. A hand trajectory authored for that arm therefore moves naturally with upstream body motion.

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

The user authors a pose by directly manipulating the skeleton on the canvas. The current IK/constraint solver acts as a posing tool and is allowed to change only the degrees of freedom owned by the active animation region.

### Region-limited manipulation

While editing an animation pose:

- bones inside the animation region are active solver degrees of freedom;
- the incoming attachment frame of the region is supplied by upstream topology and is not changed by the animation;
- excluded subtrees attached to the active region retain their local pose and follow their attachment points normally;
- upstream bones outside the region do not participate in the solve.

This requires the selection/manipulation tool to support region-limited IK. A dragged node may be solved using only a prescribed connected set of skeletal degrees of freedom rather than allowing IK to propagate through the entire skeleton.

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

For a time between two pose keyframes, evaluation proceeds in two stages.

### 1. Reference pose

The system interpolates between the endpoint poses to produce an unconstrained reference pose.

Local/articulation angles are interpolated in skeletal space. Root translation and rotation, when owned by the animation, are interpolated separately.

The transition's easing curve determines the interpolation parameter.

### 2. Solved pose

The reference pose is then passed to the nonlinear solver.

The solver finds the closest valid pose satisfying:

- positional node targets active at that time;
- persistent node pins where applicable;
- persistent bone rotation constraints;
- rigid-triangle or other structural constraints;
- animation-region ownership boundaries.

The interpolated pose is therefore a preferred pose rather than a guaranteed final pose.

The solver's objective should continue to favor solutions close to the incoming/reference pose, so unconstrained parts of the region naturally follow the authored interpolation while constrained parts move only as much as needed to satisfy the active requirements.

Evaluation must remain deterministic so playback, pausing, and scrubbing to the same time produce the same pose.

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

    animation_region region;

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
2. Use **Edit Animation Region** to restrict it to a connected skeletal region.
3. Author poses exactly as for a whole-body animation.
4. During posing and playback, solver manipulation is restricted to the animation-owned region.
5. Compose the animation with other animations whose owned regions do not overlap.

The overall authoring model remains centered on direct manipulation: users create poses by posing the character, and the solver supplies valid constrained motion between those authored states.
