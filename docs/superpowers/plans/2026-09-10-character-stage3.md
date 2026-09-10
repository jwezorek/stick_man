# Character Stage 3 Implementation Plan

**Goal:** Implement the editor behavior in `docs/character.md` on the Stage 2 model.

**Architecture:** Keep topology variants and Core ownership unchanged. Introduce a broader editor selection value and a character canvas item using a shared aggregate frame. Route authoring and clipboard changes through undoable model operations, and refresh panes from the semantic project.

**Spec:** `docs/character.md` and the Stage 3 request.

- [x] Add regression coverage for hierarchy, explicit versus inferred selection, and command history.
- [x] Add model character creation, rename, deletion, and whole-character paste commands using membership snapshots.
- [x] Generalize skeleton frames and add explicit character selection, rig expansion, and translation.
- [x] Populate character roots from Core membership; synchronize pane and canvas selection without promotion of explicit rows.
- [x] Add Make Character, adoption, editable character properties, and rename updates.
- [x] Add semantic clipboard payloads and Core deletion-preview confirmation; present Add Bone rejection normally.
- [x] Build, run regressions, inspect rendered UI where available, and audit the architectural constraints.

Validation uses the existing offscreen Qt regression executable and model/Core suites. Character tests must cover one and multiple components, loose siblings, exact-rig inference, clipboard identity, and undo restoration. UI tests exercise real widgets and tool events, including OK/Cancel dialogs.

## Validation result

The MSVC x64 Debug build succeeded, including `stick_man`, `selection_regressions`, and all model/Core regression executables. CTest passed all 31 tests. `git diff --check` passed using the repository's configured line-ending conversion. Existing CMake dependency-policy and ignored-result compiler warnings remain.

Commands run from the repository, with the Visual Studio developer environment loaded:

```text
cmake --build out/build/x64-Debug -j 6
ctest --test-dir out/build/x64-Debug --output-on-failure -j 4
```

The new non-GUI `character_stage3` executable covers semantic selection expansion/inference, explicit one-component skeleton selection, rigid translation, creation validation, and authoring history. Eleven new offscreen GUI cases cover hierarchy, Properties and tree selection, Make Character, adoption, rename, clipboard, deletion dialogs, Add Bone rejection, translation with rag-doll mode enabled, and tag hit-testing/dragging at multiple zooms. The existing selection and Stage 1/2 suites remain passing.

Rendered inspection used `selection_regressions character_visual` with `QT_QPA_PLATFORM=offscreen`, writing `out/character-stage3.png` and `out/character-stage3-skeleton.png`. The visual test loads Segoe UI explicitly because the Windows offscreen backend does not enumerate system fonts. The images show the disconnected rig inside a purple frame, a legible attached name tag, character-root hierarchy with loose siblings, editable character Properties, and the cyan frame when the child skeleton is explicitly selected.

Native desktop control was unavailable in this session. GUI interactions were verified using real Qt widgets/tool events offscreen, with manual inspection of the rendered images; a hand-driven native Windows acceptance pass was not performed.

## Architectural audit

- Core character/rig implementation and membership rules are unchanged.
- `mdl::selection` contains explicit character references; `mdl::skel_piece` remains topology-only. Empty selection represents none. Canvas inference is isolated in `mdl::infer_selection`; explicit pane selections bypass it.
- Canvas character items store character identity and resolve the complete rig. Shared `aggregate_frame` handles skeleton and character bounds; the tag is a Qt-only child and cannot enlarge rig bounds.
- Pane ownership is read from `sm::project::characters()` and Core rigs. Authoring actions call `mdl::project` commands.
- Command completion/undo/redo emits project-aware refresh. Character-only rename updates both tree and tag. Tree/model signal handlers are disconnected during rebuilding and selection synchronization.
- Character clipboard content has a distinct payload. Ordinary topology copies stay in detached scratch topology; ordinary paste remains loose. Character paste allocates fresh identities and retains actual inserted identities for redo.
- Deletion confirmation uses Core replacement previews. Selected-character deletion uses the model's structural deletion path, preserving identity through undo.
- Cross-character Add Bone is rejected before command execution and shown as a normal error dialog.
- Independent review findings about Properties component selection and transformed name-tag hit testing were addressed and regression-tested.

## Editor commands

Select complete loose skeleton rows to enable **Make Character** in Properties or the Skeleton pane context menu. The same context menu offers **Add to Character...**, with a target-character chooser. Character roots and their child skeleton rows are separate selectable objects. Rename a character in its row or Properties; **Select Component** selects the chosen skeleton explicitly. Standard Cut/Copy/Paste handles selected characters as whole characters and skeleton/topology selections as ordinary loose topology.
