# `sm::character`: Architectural Purpose

## Goal

`sm::character` should be the persistent semantic object that represents an actual animatable character.

The project already has skeletons, but a skeleton is the wrong place to attach long-lived character data such as artwork and animations. Skeletons are intentionally lightweight topology objects. Their identity and lifetime are tied to the current structure of the drawing, and ordinary editing can create, destroy, split, or merge them.

A character needs a more stable identity.

The intended distinction is:

- **skeleton** — a lightweight rooted connected component of the current topology;
- **rig** — the collection of skeleton components that currently make up a character;
- **character** — the persistent authored object that owns the rig and the resources associated with that character.

Conceptually:

```text
project
├── topology
│   ├── nodes
│   ├── bones
│   └── skeleton components
│
└── characters
    └── character
        ├── rig
        │   └── references to one or more skeletons
        ├── artwork
        └── animation
```

## Why a skeleton should not be a character

A skeleton is fundamentally a consequence of topology.

For example, deleting a single connecting bone can trivially turn one skeleton into two skeletons. Adding a bone can join previously separate components. Cut/copy/paste can create new loose skeletons. These are normal structural edits, not semantic operations on the identity of a character.

If artwork and animation were owned directly by a skeleton, those ordinary topology changes would force us to answer awkward questions about which newly created skeleton inherits the old resources, whether a merge combines resources, and whether a split implicitly creates multiple characters.

That is a sign that the ownership is at the wrong level.

Skeletons should remain deliberately inexpensive and ephemeral. They describe the current connected structure; they should not have to become persistent containers for every higher-level feature simply because they are currently the nearest object to the bones.

## Character as the stable authored boundary

A character is the object the user actually means when they say, for example, "this is Alice."

It should remain the same character even if its rig changes structurally.

A character may contain a single skeleton, which will probably be the common case, but it may also contain several disconnected skeletons. This is useful for characters whose independently movable pieces are not physically connected by bones. Character identity therefore cannot be equated with one connected skeleton component.

The character provides the stable place to own data whose meaning spans the rig:

- artwork and appearances;
- animation and poses;
- eventually other character-level metadata or resources.

The rig tells the character which skeleton components in the project's current topology belong to it.

## Loose skeletons remain useful

Not every skeleton in a project needs to belong to a character.

Users should be able to create and manipulate loose skeletons as lightweight construction objects. A loose skeleton can become part of a character through **Make Character**, later rig-editing/adoption commands, or by connecting it to a skeleton that already belongs to a character.

This keeps basic skeleton editing simple and avoids silently creating heavyweight semantic objects during ordinary topology manipulation.

## Character membership and editing rules

A skeleton belongs to at most one character. A skeleton with no character membership is loose.

### Connecting skeletons

- Adding a bone between skeletons belonging to different characters fails and displays an error dialog. The failed operation leaves the project unchanged.
- Adding a bone between a loose skeleton and a character's skeleton succeeds. The resulting connected skeleton belongs to that character, incorporating the previously loose topology regardless of which endpoint was chosen first.
- Connecting components of the same character retains that character's membership.
- Connecting loose skeletons leaves the result loose; it does not create a character.

### Splitting and deleting

Every component produced by splitting a character's skeleton retains membership in the same character. A split does not create new characters.

Deleting a character's final rig component also deletes the character. Before applying such an operation, the editor displays an **OK/Cancel** dialog explaining that the character will be deleted. **OK** applies the operation; **Cancel** leaves the project unchanged. Empty characters are not retained after this deletion.

## GUI behavior

### Skeleton pane and character selection

The skeleton pane displays characters as parent entries above the skeletons that comprise their rigs. Loose skeletons remain available without a character parent.

A character can be selected either by selecting its entry in the skeleton pane or by selecting all of its constituent skeletons in the editor. The editor indicates character selection with a bounding rectangle similar to the skeleton-selection indicator, but in a different color and with an attached label such as **character: Fred**. The exact color and label styling remain UI design details.

For a character containing a single skeleton, dragging a selection around the whole skeleton in the editor selects the character. To select that skeleton itself, the user selects its entry in the skeleton pane or clicks the corresponding skeleton in the character's properties pane.

Explicit skeleton selection through either pane must remain skeleton selection even when that skeleton is the character's entire rig. Selection therefore distinguishes a character from its component skeletons; selecting the same topology does not always imply the same semantic selection.

### Cut, copy, and paste

Cut, copy, and paste support whole-character selections. Copying a selected character includes its complete rig and associated character resources. Pasting that whole-character clipboard content creates a character with its rig and resources, preserving their internal associations. Cutting a selected character removes the whole character and places it on the clipboard for pasting.

When the selection is topology rather than a whole character, ordinary paste creates loose skeletons, including when the copied or cut topology came from a character. It does not retain the source character's membership. This also applies to a skeleton explicitly selected through a pane in a single-skeleton character.

Ordinary paste does not insert clipboard content into an existing character, regardless of the current selection. Whole-character paste creates a character; topology paste creates loose skeletons.

Pasting directly into a character will be provided by special commands. The ordinary bone-connection rule above still allows pasted loose topology to be connected to an existing character.

## Project topology remains authoritative

The project owns the live topology. Characters do not need their own independent copies of nodes, bones, or skeleton structure.

Instead, a character's rig identifies the relevant skeleton components within the project's topology. Structural operations remain project-controlled, while the character supplies the durable semantic grouping above that topology.

This separation gives the model two different kinds of identity:

- **topological identity**, which can change as the user edits connectivity;
- **character identity**, which should persist across those changes.

That distinction is the primary reason for introducing `sm::character`.

## Design principle

The purpose of `sm::character` is **not** to make skeletons more complicated under another name.

It exists specifically so that skeletons can remain lightweight and ephemeral while stick_man gains persistent character-level concepts such as artwork and animation.

In short:

> A skeleton describes what is connected right now.  
> A character describes what those pieces collectively *are*.
