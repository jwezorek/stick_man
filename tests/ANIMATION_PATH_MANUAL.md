# Transition-local path constraint smoke test (Qt editor)

1. Create a character with at least a two-bone limb and an animation containing two
   distinct poses. In Animation Mode, select the **first** frame and Constraint Tool.
   The Path operation must be available. Select the **last** frame: Path must be
   disabled, and clicking a node must not create a path.
2. Return to the first frame; select Path / Line segment and click the limb tip.
   Verify the destination-position hollow ghost is at the second pose's tip,
   independent of the current first-pose position. Revisit the transition and
   confirm the path is still visible and selectable.
3. Switch the selected path to Cubic Bezier, independently drag the two blue
   control points; no bone or node should be posed while dragging. Create a new
   cubic by dragging from a different node, then adjust its two handles.
4. Switch to Bezier spline; double-click its stroke to add a knot, drag the
   orange knot and its blue handles, then right-click an interior knot to remove
   it. The endpoints (hollow circles) must not be draggable.
5. Scrub at 0%, 10%, 50%, 90%, 100%. The *requested* target should move at
   near-constant speed along the entire curve. Actual motion may deviate where
   the skeleton cannot reach. Seek directly to 90%, then back to 10% and verify
   no dependency on previous scrub positions. Verify two simultaneous paths.
6. Add an ordinary transition pin to a different node and a rotation limit;
   check that the fixed node and the angle limit are preserved while paths
   influence reachable nodes. Ordinary Edit Mode pins/rotation tools must still
   behave as before.
7. Change both keyframe poses with the normal posing tools: verify path endpoints
   follow without moving endpoint handles by themselves. Translate/rotate the
   whole animation's initial placement and verify path ghost, path adornment,
   and animated IK targets transform identically. Character motion during
   playback must not carry the fixed path reference frame with it.
8. Exercise Undo/Redo for creation, deletion (Delete key), shape changes,
   handle moves (one undo per drag), and knot insertion/removal. Save a packaged
   project, reopen it, and repeat the scrub/shape check.
