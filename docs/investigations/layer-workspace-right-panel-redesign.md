# Layer Workspace and Right-Side Panel Redesign

**Status: design proposal for review, not an implementation.** This document supersedes the right-side and layer treatments in the earlier interface proposals. The pictures are static illustrations, not screenshots or evidence that any pictured control works. Existing artwork and colour in the pictures are illustrative; chrome remains neutral. The light preview intentionally uses a square colour selector; the dark preview intentionally uses a triangle. Both are valid alternatives, not required to match.

## Visual review

- [Dark workspace — triangle selector](design-proposal/right-workspace-dark-triangle-proposal.png) ([SVG](design-proposal/right-workspace-dark-triangle-proposal.svg))
- [Light workspace — square selector](design-proposal/right-workspace-light-square-proposal.png) ([SVG](design-proposal/right-workspace-light-square-proposal.svg))
- [Layer thumbnail size comparison](design-proposal/layer-thumbnail-modes-proposal.png) ([SVG](design-proposal/layer-thumbnail-modes-proposal.svg))
- [Canvas Viewer detail](design-proposal/right-workspace-viewer-detail-proposal.png) ([SVG](design-proposal/right-workspace-viewer-detail-proposal.svg))
- [References detail](design-proposal/right-workspace-refs-detail-proposal.png) ([SVG](design-proposal/right-workspace-refs-detail-proposal.svg))

These show intended hierarchy and density, not a pixel-perfect target or an exhaustive list of every state. In the full views, the lower colour area is a separately dockable group. The artwork is deliberately simple; the miniature examples are illustrative rather than a statement about the renderer. Detailed interaction and non-visible actions below are part of the proposal.

## 1. Workspace architecture

The main right dock has **Layers | Canvas Viewer | References**. These are alternative views of the same space, not panels piled vertically into a narrow column. The active tab is identified by neutral weight and an edge line. Users can resize the dock, move tabs, split a tab into a separate dock, or float it. Show the active document's state; panel placement and selected tabs are workspace preferences. Keep the canvas central and tools accessible.

| Area | Recommendation | Why / tradeoff |
| --- | --- | --- |
| Layers | Main right dock; default active | Most frequent structural controls stay close at hand. |
| Canvas Viewer | Main tab; can split or float | Overview is useful at high zoom, but need not consume permanent space. Split when it must stay visible alongside Layers. |
| References | Main tab; can split or float | Separate navigation and viewing from artwork editing. Opening it as a tab alone hides Layers; **Open alongside** splits/tears it into an independent dock or floating panel, so both remain visible while drawing. |
| Colour, Palette, Colour Mixer | A second, independently dockable group, defaulting to Colour; each also separable | Related colour tasks share space without mixing controls into the layer list. Side-by-side placement is possible for frequent mixer/palette use; tabs economise space when needed. |
| Assets | Separately dockable, opened on demand; optionally grouped with colour panels by the user | Asset browsing can need more room and is not always a colour task. It should not displace the persistent layer controls by default. |
| Document | Separately dockable, opened on demand; compact contextual access via existing document UI where appropriate | Document metadata is less frequent than layer or colour work. It need not be a permanent properties section. |

This is a starting arrangement, not a single giant tab collection or a restriction on user arrangements. Dock split/float/close/restore actions must be available from the tab menu and keyboard-accessible; retain a Panels/Window route to reopen closed panels. Saving and restoring layout must not hide an open document or strand a panel off-screen. The precise persistence scope and integration with existing docking need validation.

## 2. Layers: layout and controls

A compact header provides blend mode, applicable lock/alpha-lock and clipping actions, and thumbnail-size menu; the next row contains selected-layer opacity slider and numeric value. Opacity here is **layer** opacity, never brush opacity. Use compact but reachable hit areas, legible text and sufficient contrast; glyph appearance can be small while its interaction target remains comfortable. Keep essential per-row eye, lock, type, thumbnail and name present at every size. Preserve canvas space without establishing an arbitrary panel-width or document-complexity minimum. At narrow widths elide long names with tooltip/accessibility full text, and prioritize row controls; allow resizing, scrolling and a contextual overflow rather than silently losing operations. Test usable widths and high-DPI scaling instead of hard-coding a speculative minimum.

The list scrolls independently. Each row has a disclosure triangle where applicable, type glyph, thumbnail, name, optional compact functional colour-label marker, visibility and lock. Selection uses neutral row brightness and a non-colour edge/focus indicator; active item is also indicated by weight/focus. Folders show a meaningful composite thumbnail when available; transparent content uses a quiet neutral checkerboard, with empty/failed/loading states distinguishable. Thumbnails fit the document extent consistently so they communicate artwork position, rather than presenting a misleading crop. The image shows illustrative raster, vector, folder, reference and mask glyphs; finalize their distinct shapes with accessibility testing.

Bottom actions: add layer (menu for supported types), add folder, move up/down and delete; more actions in context menu. Keep buttons disabled with a reason when inapplicable, rather than pretending an unfinished action works. Inline rename by F2 or double-click name; Enter commits, Escape cancels. Drag-and-drop has before/after/inside indicators, auto-scroll near list edges, and equivalent keyboard move commands. Explicitly signal invalid drop destinations before release. Do not put creation, import or save shortcuts into the main toolbar solely for this design.

### Thumbnail sizing and wheel routing

Starting sizes, in logical pixels, to validate with real artwork, scaling and target sizes:

| Mode | Thumbnail | Purpose |
| --- | ---: | --- |
| Large | 48 × 48 | Better image identification. |
| Medium | 32 × 32 | Default; balance detail and row count. |
| Compact | 24 × 24 | More rows while retaining readable names and the same essential controls. |

These are proposed presets, not limits on panel size, zoom, layer count, nesting or artwork complexity. Row height may exceed the thumbnail when needed for a comfortable target. All modes retain hierarchy, labels, selection and controls.

- Wheel **over the actual thumbnail rectangle**, not elsewhere on the row, changes one preset per wheel notch: up to larger, down to smaller. Respect platform/device natural-scroll configuration. Accumulate high-resolution deltas to one logical step, reset appropriately when pointer leaves or gesture ends; clamp to presets. At an endpoint consume the thumbnail gesture rather than unexpectedly moving the list. No simultaneous list scroll, selection change or canvas zoom.
- Wheel elsewhere over the list scrolls the list normally. Other controls handle their own wheel only when intentionally focused/targeted and must not accidentally change layer properties while scrolling.
- Preserve selected layers and keep the same list anchor visible as rows resize. Do not move keyboard focus or change document content; treat size as a workspace/view preference, not an artwork edit.
- Discoverability: a tooltip on the thumbnail says “Wheel: thumbnail size” and names the current mode; a small header size menu and its keyboard-accessible equivalent offer Large, Medium and Compact explicitly. Accessible names announce mode and action. No permanently repeated instructional label in every row.
- Provide a preference to disable hover-wheel resizing, particularly for touchpads and users who scroll over thumbnails accidentally; the explicit menu still works. Validate touchpad behavior on each supported platform. The interaction must not prevent keyboard, screen reader or alternative input use.

### Layer and relationship vocabulary

This is the proposed semantic model, not a claim that it exists today:

| Kind / relationship | Row representation | Meaning and contextual operations |
| --- | --- | --- |
| Raster | Image/pixel glyph and artwork thumbnail | Paintable pixel content; duplicate, rename, merge and convert only where supported. |
| Vector | Distinct path/nodes glyph and preview | Editable vector content, not merely a raster thumbnail. Offer vector-specific operations only when supported. |
| Folder | Disclosure + folder glyph + composite preview | Owns ordered children and can nest. Collapse/expand without changing child visibility or document structure. Folder visibility/opacity/blend apply to the group's composite; exact compositing isolation semantics must be defined during implementation. |
| Reference **layer** | Distinct reference glyph + image thumbnail | A document layer for artwork-related reference/tool use, with explicit visibility and export/compositing behavior chosen in implementation. It is not the independent References panel. |
| Mask | Mask glyph and grayscale thumbnail indented directly beneath its owner | Attached to one eligible layer or folder; enables/disables or edits the owner's mask. Explicitly show owner and mask selection; no ambiguous floating mask. Mask action and representation depend on actual mask support. |
| Clipped layer | Small neutral bent-arrow/relationship indicator and accessible “clipped to [name]” | The layer is constrained by the alpha of the valid base below it. This is distinct from masks and alpha lock. Context action creates/removes the relation; do not imply it is already functional. |

Rows are ordered top to bottom in visual stacking order. Children are indented under their parent and disclosure hides only presentation, not the underlying objects. A mask is shown immediately beneath its owner as an attached item, not as an independent sibling eligible to be a clipping base. Depth is model-driven with scrolling/available-space handling, not an invented nesting cap. Folder composite, inherited visibility and inherited lock should be communicated in tooltips and accessible descriptions; distinguish a row's own state from an effectively hidden/locked ancestor.

Clipping proposal: a clipped sibling binds to an eligible immediately lower base within the same parent; a contiguous run of clipped siblings may share that base. An explicit relationship must survive innocuous rename and selection changes. Reorder/drop previews must show the resulting relationship; reject a move that would leave an invalid relation, or require an explicit user-confirmed unclip rather than silently changing appearance. A base move carries its contiguous clipped run by default; moving an individual clipped item must preserve a valid base or be rejected. Folders and eligible layers may be bases only where compositor support is defined. Prevent cycles, moving a folder into itself or descendants, mask reparenting to an invalid owner, and cross-parent clipping. Define undo as one transaction with all affected relationships. Test composite edge cases before declaring these semantics implemented.

### Labels, state, selection

- **Colour labels** are user-selected functional markers, not decorative selection backgrounds. Offer None and a small named palette; include textual names and a non-colour marker/accessible description so colour is not the only cue. Label a selection via context menu; show mixed state as mixed. Label choice must not alter pixels or layer compositing.
- **Visibility and lock** act on their row. Give accessible names such as “Hide Ink” and “Unlock Ink”; communicate inherited versus direct state. Lock is an editing restriction, distinct from visibility. Alpha lock, when actually supported for a suitable layer, restricts edits to existing alpha and is not a clipping mask.
- Click selects; standard platform modifier-click toggles individual selection and Shift selects a contiguous visible range. Maintain a primary active item for painting and a set for batch operations. Collapse a folder without silently discarding selected descendants: indicate hidden selection count/state and preserve it; expansion reveals it again. State clearly whether a batch command includes hidden selected descendants before applying it.
- For multiple selection, opacity/blend/visibility/lock show a value only if common; otherwise show “Mixed”/indeterminate. Explicit user action applies the chosen value to eligible selected items as one undoable transaction; communicate ineligible items before partial application, rather than silently changing a subset. No property changes just from selecting. Disable paint when no valid unique active target exists, with a clear explanation.
- Context actions are conditional on type, selection and lock state: rename, duplicate, delete, label, grouping, mask, clip/unclip and supported merges. Confirm destructive operations where recovery is not assured. Keyboard and screen-reader equivalents must exist for drag, toggles, hierarchy and menus. Tooltips, textual state and visible focus do not depend on hue.

## 3. Canvas Viewer

The viewer displays a fitted overview of the **whole current canvas**, reflecting current composite and transparency, independently of main-canvas zoom. The contrasting outlined viewport represents the part currently visible in the main editing view. Dragging it pans the main canvas; clicking elsewhere recentres the main view. If the main view extends past canvas edges, render the intersection and provide a clear edge/position cue; do not confuse clipping of the outline with the actual viewport. For rotation or mirroring, use the actual document-to-view transform: represent a rotated visible region accurately (polygon if necessary), not a falsely axis-aligned rectangle. At very high zoom keep an accurate tiny outline plus a usable minimum-sized *interaction target*, not a falsified viewport size. Keyboard arrows pan, with a documented larger-step modifier; provide accessible position/zoom descriptions.

The minus/slider/percentage/plus controls operate **main canvas zoom**, not viewer preview scale. Fit canvas fits the main canvas in its viewport; Centre canvas centres it without unexpectedly changing zoom. Synchronize on document, canvas size, viewport resize, zoom, pan, rotation and content updates; avoid feedback loops between overview and canvas. Empty/no-document and temporarily unavailable preview states need explicit messages. Cached, downsampled updates should remain responsive for large sparse documents; rendering details and performance budgets require measurement.

## 4. References

The independent References workspace imports multiple images, organizes them into named collections, and navigates a collection with previous/next, a thumbnail strip and selected-image name/index. Import accepts multiple images; collection management provides create, rename, reorder, move/remove and delete with clear confirmation semantics. Removing a reference does not delete the original file by default. Selecting an image restores its own zoom, pan and rotation; drag pans, zoom controls and fit affect only that reference, and rotation can be entered/reset independently. The panel never changes the editing canvas transform, adds an artwork layer, or exports references as part of the artwork. Reference **layers** above are a separate explicit document feature.

The default tab is space-efficient; **Open alongside** detaches/splits References into an independent dock or floating panel, keeping the reference visible and usable while Layers is active and the user draws. It can be redocked, closed and reopened without losing its collection/view state. Test floating-window focus and drawing on the canvas. In the image, the rotation and zoom numbers refer to the independently displayed reference. The thumbnails are schematic.

Persistence needs an explicit product decision: propose document-associated collections with embedded image data for portable projects, optional linked files only with clear missing-file/relink handling, and workspace-stored panel arrangement. Avoid silently converting links to embedded data or removing source files. Consider file size, image decoding, colour profiles, privacy of embedded reference material, autosave/recovery and compatible project format migration. If implementation chooses a different scope, surface it clearly before release.

## 5. Appearance and accessibility

Dark uses neutral charcoal and grey with slightly white icons; light uses almost-white and light grey with dark icons. Neutral surface changes, text weight, thin edges and focus shapes communicate state; no decorative coloured outlines, gradients, glows or card stacks. Functional colour is reserved for artwork, colour selection and user-chosen labels. Use modest separators only to clarify dock and row structure. Keep controls compact but not tiny; verify target size, text scaling, contrast, truncation, focus and hover in both themes, including high-DPI, keyboard, screen reader and touchpad. WCAG 2.2 [text contrast](https://www.w3.org/TR/WCAG22/#contrast-minimum) and [non-text contrast](https://www.w3.org/TR/WCAG22/#non-text-contrast) are useful benchmarks, not a claim of certification.

## 6. Current source versus proposal

This section describes inspected source, not a verified running build. The checkout already contains earlier uncommitted implementation work; this proposal does not change it.

| Capability | Current source evidence / caveat | This proposal |
| --- | --- | --- |
| Layers | `Layer`/`LayerModel` provide a flat raster-layer model and properties including name, visibility, lock and opacity; Layers UI uses the active layer and has an opacity slider/field. Source includes document-fit thumbnails and coalesced updates. | Adjustable presets, richer hierarchy, batch semantics and performant thumbnail lifecycle. Existing thumbnail code and new layer tests have not been verified in a full Qt application build. Opacity undo is not established. |
| Structure and compositing | No evidence in the inspected model of the proposed nested folders, vector layers, attached masks, clipping relationships, colour labels or multi-selection. Some UI controls are unfinished. | Full behavior above requires model, compositing and UI work; icons alone are not functionality. Blend mode and alpha lock must be genuinely implemented before enabled. |
| Canvas view | Existing canvas has zoom, fit, pan and view-centre state. | The overview, draggable viewport, synchronization and centre action as specified are proposed, not an existing navigator. |
| References | No independent reference workspace established in the inspected right-dock implementation. | Collections, per-image transforms and alongside mode are proposed. |
| Colour / docks | Colour selector, Palette, Color Mixer, Assets and docking have source implementations or earlier work; mixer has a pigment-inspired library integration, palette is simple, Assets is presently empty. Full application behavior was not built/verified in this environment. | New grouping, independent splitting and complete restoration are design goals, not a guarantee of current behavior. Square and triangle selector alternatives remain valid. |

The missing Qt development SDK prevented a full compile/run in prior work. Do not treat illustrations, isolated source inspection or prior tests as proof of end-to-end correctness.

## 7. Dependencies, risks and validation before implementation

1. **Model and renderer:** define stable layer IDs, ownership tree, vector/reference payloads, attached masks, clipping graph and compositing rules before the UI. Document how visibility, opacity, blend and alpha lock interact with groups, masks and references. Preserve existing painting behavior.
2. **Transactions and persistence:** batch edits, reorder/reparent/clip changes, rename and labels require atomic undo/redo. Version the project schema, preserve old documents, define forward-compatibility and lossless or explicitly warned fallback. Test save/load, autosave and crash recovery for all new types and references. No unsupported promise that current undo covers every existing action.
3. **Scale and performance:** virtualize rows; cache thumbnails by stable identity, content generation and size; invalidate for paint, masks, visibility, compositing and document size as appropriate. Coalesce frequent edits; render asynchronously where safe, cancel stale work, bound caches and prevent late results overwriting newer ones. Large sparse canvases and complex nested composites need profiling without imposing arbitrary layer-count, nesting or artwork-complexity limits.
4. **Interaction correctness:** test selection anchor, hidden descendants, mixed properties, disabled/inherited states, invalid drag targets, clipping runs, per-type menus, keyboard alternatives, hover-wheel focus and scroll isolation, touchpad inversion and both endpoint behaviors.
5. **Docking and transforms:** test split/floating/redock, restore on changed screen geometry, per-document switching, no-document states, canvas rotation/mirroring, extreme zoom and precision. Ensure references remain visible while drawing without stealing keyboard shortcuts.
6. **Visual and accessibility:** compare all three densities at realistic widths and display scales in both themes, verify actual layer previews are legible, screen-reader labels and non-colour state cues, and contrast of both viewport outline and artwork under it. Test with real documents and users rather than assuming the schematic dimensions work.
7. **Scope and rollout:** add data structures and safe compatibility first, then functional layer operations/undo, then workspace panels and performance polish. Disable unsupported actions with explanation until backed by real operations; never ship decorative controls that imply working clipping, blend modes, alpha lock, vector editing or wheel behavior.

## 8. Decisions for review

1. Approve the default two-group arrangement and the split/float path for keeping Viewer or References alongside Layers?
2. Are 48 / 32 / 24 logical-pixel starting presets and thumbnail-only wheel gestures comfortable, including the disable preference?
3. Approve same-parent clipping runs and the proposed reject/explicit-unclip policy for conflicting moves?
4. Should references be embedded by default, linked, or selectable per import? Should collections follow the document or be shared across documents?
5. What exact canvas/export and tool behavior should a document reference **layer** have, distinct from the independent reference panel?
6. Which folder compositing/isolation and multi-selection batch policies should be finalized before implementation?

No application functionality is changed by this proposal; approval does not itself implement it.
