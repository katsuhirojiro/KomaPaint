# Brush Studio — visual mockup

`brush-studio.png` (1600 × 1000) is a design mockup only. No application code was
written or changed for it: nothing here is wired into `src/` or `ui/`, and Brush
Studio does not exist yet.

![Brush Studio mockup](brush-studio.png)

## Why it looks like this

Every colour, size and type step comes from `ui/themes/KomaTheme.qml`, so the
window reads as a KomaPaint window rather than as a generic dark editor:

| element | token / value |
| --- | --- |
| window + command bar | `windowBg` `#2c2c2c` |
| docked panels | `paneBg` `#383838`, headers `paneHeaderBg` `#2c2c2c` |
| canvas desk | `canvasBg` `#454545` |
| fields, wells, wells in lists | `inputBg` `#292929` |
| selected row / active tool | `rowSelected` / `toolActive` `#505050` |
| hairlines between regions | `hairline` `#5c5c5c`, one pixel only |
| primary action (Save Brush) | `accent` `#c8c8c8` fill with `textOnAccent` ink |
| type | Inter, 9 / 10 / 11 / 12 px, micro caps at 9 px with 0.9 px tracking |
| spacing | 4 px grid, `panelInset` 10, control height 24 |
| icons | 1.5 pt single-colour line art, tinted `#dedede`/`#909090` |

The dark theme has no saturated accent on purpose — ink (`accent`) carries
structure and selection, so the only "loud" element in the window is the one
commit action. That is why there is one light button and not a palette of them.

Transparency is painted with the theme's own checkerboard
(`transparencyLight`/`transparencyDark`), not a generic one.

## Window anatomy

```
┌──────────────────────────────────────────────────────────────┐
│ 30  title bar — Brush Studio · Krita5 prototypes 1           │
│ 40  command bar — name · engine · New/Duplicate/Import/Export│
│     … Save Brush · Close                                     │
├────────┬─────────────────────────────────────────┬───────────┤
│ 248    │ 34 rail │ 32 tab strip                  │ 300       │
│ left   │         │                              │ brush     │
│ brush  │         │ 1024 × 320 mask, 88 %        │ settings  │
│ compo- │         │ painted tip + pressure tests │ (accordion│
│ nents  │ 18 ruler                                 │  + sticky │
│        │                                          │  footer)  │
├────────┴─────────────────────────────────────────┴───────────┤
│ 152  live preview — stroke sheet · stamp strip · engine notes │
│ 24   status bar — component counts · engine · saved state     │
└──────────────────────────────────────────────────────────────┘
```

## The five areas from the brief

**A. Command bar.** Brush identity (thumbnail + name with a library dropdown),
a Digital / Realistic engine selector with the warning affordance KomaPaint
already uses for experimental engines, and New / Duplicate / Import / Export on
the left of the group separator. Save and Close are separated on the right
because they commit or abandon the whole editing session, not the brush.

**B. Brush components.** The list is the brush's own stack: three tips, two
textures and one imported image, each tagged `TIP` / `TEX` / `IMG` / `CST` so
the type is readable at a glance. The header counts them; the chips under it
summarise what the current brush is built from; the footer is where a new
component or a dropped file arrives. Selection is the theme's `rowSelected`
fill plus a 2 px accent bar, exactly as the brush library uses elsewhere.

**C. Working area.** The tab strip says which component is being edited
(`Tip · Bristle smear 03`), the floating chip states the document
(mask, 8-bit, 1024 × 320, edited), and the marquee is drawn as marching ants
with corner and edge handles. The rail on the left is the drawing toolbar —
brush, eraser, smudge, shapes, selection, lasso, move, fill, eyedropper, zoom —
in the same 34 px tool buttons the main window uses. The ruler is in document
pixels, not screen pixels, so it stays meaningful at 200 %.

**D. Brush settings.** Thirteen categories, collapsed to a single line each with
their current values as a summary, so the panel stays scannable. Only the two
that are open — Brush Tip and Size — show controls. Each category resets
independently; the sticky footer counts what has been changed and offers one way
back. Nothing here is a wall of sliders.

**E. Live preview.** A large stroke sheet (three directions, pressure 20 → 100 %),
the stamp strip showing the same dab at six pressure steps with its spacing, and
the engine-specific notes that explain what the preview is actually simulating.

## Deliberately deferred

- **Node graph.** Represented as one row inside `Advanced` — "Open node graph
  editor…" with a small optionality note. It opens its own workspace; it is not
  a mode of this window and must never be the default screen.
- **Settings search.** Icon present in the settings header; behaviour not
  designed yet.
- **A/B compare.** Button present in the preview header; the interaction
  (split view vs. alternating strokes) needs its own mockup.
- **Realistic-engine specifics.** The mockup shows the engine switch and its
  warning; it does not attempt to show the extra material settings that engine
  will add.

## Numbers in the mockup

`8.4 ms` preview, `60 px`, `82 %` flow and the component counts are illustrative
placeholders that match the shape of real values — they are not measurements and
should not be quoted as performance figures.

## Regenerating

`brush-studio-mockup.py` renders the PNG deterministically: it needs Pillow,
numpy and the Inter family (400/500/600), draws at 2× and downsamples, so every
edge and glyph is antialiased. It is a design artefact, not a build dependency —
edit it to try new proportions, then commit both the script and the new PNG.

## Open questions for review

1. Should the engine selector live in the command bar (as now) or inside the
   settings panel as its own first section? The bar keeps it visible while
   switching; the panel keeps the bar quieter.
2. The component list groups by brush order (tips first). Should it group by
   *type* instead, with tips, textures and imports as collapsible sections?
3. Preview occupies 152 px. At 1080p that fits; on a 768 px-tall laptop the
   settings panel would need to shrink first. Should the preview collapse into a
   tab of the settings panel at small heights?
