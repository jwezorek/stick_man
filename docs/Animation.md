# stick_man Animation System Design

**Version:** 1  
**Scope:** Core animation model, poses, evaluation semantics, reusable timeline control, and editor interaction

## 1. Overview

`stick_man` animation is based on a timeline of **actions** rather than conventional per-property keyframe tracks.

An animation consists of ordered animation layers containing timed actions. Each action:

- begins at a particular time;
- has a duration;
- has an action type;
- has type-specific parameters;
- has an easing function.

Examples of actions include:

- rigidly rotating a bone around its root or tip;
- rigidly rotating a node or portion of a skeleton around another node;
- performing an IK rotation;
- moving an IK target along a path.

Actions correspond closely to manipulations that can already be performed interactively with the editor's selection/manipulation tools.

The animation timeline looks broadly like a Gantt chart. Actions appear as colored rectangles whose horizontal position represents start time and whose width represents duration.

Unlike a traditional animation editor, animation layers do **not** correspond to bones, nodes, properties, or other character objects.

Layers instead provide:

1. a place for actions whose time intervals overlap; and
2. an explicit ordering for composing actions whose effects interact.

The editor also supports **named poses**. A pose is a static snapshot of a character's rig state and is distinct from an animation.

The goal is a compact graphical representation of animation as a sequence and composition of meaningful operations rather than a large collection of low-level property curves.

---

## 2. Design Goals

The animation system should:

- model animation in terms of meaningful skeleton operations;
- support overlapping actions;
- produce deterministic results when seeking directly to any time;
- avoid frame-by-frame incremental integration;
- allow animation-layer ordering to affect the result where appropriate;
- make IK and other procedural manipulation first-class animation operations;
- use the canvas as the primary way to author geometric action parameters;
- allow exact editing through action-specific controls;
- support per-action easing;
- make animation creation feel like directly manipulating and performing the character;
- reuse the same manipulation semantics used for ordinary character editing;
- support reusable named poses;
- provide a persistent place to manage poses and animations independently of the timeline editor;
- keep the large timeline UI hidden when the user is not actively editing an animation;
- separate the reusable timeline widget from stick_man animation semantics;
- tolerate later rig editing in a predictable, inspectable way;
- remain entirely usable by `stick_man` Core and editor without external runtime dependencies.

---

## 3. High-Level Model

Conceptually:

```text
character
    default pose
    named poses
        pose
        pose
        ...

    animations
        animation
            base pose
            ordered animation layers
                layer
                    timed action
                    timed action
                    ...
                layer
                    timed action
                    ...
```

A possible data model is:

```cpp
struct pose
{
    object_id id;
    std::string name;
    // pose state keyed by persistent rig object IDs
};

struct animation
{
    object_id id;
    std::string name;
    object_id base_pose;
    std::vector<animation_layer> layers;
};

struct animation_layer
{
    std::vector<animation_action> actions;
};

struct animation_action
{
    duration start;
    duration duration;
    easing easing;
    action_data data;
};
```

The exact C++ representation is not decided by this document.

Action-specific data will likely be represented by a variant or equivalent value-semantic type.

Actions and poses refer to rig objects using stable object IDs rather than display names.

Poses and animations belong to a character rather than existing as unassociated project-wide assets.

A character also has a designated **character root node**. For an ordinary single-skeleton character this defaults to that skeleton's root node. A multi-skeleton character still designates one node as the character root. The character root provides the stable origin used by animation target-reference modes.

---

## 4. Named Poses

A **pose** is a named snapshot of a character's rig state.

Examples might include:

```text
Standing
Crouched
Arms Up
Pointing
Ready
```

A pose is not an animation of zero duration and should not be represented as one.

Conceptually:

```text
Pose
    static rig state

Animation
    base pose + ordered timed actions
```

Named poses are useful independently of animation and provide the starting state for animations.

### 4.1 Character Default Pose

When one or more skeletons are made into a character, their current positions become that character's initial **Default** pose.

Thus character creation conceptually performs:

```text
current skeleton state
        |
        v
create character
        |
        +--> capture Default pose
```

The Default pose is a built-in pose belonging to the character.

It should appear in the character's `Poses` section in the Animation pane.

The Default pose:

- always exists while the character exists;
- cannot simply be deleted;
- is the base pose selected automatically for new animations;
- can be explicitly replaced from the character's current pose through an operation such as `Set Current as Default Pose`.

Creating additional named poses does not alter the Default pose.

### 4.2 Pose operations

The editor should support at least:

- New Pose from Current;
- Apply Pose;
- Rename Pose;
- Duplicate Pose;
- Delete Pose;
- Set Current as Default Pose.

The built-in Default pose cannot be deleted.

Applying a pose during ordinary editing changes the character's current rig state and should participate in normal undo/redo.

Changing the currently displayed rig after applying a named pose does **not** automatically update the saved pose. Updating an existing pose should be an explicit operation.

### 4.3 Animation base poses

Each animation has a base pose.

New animations initially use the character's Default pose.

The user may later choose another named pose as the animation's base.

Conceptually:

```text
Animation: Wave
Base Pose: Standing
```

Evaluation at any time begins from that base pose.

If a named pose is used as the base of one or more animations, deleting that pose is a dependent operation. The editor should not silently leave dangling base-pose references.

A deletion workflow can offer to:

- reassign affected animations to the character's Default pose; or
- cancel the deletion.

---

## 5. Animation Layers

Animation layers have semantic ordering.

They are **not** merely rectangle-packing lanes.

Consider:

```text
             0        1        2        3

Layer 3            [ IK hand ------------ ]

Layer 2      [ rotate forearm ]

Layer 1      [ rotate torso ---------------- ]
```

The animation evaluator processes layers in a defined order.

The convention is:

> **Animation layers are evaluated from bottom to top.**

Operations on higher layers are therefore applied to the pose produced by lower layers.

Changing an action's layer can change the resulting animation.

For example:

```text
rotate torso
then
IK hand to target
```

does not necessarily produce the same pose as:

```text
IK hand to target
then
rotate torso
```

This is expected and intentional.

### 5.1 Actions within a layer

Actions within one animation layer cannot overlap in time.

For example:

```text
[ rotate arm ][ rotate hand ][ rotate finger ]
```

requires only one layer.

These actions form an implicit temporal sequence.

There is no need for the user to construct an explicit sequence object. Their positions in the timeline already express the sequence.

### 5.2 Overlapping actions

Actions on different layers may overlap freely:

```text
Layer 2         [ move hand with IK ----------- ]

Layer 1    [ rotate torso ---------------------- ]
```

Layer ordering determines the order in which these operations are applied.

---

## 6. Absolute-Time Evaluation

Animations must be directly evaluable at an arbitrary time.

Seeking directly to:

```text
2.713 seconds
```

must not require simulating every earlier frame.

Evaluation begins from the animation's base pose and reconstructs the pose at the requested time.

For an action:

```text
before its start:
    it contributes nothing

while active:
    it contributes its effect at normalized progress u

after completion:
    it contributes its final effect
```

where:

```text
u = (time - start) / duration
```

clamped to:

```text
0 <= u <= 1
```

Completed actions continue to affect later poses.

Otherwise a completed rotation would disappear merely because the playhead had moved beyond its rectangle.

### 6.1 Layer evaluation

Each layer behaves conceptually like a sequence.

At time `t`:

- actions that have not begun contribute nothing;
- completed actions contribute their completed result;
- at most one action in the layer is currently active and contributes its partial result.

The layer is applied to its input pose.

Layers are composed from bottom to top:

```text
base pose
    |
    v
evaluate Layer 1 at t
    |
    v
evaluate Layer 2 at t
    |
    v
evaluate Layer 3 at t
    |
    v
final pose
```

This makes evaluation deterministic while preserving meaningful ordering between interacting operations.

---

## 7. Action Types

The initial action vocabulary should reflect the kinds of skeleton manipulation already supported by the editor.

The exact taxonomy may change as the current Selection-tool operations are factored into reusable manipulation primitives.

### 7.1 Rigid Bone Rotation

Rigidly rotate a bone and the appropriate affected structure around one of the bone's endpoints.

Parameters include at least:

```text
bone
pivot = root | tip
rotation amount
```

At normalized progress `u`:

```text
applied rotation =
    final rotation * eased(u)
```

The affected geometry should match the corresponding interactive manipulation behavior.

---

## 8. Rigid Node Rotation

Rotate selected skeleton geometry around another node.

Conceptually:

```text
subject
pivot node
rotation amount
```

This should use the same operation available through direct manipulation rather than introducing an animation-specific version.

The exact target representation remains to be determined.

---

## 9. IK Rotation

An IK rotation action applies the appropriate inverse-kinematics operation over time.

Its exact parameterization has not yet been settled.

The important requirement is:

> Animation IK uses the same underlying solver and manipulation semantics as interactive IK editing.

There should not be a second animation-only implementation of IK.

---

## 10. IK Translation

IK translation moves an effector toward a target while solving the skeleton according to the existing IK rules, including pinned nodes where applicable.

The action describes the **target motion**, not interpolated joint angles.

At 50% progress, the system does not put every affected joint halfway between its initial and final angles.

Instead:

```text
1. determine the target-path position at 50% progress;
2. resolve that point through the action's target reference frame;
3. solve IK toward the resulting target.
```

This lets the result naturally respond to actions on lower animation layers while keeping the authored target semantics explicit.

### 10.1 Target reference frame

The coordinate frame used by an IK translation action is a property of the action.

The initial implementation supports three target-reference modes:

```text
character_start
character_root
node
```

#### `character_start`

Path points are stored relative to the character root's position at the **start of the animation**.

The origin is captured from the animation base pose and remains fixed for the entire animation evaluation.

Conceptually:

```text
animation_origin = base-pose position of character root
world_target = animation_origin + path_point
```

This expresses a target fixed in the animation's stage while avoiding arbitrary project-space coordinates.

If lower-layer actions later move the character root, a `character_start` target does not move with it.

#### `character_root`

Path points are stored relative to the character root's **current evaluated position in the pose entering the action**.

Conceptually:

```text
world_target = input-pose character-root position + path_point
```

If lower-layer actions move the character root, the target moves with it.

#### `node`

Path points are stored relative to the current evaluated position of a specified node in the pose entering the action.

Conceptually:

```text
world_target = input-pose reference-node position + path_point
```

This initial node-relative mode is translation-relative only. A node supplies an origin but not an oriented local coordinate frame.

A future bone-relative target mode may provide both translation and orientation if a concrete use case requires it.

### 10.2 Character root

Every character has a designated character root node.

For a single-skeleton character, the character root defaults to the root node of that skeleton.

For a character containing multiple disconnected skeletons, one node is still designated as the character root so the character has one stable animation-space origin.

The character root is persistent semantic character data, not editor/session state.

A character with a non-empty rig must always have a valid character root. Rig-editing operations that remove the designated root must therefore establish another valid root as part of the structural edit.

### 10.3 Target path type

Target reference frame and path geometry are independent properties.

The initial IK translation action should support at least:

```text
Line
Cubic Bézier
Spline
```

A spline is a smooth series of connected cubic Bézier segments.

Conceptually, action data may use a value-semantic variant such as:

```cpp
using target_path = std::variant<
    line_path,
    cubic_bezier_path,
    spline_path>;
```

The semantic distinction between path types should be retained rather than forcing every path into a cubic representation solely for implementation convenience.

A new action normally begins with a line path. If the action is created by a direct-manipulation gesture, its first point is initialized from the effector's current evaluated location expressed in the selected target frame, and its final point is initialized from the completed gesture.

This makes newly authored actions continuous by default without making continuity a semantic invariant. Later edits to earlier actions or layers are allowed to make the beginning of a path discontinuous; the system does not silently rewrite authored path data to conceal that change.

### 10.4 Path evaluation and easing

Easing and geometric path shape are separate concepts.

Conceptually:

```text
u = normalized action time
s = easing(u)
path_point = path.position_at_progress(s)
world_target = resolve(target_frame, path_point)
solve IK toward world_target
```

For curved paths, normalized progress should represent normalized distance along the path rather than the raw polynomial parameter. An approximately arc-length-parameterized lookup is sufficient.

This means Linear easing has approximately constant target speed regardless of whether the target path is a line, cubic Bézier, or multi-segment spline.

### 10.5 Editing the path

The path should primarily be edited directly on the canvas.

When an IK translation action is selected, the editor displays geometry appropriate to its path type.

For example:

```text
Line:
    start point
    end point

Cubic Bézier:
    start point
    end point
    two control handles

Spline:
    knots
    segment control handles
```

The target-reference mode and, for `node`, the reference node are editable action properties.

Exact numeric coordinates and path parameters may also be available through the action editor.

## 11. Easing

Every action has an easing function.

Easing is purely a remapping of normalized action time:

```text
u = clamp(
    (time - action.start) / action.duration,
    0,
    1)

u = easing(u)

action.evaluate(pose, u)
```

Easing knows nothing about the action type.

For example:

```text
rigid rotation:
    angle = total_angle * easing(u)
```

and:

```text
IK translation:
    target = resolve(frame, path.position_at_progress(easing(u)))
```

The target path controls **where** the target travels.

The target reference frame controls **what those path coordinates are relative to**.

The easing function controls **how progress through that path changes with time**.

These are separate concepts.

### 11.1 Initial easing choices

The initial editor should provide a small useful set:

```text
Linear
Ease In
Ease Out
Ease In/Out
Smoothstep
```

The mathematical definitions should be fixed and unit-tested.

Custom easing curves can be added later if needed.

---

# Editor UI

## 12. Persistent Animation Pane

The editor has a persistent **Animation pane** used to manage the poses and animations belonging to characters.

This pane exists independently of Animation Mode.

It is the normal entry point for creating, selecting, renaming, deleting, duplicating, and opening animation assets.

The pane should be implemented as a tree view.

A representative structure is:

```text
Animation
────────────────────────────────

▼ Alice                         [character icon]
    ▼ Poses                     [poses-group icon]
        Default                 [default-pose icon]
        Standing                [pose icon]
        Crouched                [pose icon]
        Pointing                [pose icon]

    ▼ Animations                [animations-group icon]
        Idle                    [animation icon]
        Walk                    [animation icon]
        Wave                    [animation icon]

▼ Robot                         [character icon]
    ▼ Poses
        Default
        Neutral

    ▼ Animations
        Idle
        Look Around
```

Loose skeletons that do not belong to characters do not appear as top-level items.

### 12.1 Tree item types and icons

The pane should use distinct custom icons for:

- Character;
- Poses group;
- Default pose;
- ordinary pose;
- Animations group;
- animation.

Pose and animation items should therefore remain visually distinguishable even when their grouping is obvious from the tree.

Character and group items are structural browser nodes rather than editable animation assets.

### 12.2 Pane buttons

The pane should initially provide three primary buttons:

```text
[ + Pose ]   [ + Animation ]   [ Delete ]
```

The exact icon/button treatment can follow the rest of the editor.

`Delete` is useful and common enough to deserve a visible control rather than being available only through context menus.

Its enabled state depends on the current selection.

It should normally be disabled when:

- nothing is selected;
- a Character node is selected;
- the `Poses` group node is selected;
- the `Animations` group node is selected;
- the built-in Default pose is selected.

### 12.3 Creating a pose

`+ Pose` creates a new pose from the current state of the relevant character.

The new item appears under:

```text
Character
    Poses
        Pose 1
```

and immediately enters inline rename mode.

The browser should choose the character from:

1. the owner of the currently selected pose/animation/group item, when applicable; otherwise
2. the currently selected character in the editor.

If no unambiguous character is available, the operation should be disabled or ask the user to select one rather than guessing.

### 12.4 Creating an animation

`+ Animation` creates a new animation belonging to the relevant character.

Its base pose initially defaults to that character's Default pose.

The new item appears as:

```text
Character
    Animations
        Animation 1
```

and immediately enters inline rename mode.

After creation, the editor should ask whether the user wants to edit it immediately.

For example:

```text
Edit "Animation 1" now?

Editing opens the Animation Timeline
and enters Animation Mode.

[ Edit Animation ] [ Not Now ]
```

The user is therefore not forced into Animation Mode merely because an animation asset was created.

### 12.5 Inline name editing

Pose and animation names should be editable directly in the tree.

The expected editing mechanisms are:

- F2;
- context menu → Rename;
- normal tree-view inline editing behavior where appropriate.

Creation should begin inline editing automatically.

A dedicated Rename button is unnecessary.

### 12.6 Animation context menu

Animation items should initially expose:

```text
Edit Animation
────────────────
Rename
Duplicate
Delete
```

`Edit Animation` enters Animation Mode for that animation.

Double-clicking an animation should be equivalent to `Edit Animation`.

If an animation is currently being edited, its tree item should show a persistent indication independent of tree selection, such as:

- bold text;
- a small overlay icon; or
- another subtle active-editor indicator.

### 12.7 Pose context menu

Ordinary pose items should expose:

```text
Apply Pose
────────────────
Set as Animation Base...
Rename
Duplicate
Delete
```

The exact `Set as Animation Base...` workflow may be simplified later if changing an animation's base pose is more naturally done from the animation's properties.

Double-clicking a pose should apply it to the character.

Applying a pose is an ordinary editable operation and participates in undo/redo.

The Default pose can expose:

```text
Apply Pose
Update Default from Current
```

plus any applicable non-destructive operations.

### 12.8 Duplicate

Duplicate is useful for both poses and animations but does not initially require a dedicated pane button.

Duplicating a pose creates an independent copy.

Duplicating an animation creates an independent animation with the same:

- base pose;
- layers;
- actions;
- timings;
- easing;
- action parameters.

### 12.9 Delete behavior

Deleting an ordinary pose or animation should participate in undo/redo.

Routine deletion does not need a generic confirmation dialog merely to ask "Are you sure?"

However, deletion that affects dependencies should be explicit.

For example, deleting a pose that is used as an animation base should explain which animations depend on it and offer to reassign those animations to Default.

### 12.10 Tree ordering

The order of pose and animation items in this browser has no animation semantics.

Creation order is sufficient initially.

Sorting can be added later.

Dragging items within the browser should not silently modify animation evaluation order.

### 12.11 Expanded state

The browser should preserve ordinary expanded/collapsed state during editing.

Creating a new item should expand its parent path automatically if necessary so the newly created item is immediately visible.

### 12.12 What the Animation pane does not do

The persistent Animation pane is an **asset browser and manager**.

It does not need:

- playback controls;
- a playhead;
- animation layers;
- action rectangles;
- recording controls.

Those belong to the Animation Timeline shown while editing an animation.

---

## 13. Animation Mode

Animation authoring occurs in a distinct **Animation Mode**.

Animation Mode is not itself a canvas tool.

The normal non-structural editor tools continue to exist within Animation Mode:

- Selection;
- Pan;
- other existing non-structural tools as appropriate.

Structural rig editing is disabled while Animation Mode is active.

In particular, Animation Mode does not allow operations such as:

- adding or deleting nodes;
- adding or deleting bones;
- splitting or merging skeletons;
- changing skeleton membership in a character;
- otherwise changing the active character's rig topology.

The topology visible to an Animation Mode session is fixed at the topology that existed when the mode was entered.

### 13.1 Persistent character versus working rig

Animation preview and procedural evaluation must never pose or otherwise mutate the real project-backed rig.

Entering Animation Mode creates a **detached working copy of the active character's rig topology**. The working copy preserves the same persistent skeleton, node, and bone IDs as the source rig but exists outside the real project's object registry and undo model.

Conceptually:

```text
sm::project
    |
    +-- persistent character / rig
    |       persistent authored state
    |
    +-- enter Animation Mode
            |
            v
       detached working topology
       same semantic object IDs
       private mutable pose state
```

The working copy is not a second project character. It is temporary evaluation state owned by the Animation Mode session.

The current Core representation does not require `sm::character` itself to become copyable. The session may construct independent scratch topology by copying the character's skeletons into a detached `sm::topology` while preserving IDs.

Editor-only pointers or `user_data` carried by project objects are not semantic animation state and must not be copied as meaningful state into the working topology.

### 13.2 Base-pose initialization and reevaluation

After the working topology is created, its pose is immediately replaced by the active animation's base pose.

The pose that happened to be displayed in ordinary editing before Animation Mode was entered is not used as the animation's starting pose and does not need to be saved or restored by the animation system.

For every playhead evaluation, the working rig is conceptually reset to the animation base pose and then the animation is evaluated at the requested absolute time.

The working topology itself may be allocated once for the session; reevaluation does not require reconstructing topology on every frame.

A future extension may permit animations without explicit base poses. That is intentionally outside the initial design.

### 13.3 Tool commit semantics

The same Selection-tool gesture has different persistence semantics inside and outside Animation Mode.

Normal editing:

```text
Selection tool gesture
    -> manipulate project-backed rig
    -> commit ordinary project geometry/pose change
    -> normal undo/redo
```

Animation Mode:

```text
Selection tool gesture
    -> manipulate detached working rig
    -> create or edit animation action data
    -> commit only the animation-data edit to the project
```

The transient node/bone changes made while previewing, scrubbing, solving IK, or performing the gesture are **not project mutations and are never undoable**.

The animation-data edit produced by the gesture is persistent authored data and participates in normal undo/redo.

This requires the Selection tool's manipulation behavior to be separable from its ordinary project-commit behavior. Animation Mode may reuse the same manipulation primitives, but references or pointers to working-topology objects must never be passed to normal `mdl::project` geometry-mutation APIs.

Persistent communication between the working session and animation data is through stable `object_id` values.

### 13.4 Canvas binding and identity

While Animation Mode is active, canvas node/bone/skeleton items for the active character should bind to the corresponding objects in the detached working topology rather than to the real project topology.

This is preferable to scattering special "preview position" branches throughout every canvas item.

Conceptually:

```text
Normal editing:
    active-character canvas items -> project topology

Animation Mode:
    active-character canvas items -> working topology
```

Because a project node and its working-copy counterpart are different C++ objects with the same semantic ID, any editor state that must survive the mode boundary should use persistent IDs rather than pointer identity.

This includes, as applicable:

- selected node/bone identity;
- action effector IDs;
- pin IDs;
- reference-node IDs;
- character-root ID.

Canvas items and active tool gestures that reference working objects must be destroyed or rebound before the Animation Mode session destroys its working topology.

### 13.5 Artwork during animation preview

Artwork remains persistent character data; it does not need to be duplicated into the Animation Mode session merely to preview animation.

During Animation Mode:

```text
persistent character
    -> artwork resources, appearances, bindings, draw order

working topology
    -> current evaluated bone/node geometry used by those bindings
```

Artwork rendering must therefore resolve binding/resource semantics from the real character while obtaining current geometric transforms from the working topology.

Any Core/editor artwork API that currently assumes that artwork transforms must be calculated from the project-backed topology will need an interface that can evaluate those transforms against the working topology instead.

### 13.6 Other project objects

Only the active character's working topology is manipulable in Animation Mode.

Other characters or loose skeletons may remain visible if useful, but they are read-only for the duration of the mode and cannot become targets of animation-authoring gestures.

---

## 14. Entering Animation Mode

Animation Mode is entered by opening an animation.

Typical entry paths are:

- double-click an animation in the Animation pane;
- animation context menu → `Edit Animation`;
- choose `Edit Animation` after creating a new animation.

Entering Animation Mode:

1. makes the animation active;
2. selects or activates its owning character;
3. creates the detached working topology for that character while preserving persistent rig IDs;
4. binds the active character's canvas presentation to the working topology;
5. shows the Animation Timeline pane;
6. places the playhead at the start;
7. applies the animation's base pose to the working rig and evaluates the animation at that time;
8. renders character artwork using persistent artwork data and working-rig geometry;
9. shows the Animation Mode canvas banner.

There does not need to be a separate global "Animation Mode" tool button.

Opening an animation is the normal way of entering the mode.

---

## 15. Animation Mode Canvas Banner

While Animation Mode is active, the top of the canvas displays a clearly visible but compact banner.

For example:

```text
┌─────────────────────────────────────────────────────────┐
│ Animation Mode — Alice / Wave       [Leave Animation]  │
└─────────────────────────────────────────────────────────┘
```

The exact presentation should follow the editor's normal styling.

The banner should:

- state clearly that Animation Mode is active;
- identify the active character;
- identify the active animation;
- contain an obvious control for leaving Animation Mode.

This is important because the Selection tool has different commit semantics while animation editing is active.

The mode should not be indicated solely through the presence of the timeline.

### 15.1 Leaving Animation Mode

The banner provides a `Leave Animation` or `Leave Animation Mode` control.

Leaving the mode:

1. stops playback if active;
2. ends or cancels any provisional animation-authoring gesture;
3. clears canvas/tool objects that reference the working topology;
4. destroys the detached Animation Mode working topology/session;
5. rebinds/rebuilds the canvas from the real project-backed topology;
6. closes/hides the Animation Timeline pane;
7. removes animation-specific canvas overlays;
8. returns Selection-tool operations to normal editing semantics.

The real project character's ordinary editable pose reappears automatically because Animation Mode never changed it.

There is therefore no pose-restoration command and no undo record associated with entering, previewing, scrubbing, playing, or leaving Animation Mode.

Leaving Animation Mode does not delete or discard the animation.

---

# Reusable Timeline Control

## 16. Timeline Widget Architectural Boundary

The Gantt-like timeline itself should be implemented as a reusable custom control separate from its use by the animation system.

The timeline widget should know about:

- time;
- rows;
- timed rectangles;
- labels;
- palette colors;
- selection;
- a head;
- a row head;
- dragging/resizing;
- scrolling and zooming.

It should know nothing about:

- characters;
- poses;
- bones;
- nodes;
- IK;
- animation actions;
- animation evaluation;
- animation layers as a domain concept.

The Animation Timeline pane adapts stick_man animation data to the generic timeline widget.

Within the generic widget, `row` or `lane` is acceptable terminology.

Within the animation model and user-facing animation UI, these rows are called **animation layers**.

---

## 17. Timeline Time Axis

The widget has a horizontal time axis.

It should support:

- a configurable visible time range;
- major tick marks;
- minor tick marks;
- formatted time labels;
- horizontal scrolling;
- horizontal zoom.

Time display should remain readable across useful scales.

For example, labels might appear as:

```text
0.0      0.5      1.0      1.5      2.0
```

at one zoom level and at finer increments when zoomed in.

The timeline widget owns formatting/presentation of these labels, although the caller may provide formatting settings if necessary.

---

## 18. Timeline Rows

The widget displays one or more horizontal rows.

The widget should support:

- fixed or configurable row height;
- row separators;
- vertical scrolling when necessary;
- hit testing on rows;
- hit testing between rows.

The widget does not assign semantic meaning to a row's position.

It merely preserves and reports row ordering.

---

## 19. Timeline Items

A timeline item is a colored rectangle with:

- a stable caller-supplied ID;
- start time;
- end time or duration;
- row;
- text label;
- palette color;
- interaction state.

The timeline widget should support item states such as:

- normal;
- hovered;
- selected;
- provisional;
- disabled or invalid where useful.

A caller might present:

```text
[ Rotate Arm            ]
[ IK Hand --------------------- ]
```

The animation layer determines what those labels mean.

### 19.1 Rectangle labels

Rectangles should support text labels.

Color is useful for recognizing action categories, but it should not be the sole means by which the user determines an item's meaning.

Labels can be elided when a rectangle is too short to display them comfortably.

### 19.2 Minimum interaction size

A very short-duration item can become narrower than is practical to select.

The widget should therefore distinguish:

```text
semantic width
```

from:

```text
minimum hit-test/display affordance
```

without falsifying the item's actual time interval.

---

## 20. Timeline Color Palette

The timeline widget owns a fixed palette of aesthetically chosen colors.

The rest of the application should not assign arbitrary RGB values to timeline items.

Instead, callers use a palette enum such as:

```cpp
enum class timeline_color
{
    blue,
    green,
    orange,
    purple,
    red,
    teal,
    yellow,
    // exact palette to be selected later
};
```

The exact enum names can be finalized when the palette is chosen.

The animation system can consistently map action types to palette entries:

```text
Rigid Rotation -> one palette color
IK Translation -> another palette color
IK Rotation    -> another palette color
```

The widget then owns the actual rendering values.

This makes it possible to:

- choose one coherent palette;
- adapt rendering for dark/light presentation if necessary;
- change the palette later without changing animation data;
- keep color usage consistent throughout the application.

Color choice is presentation state and is not serialized into animation data.

### 20.1 Rectangle gradients

Timeline rectangles should not initially be rendered as completely flat fills.

Each palette entry should produce a subtle horizontal gradient.

Conceptually:

```text
lighter/base tone ----------------> slightly darker tone
```

The gradient should remain restrained rather than glossy or heavily stylized.

Hover, selection, provisional, and invalid states should preferably be shown through:

- borders;
- overlays;
- selection outlines;
- subtle tint changes;

rather than by requiring callers to select separate colors.

---

## 21. Timeline Head

The timeline widget has a **head** representing a time.

The head is displayed as a vertical marker through the timeline.

The caller can set it directly:

```cpp
timeline.set_head_time(t);
```

The user can drag the head horizontally, and the widget reports the newly requested time to its owner.

The widget should support:

- direct programmatic movement;
- user dragging;
- hit testing;
- snapping where configured;
- automatic scrolling/follow behavior when requested.

---

## 22. Head Animation and Clock Ownership

The timeline widget should **not** own a playback clock.

It should not independently animate its own head in real time.

Instead:

```text
animation controller
    owns playback clock
          |
          +--> set timeline head
          |
          +--> evaluate animation
          |
          +--> update canvas
```

The same rule applies during live action recording.

The Animation Timeline/controller computes the current animation time and updates both:

- timeline head;
- canvas pose.

This guarantees that the visual head and the displayed character pose use the same time source.

### 22.1 Follow-head behavior

The widget may own visual **follow-head** behavior.

For example:

```cpp
timeline.set_follow_head(true);
```

can mean:

> When the caller moves the head outside the comfortable visible region, scroll the timeline to keep it visible.

This is presentation behavior rather than clock ownership.

---

## 23. Timeline Row Head

The widget also has a vertically movable **row head** associated visually with the current head.

The row head chooses an insertion position in the row dimension.

It can occupy:

- an existing row; or
- a position between two rows.

Conceptually:

```text
                         head
                           |
Row 3                      |
                           |
                        >--|    row head between rows
                           |
Row 2               [ item ------- ]
                           |
Row 1     [ item ----------|------- ]
```

The row-head position therefore cannot be represented solely by an integer row index.

Conceptually it needs something like:

```cpp
struct row_head_position
{
    enum class placement
    {
        on_row,
        between_rows
    };

    placement kind;
    std::size_t index;
};
```

The exact API can differ.

The generic widget merely reports the position.

The animation-specific owner interprets:

```text
on Animation Layer 2
    -> create action on Layer 2

between Layers 2 and 3
    -> create a new Animation Layer there
```

---

## 24. Timeline Editing Operations

The timeline widget should support direct manipulation of items.

Initial requirements include:

- select an item;
- drag horizontally;
- drag vertically to another row;
- drag between rows;
- resize from the left edge;
- resize from the right edge;
- expose a context-menu request;
- keyboard nudge where useful.

The widget should also support:

- auto-scroll while dragging near an edge;
- invalid-drop feedback;
- optional time snapping;
- provisional items;
- hover feedback;
- configurable follow-head behavior.

Multi-selection may be added later and is not required for the first animation implementation.

---

## 25. Timeline Snapping

Snapping should be generic.

The timeline widget should not contain animation-specific notions such as frames unless the caller explicitly expresses them as a time interval.

Conceptually:

```cpp
timeline.set_snap_interval(10ms);
```

or:

```cpp
timeline.set_snap_enabled(false);
```

The final interface can use whichever time type is adopted by the editor.

---

## 26. Timeline Widget Events

The widget should report **editing intentions** rather than directly mutating stick_man animation objects.

Conceptual events/signals include:

```text
headMoved(time)

rowHeadMoved(position)

itemSelected(id)

itemMoveRequested(id, newTime, newRowPosition)

itemResizeRequested(id, newStart, newEnd)

itemContextMenuRequested(id, position)
```

Exact names are not fixed.

The important boundary is:

```text
timeline widget
    reports requested edit
          |
          v
animation editor/controller
    validates operation
    mutates Core model
          |
          v
timeline widget receives updated model state
```

The reusable widget should never hold pointers or references to Core animation objects.

---

# Animation Timeline Pane

## 27. Animation Timeline Pane

The **Animation Timeline** is a separate pane from the persistent Animation pane.

It is visible only while Animation Mode is active.

This distinction is important:

```text
Normal editing:

    Character pane
    Artwork Browser
    Animation pane

    no Animation Timeline


Animation Mode:

    Character pane
    Artwork Browser
    Animation pane
    Animation Timeline
```

The persistent Animation pane answers:

> What poses and animations exist?

The Animation Timeline answers:

> What actions make up the animation I am currently editing?

The Animation Timeline is a stick_man-specific controller/view built around the generic timeline widget.

---

## 28. Playhead and Canvas Pose

In Animation Mode, the generic timeline head functions as the animation **playhead**.

The playhead represents the current animation time.

The canvas displays:

```text
active animation
evaluated into the Animation Mode working rig
at playhead time
```

Moving the playhead immediately resets the working rig to the animation base pose, reevaluates the animation at the requested absolute time, and updates the canvas. The real project-backed character is not posed by this operation.

Scrubbing backward or forward must work without simulation or history dependence.

---

## 29. Transport Controls

The Animation Timeline should provide basic transport controls adjacent to the timeline.

The initial set should include approximately:

```text
Go to Start
Play / Pause
Stop
Loop
Current Time
```

During playback, the animation controller advances time and updates:

- the timeline head;
- the canvas pose.

`Pause` leaves the playhead at its current position.

`Stop` returns the playhead to the start.

Character manipulation should not occur simultaneously with ordinary playback. Beginning an authoring gesture should pause playback if necessary.

A full video-editor transport system is not required.

---

## 30. Layer Insertion Marker

Within the Animation Timeline, the generic row head becomes the **layer insertion marker**.

The playhead answers:

> At what time will a newly created action begin?

The layer insertion marker answers:

> At what point in the animation-layer stack will the action be inserted?

It can be positioned:

- on an existing animation layer; or
- between two animation layers.

### 30.1 Marker on a layer

When positioned on an existing layer, a newly created action is placed there if its time interval fits.

### 30.2 Marker between layers

When positioned between layers, creating an action creates a new animation layer at that location.

### 30.3 Overlap conflicts

An action cannot overlap another action on the same animation layer.

If the targeted layer cannot accept the interval, the editor should not silently search for an unrelated free layer.

The UI should instead expose the conflict and permit insertion immediately above or below.

Layer position is semantic, so automatic rearrangement should remain conservative.

---

## 31. Creating Actions by Performing Gestures

In Animation Mode, Selection-tool manipulation is used to create animation actions.

Creation is a live operation.

At mouse-down:

```text
action start = current playhead time
layer = current layer insertion position
```

While the user actively performs the gesture:

- the provisional action is updated;
- the animation controller advances time;
- the timeline head moves;
- the provisional timeline rectangle grows;
- the canvas displays the current manipulation.

On mouse-up:

- the provisional action becomes committed;
- its end time is the current playhead time;
- its action parameters are derived from the completed manipulation;
- the playhead remains at the end of the new action.

---

## 32. Active Gesture Time

A newly created action's initial duration is based on time during which the user is **actively performing the manipulation**, not simply the wall-clock interval from mouse-down to mouse-up.

Conceptually:

```text
pointer actively moving:
    animation time advances

pointer paused:
    animation time pauses
```

If the user pauses while holding the mouse button to think, that pause does not lengthen the action.

When manipulation resumes, animation-time advancement resumes.

The user can see this directly because both:

- the playhead; and
- the provisional action rectangle

stop while the gesture is paused.

The exact inactivity threshold is an editor implementation detail.

---

## 33. Recording Speed

Animation Mode provides a **recording speed** control.

This lets users comfortably author animations that would otherwise require physically awkward fast or slow gestures.

The control describes how quickly the resulting action plays relative to the author's physical manipulation.

Examples:

```text
Recording speed 1x

1 second active gesture
    -> 1 second action
```

```text
Recording speed 2x

1 second active gesture
    -> 0.5 second action
```

```text
Recording speed 4x

1 second active gesture
    -> 0.25 second action
```

```text
Recording speed 0.5x

1 second active gesture
    -> 2 second action
```

A useful initial control might provide:

```text
0.25x
0.5x
1x
2x
4x
```

At `4x`, for example, one second of active physical manipulation advances the playhead by one quarter second.

Recording speed is an editor authoring aid.

It is not stored in the animation.

---

## 34. Gesture Recording Is Not Motion Capture

The physical pointer trajectory is not necessarily recorded as animation data.

The gesture establishes action parameters and an initial duration.

For example, a rigid rotation gesture determines:

- the target rotation;
- duration.

Playback still evaluates that rotation according to the action's easing.

Likewise, an IK translation action uses its authored target path (line, cubic Bézier, or spline) rather than replaying raw pointer samples.

The system therefore remains action-based rather than becoming a mouse-motion capture system.

---

## 35. Animation Timeline Item Presentation

The Animation Timeline maps action types to generic timeline palette colors.

For example:

```text
Rigid Rotation -> timeline_color::blue
IK Translation -> timeline_color::orange
IK Rotation    -> timeline_color::green
```

The exact mapping will be selected with the final palette.

Action labels should be short and useful, for example:

```text
Rotate Forearm
IK Hand
IK Foot
```

Action-type colors should remain consistent across animations.

During live creation, the provisional action appears immediately as a provisional timeline item and grows with the advancing playhead.

---

## 36. Editing Existing Actions in the Timeline

The user can:

- drag an action horizontally to change start time;
- resize it to change duration;
- move it vertically to another animation layer;
- move it between layers to create a layer at that point;
- select it by clicking its rectangle;
- invoke an animation-specific context menu.

Because animation-layer position has semantic meaning, vertical movement changes animation behavior rather than merely rearranging UI.

Empty animation layers may be removed automatically when no actions remain.

---

## 37. Selecting and Editing Existing Actions

Selecting an action in the timeline selects the animation action itself.

The canvas should display editing geometry appropriate to the action.

### Rigid rotation

Display:

- pivot;
- affected element;
- rotation arc or equivalent guide.

### IK translation

Display:

- effector;
- target-reference frame;
- target path;
- path-type-specific points and handles.

Manipulating these action-specific canvas controls edits the selected action rather than creating a new one.

The referenced skeleton objects may also be highlighted, but action selection and skeleton-object selection remain conceptually distinct.

---

## 38. Action Editor

Selecting an action should expose a compact action editor associated with it.

This may be a drawer, card, or persistent area of the Animation Timeline pane.

Every action editor contains common controls:

```text
Start
Duration
Easing
```

Below these are action-specific controls.

Example:

```text
Rigid Bone Rotation

Bone
Pivot: Root / Tip
Rotation
```

or:

```text
IK Translation

Effector
Target reference: Character Start / Character Root / Node
Reference node (when applicable)
Path type: Line / Cubic Bézier / Spline
Path parameters
Pinned nodes
```

The action editor complements direct canvas manipulation.

Geometric editing should normally remain easier on the canvas.

---

# Rig Evolution and Animation Compatibility

## 39. Poses Describe Pose, Not Rig Topology

Saved poses should represent the state of an existing character rig, not private copies of the rig's topology.

Topology information such as:

- which nodes and bones exist;
- bone ownership;
- connectivity;
- bone lengths;
- constraints;

belongs to the character's rig.

Pose information is stored against persistent object IDs.

The exact pose-state representation remains an implementation detail, but the design should preserve this separation:

```text
character rig
    defines what exists and its structural rules

pose
    defines how that rig is posed
```

This distinction lets many rig edits propagate naturally to existing poses.

---

## 40. Cosmetic Rig Changes

Changes that preserve identity and structure should not affect poses or animations.

Examples:

- rename node;
- rename bone;
- rename skeleton;
- rename character.

Because poses and actions refer to stable object IDs, display-name changes require no repair.

---

## 41. Adding Rig Elements

A character may gain new nodes or bones after poses and animations already exist.

Existing pose data has no historical state for a newly created object.

The recommended policy is:

> When a new rig element is added, its current authored state is seeded into every existing pose of that character.

For example, if a tail is added while the character is currently standing, each existing pose initially receives the tail in the configuration in which it was created.

The animator can subsequently update individual poses as desired.

Existing animations contain no actions for the new element unless the user creates them.

The new element therefore follows ordinary rig behavior until explicitly animated.

This is preferable to making every existing pose immediately "broken" whenever a character gains detail.

---

## 42. Removing Rig Elements

Deleting a node or bone is more consequential.

### 42.1 Poses

Pose state belonging only to deleted object IDs can be removed as part of the same project edit.

Surviving pose data remains associated with surviving IDs.

Undoing the topology deletion should restore both the rig object and its pose data as part of the same undoable operation.

### 42.2 Actions

An animation action may explicitly target a node or bone.

Deleting that object must not silently redirect the action to another object.

Actions whose required target IDs no longer exist become **invalid**.

The editor should surface this rather than crashing or quietly changing the animation's meaning.

An invalid action can be displayed in the timeline using:

- an invalid/warning treatment;
- a warning icon;
- a tooltip or action-editor explanation.

It does not contribute to animation evaluation until repaired.

The user may then:

- retarget the action where meaningful; or
- delete it.

This preserves authoring work and makes the consequence of topology changes visible.

An alternative implementation may eventually offer an explicit deletion-time option to remove affected actions, but silent deletion should be avoided.

---

## 43. Connectivity and Skeleton Splits

Deleting a bone can split one skeleton into multiple skeletons while the surviving nodes and bones retain their object IDs.

A character is allowed to own multiple skeletons, so this does not inherently invalidate the character.

Actions referencing surviving objects remain potentially valid.

However, an operation such as IK may rely on a chain that no longer exists after the topology change.

Therefore actions should validate not only that their referenced IDs exist, but also that the structural relationship required by the action remains valid.

For example:

```text
IK action
    effector still exists
    pinned node still exists
    but no usable chain connects them
```

should be surfaced as an invalid action rather than producing undefined behavior.

---

## 44. Bone Length and Constraint Changes

Bone lengths and constraints belong to the rig rather than to individual poses.

Changing them therefore changes the structural context in which old poses and animations are interpreted.

The system should prefer preserving semantic pose information where possible rather than storing obsolete copies of old rig geometry.

For example:

- changing a bone length should allow poses to retain their intended orientation while world-space endpoints move according to the new length;
- changing a constraint may make an old pose or action impossible to reproduce exactly.

After a structural/constraint edit, Core should be able to validate affected poses and animations.

If a stored pose cannot be represented legally under the new rig constraints, it should be marked as needing attention rather than silently discarded.

The exact pose-repair mechanism can be determined once the concrete pose representation is finalized.

---

## 45. Validation After Rig Changes

Rig-changing operations should result in animation/pose validation.

Conceptually:

```text
edit character rig
        |
        v
project updates topology
        |
        v
validate character poses
validate animation action references
        |
        v
surface compatibility problems
```

Validation should detect at least:

- missing target IDs;
- missing pivot IDs;
- missing IK effectors;
- missing pinned nodes;
- impossible/disconnected IK chains;
- pose data incompatible with current rig constraints.

This should be centralized rather than having individual UI components discover corruption opportunistically.

---

## 46. No Silent Semantic Retargeting

A general rule for rig evolution is:

> Persistent object identity is authoritative. When an object disappears, animation data must not silently choose a "similar" replacement.

For example, deleting `left_hand` must not cause an IK action to attach itself to whichever node happens to be spatially closest.

Retargeting is a user decision.

This is one of the benefits of the global object-ID model.

---

## 47. Default Pose and Rig Evolution

The character's Default pose participates in the same rig-evolution rules as other poses.

When new elements are added, their current state is seeded into Default.

When elements are removed, their Default-pose state is removed.

Structural changes that make part of Default incompatible should be surfaced through the same validation process.

Because every animation has a valid base pose and the built-in Default cannot be deleted, there is always a fallback base-pose target for animation management.

---

# Shared Manipulation and Playback

## 48. Shared Manipulation Primitives

The animation system should not duplicate skeleton-manipulation algorithms already used by the Selection tool.

Operations such as:

- rigid rotation;
- rotation about an endpoint;
- rigid translation;
- IK rotation;
- IK translation;
- pinned-node handling;

should ultimately be expressed through reusable manipulation primitives.

The Selection tool can invoke these operations immediately during ordinary editing.

Animation evaluation can invoke the same operations at an action's current normalized time.

This ensures interactive posing and animation have the same geometric behavior.

---

## 49. Scrubbing and Playback

Moving the playhead reevaluates the animation at exactly that time.

Scrubbing must work equally well:

- forward;
- backward;
- by small movements;
- by large jumps;
- by arbitrary direct seeks.

The resulting pose at time `t` must not depend on how the playhead arrived there.

Playback is repeated evaluation at increasing absolute times.

The animation itself does not depend on playback frame rate.

---

## 50. Selection and Playhead State

Action selection and current animation time are separate concepts.

Selecting an action does not inherently change the playhead.

Moving the playhead does not inherently change the selected action.

A future editor convenience may optionally move the playhead when an action is opened, but the underlying concepts remain separate.

---

# Persistence and Ownership

## 51. Serialization

Poses and animations belong to character/project data and must be serialized as part of the packaged project.

Serialized character semantic data includes the designated character-root node ID.

Serialized pose data needs to contain enough pose state, keyed through stable IDs, to reproduce the pose against the character rig.

Serialized animation data includes at least:

```text
animation ID/name
base pose ID
ordered animation layers
actions
action start times
action durations
easing
action types
action-specific parameters
object IDs referenced by actions
IK target-reference mode and reference-node ID where applicable
IK path type and path geometry
pin information where applicable
```

Editor-only state should not become animation semantics.

Examples of editor/session state include:

- recording speed;
- current playhead time;
- selected timeline item;
- timeline zoom;
- scroll position;
- palette RGB values;
- gradient details;
- pane visibility.

---

## 52. Core and Editor Responsibilities

### Core

Core should own:

- character-root semantics used by animation target frames;
- Default and named pose semantic data;
- animation semantic data;
- animation layers and ordering;
- action types and parameters;
- easing definitions;
- deterministic animation evaluation against detached working topology;
- target-reference resolution using character root/current node positions;
- pose/action reference validation;
- rig-change compatibility rules that affect semantic data;
- manipulation algorithms needed by evaluation;
- serialization/deserialization;
- references through stable object IDs.

Core must not depend on Qt.

### Editor

The editor should own:

- persistent Animation pane;
- tree presentation and custom icons;
- inline rename interaction;
- Animation Mode session lifecycle and canvas rebinding between project and working topology;
- canvas Animation Mode banner;
- Animation Timeline pane;
- generic timeline widget;
- palette rendering and gradients;
- playhead interaction;
- playback controls;
- recording speed;
- layer insertion marker;
- action colors and labels;
- provisional action display;
- action selection;
- drag/resize interaction;
- action editors;
- target-path editing controls for line, cubic Bézier, and spline paths;
- canvas gesture interpretation;
- active-versus-paused gesture timing;
- visual warnings for invalid poses/actions;
- visual previews.

---

# Future Extension

## 53. Pose-to-Pose Animation Generation

Named poses leave room for a later pose/keyframe-style authoring convenience without introducing a second animation engine.

A future tool may accept:

```text
Pose A
Pose B
duration
easing
```

and generate ordinary animation actions that transition between them.

One possible algorithm is:

1. create an FK-style reference transition by interpolating rig rotations;
2. compare important node trajectories with direct pose-to-pose positional interpolation;
3. infer useful IK effectors where FK trajectories differ substantially from desirable endpoint motion;
4. generate ordinary rigid/FK and IK actions;
5. allow the user to edit the resulting actions normally.

This is a future authoring/generation feature, not part of the initial animation evaluator.

The preferred architectural direction is:

```text
pose/keyframe convenience
        |
        v
transition generator
        |
        v
ordinary stick_man actions
        |
        v
normal animation evaluation
```

rather than introducing a separate keyframe animation model.

Generated collections of actions may eventually benefit from a visual grouping/collapse mechanism in the timeline, but this is not required initially.

---

# Scope and Open Questions

## 54. Initial Non-Goals

The first animation implementation does not need:

- traditional per-property keyframe tracks;
- a graph editor;
- arbitrary user-defined action types;
- custom easing-curve editing;
- automatic pose-to-pose action generation;
- raw pointer-motion capture;
- physics simulation;
- procedural blending systems;
- a generalized animation state machine;
- timeline multi-selection unless it proves necessary.

These can be reconsidered as actual use cases arise.

---

## 55. Open Design Questions

The following details remain unresolved.

### Pose state representation

What exact minimal rig state does a pose store so that:

- bone-length changes remain meaningful;
- root translations are preserved;
- constraints can be validated;
- topology is not duplicated?

### Invalid pose repair

When changed rig constraints make an old pose illegal, what exact UI and Core operation repairs it?

### IK rotation

What is the exact stored representation and evaluation rule for an IK rotation action?

### Action taxonomy

Which current Selection-tool manipulation modes deserve distinct action types, and which should instead be parameters of a smaller set of action types?

### Pins

For IK actions, are pinned nodes:

- explicitly stored with each action;
- inherited from the base pose;
- captured from editor state when the action is created;
- some combination of these?

### Active-gesture detection

What inactivity threshold or event strategy best distinguishes an intentional pause during a drag from ordinary gaps between pointer events?

### Editing versus creating

When an animation action is already selected, which canvas gestures edit that action and which gestures begin a new one?

### Layer insertion conflicts

What exact visual feedback should be shown when the layer insertion marker targets a layer that cannot contain the provisional action?

### Timeline palette

Which fixed palette should the reusable timeline widget use, and what enum names best represent it?

### Timeline zoom/navigation details

What mouse/keyboard gestures should control:

- zoom;
- horizontal pan;
- jump to head;
- fit animation?

These are widget interaction decisions rather than animation-model decisions.

---

## 56. Guiding Principle

The persistent Animation pane answers:

```text
What poses and animations does this character have?
```

Opening an animation enters a focused editing state:

```text
Animation pane
    -> choose/open animation

Animation Mode
    -> canvas banner makes mode explicit
    -> Animation Timeline becomes visible
    -> playhead controls displayed pose
    -> Selection tool creates actions
```

The reusable timeline widget itself answers only generic questions:

```text
Where are timed items?
Which row are they on?
What time is the head at?
Where is the row head?
What edit did the user request?
```

It does not know what those items mean.

The animation timeline should be understandable as a small graphical program:

```text
do this
then this

while that is happening
also do this

apply this operation before that one
```

Animation Mode should make creating those operations immediate:

```text
choose when
    -> playhead

choose composition position
    -> layer insertion marker

perform manipulation
    -> Selection tool on canvas

perform it in time
    -> live action recording

refine it afterward
    -> timeline + action editor
```

At any time `t`, `stick_man` reconstructs the character pose deterministically from:

```text
base pose
+
ordered animation layers evaluated at t
=
animated pose
```

That model is the foundation of both Core animation semantics and the editor's animation UI.
