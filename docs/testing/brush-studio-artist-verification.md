# Brush Studio artist verification

This is a **human test of the integrated Windows application**, not the isolated
Brush Studio review prototype. Use the `KomaPaint-Windows-x64-Human-Test.zip`
from the CI run named in the hand-off. The ZIP contains the normal KomaPaint
application plus a `Human-Test` folder with the seven brushes and this page.

The brushes are test material. They are not installed by default and do not
change any product default. Each `.kb` can be opened, inspected, edited and
saved in Brush Studio like any other brush.

## Before drawing

1. Extract the ZIP and launch `KomaPaint.exe`. Use the isolated copy; do not
   point it at a working project you care about.
2. Create a white **1200 × 1000** canvas (or larger). Use a tablet/stylus for
   the pressure steps. A mouse reports full pressure, so it cannot exercise the
   light-pressure rows.
3. Open **Brush Studio**. Leave its private pad visible.
4. Keep a scratch folder for the `.kb` files you save during the walkthrough.

## For every brush

Repeat these five steps. Do not skip the save/reopen step: it is the part that
proves the Sub-Tool and the artwork survived together.

1. In Brush Studio choose **Open** and load the named file from
   `Human-Test/Brushes`.
2. Draw one stroke on the Studio pad at the pressure described below. Look at
   the pad before saving.
3. Choose **Save**. Save to your scratch folder with a new name ending in
   `.kb`. The save installs the brush under **Brush → Digital** in the Sub-Tool
   palette.
4. Close Brush Studio, select the new Sub-Tool, and draw the same gesture on
   the artwork canvas. Compare the two marks.
5. Save the artwork as a `.koma`, close the tab, reopen it, and confirm every
   row is still there with the same appearance.

If Save reports “Saved to file, not installed”, stop and record the exact
message. That is a failure, not a brush to work around.

## The seven rows

| # | Brush file | Draw it like this | Expected on pad and artwork |
|---|---|---|---|
| 1 | `01-round-ink.kb` | Firm, even speed; full pressure. | Crisp, fully opaque round line. Width stays constant; edges are clean, not fuzzy. |
| 2 | `02-pressure-pencil.kb` | Start light, press to firm, release light. | Thin, pale ends and a darker, wider middle. Visible graphite grain, not a smooth plastic tube. |
| 3 | `03-soft-airbrush.kb` | Light, repeated passes; do not scrub hard. | Broad soft cloud with no hard rim. It builds gradually and is never as opaque as the ink. |
| 4 | `04-small-low-pressure.kb` | Barely touch the tablet at about 20% pressure. | A small 4–6 px line. It must not disappear, jump, or turn into separated dots. |
| 5 | `05-large-varying-pressure.kb` | One long stroke, light to firm to light. | A wide wash whose width follows the pressure smoothly. No step changes or starved sections. |
| 6 | `06-high-spacing.kb` | Medium-fast, firm, one pass. | The 50% spacing is visible as a scalloped rim, but the stroke is continuous. Individual circles are a failure. |
| 7 | `07-changing-pressure.kb` | One long S-curve, very light → firm → very light. | The line swells and thins continuously through the gesture. No repeated circles at the light ends. |

## What “clearly distinguishes” means

These are deliberately different brushes, not three names for one engine path:

- **Ink** is hard, full opacity and constant width at full pressure.
- **Pencil** carries graphite grain and changes both width and opacity with
  pressure.
- **Airbrush** is a low-flow, zero-hardness soft tip. Its maximum opacity is
  much lower than ink and its edge falls away gradually.

Record which of the three you would actually reach for while drawing. If they
feel interchangeable, that is a finding even if every row is continuous.

## Save and reopen

After all seven rows:

1. Save the artwork once as `artist-check.koma`.
2. Close the tab.
3. Reopen the file.
4. Confirm all seven rows have the same width, softness and grain as before the
   save.
5. In the Sub-Tool palette, switch between the installed brushes. Each must
   still paint with its own document, not silently fall back to a stock brush.

## Pass/fail notes to send back

For each row, record:

- Pass / fail for continuity.
- Pass / fail for the expected ink, pencil or airbrush character.
- Pass / fail for Sub-Tool installation.
- Pass / fail for save and reopen.
- One sentence on how it felt: pleasant, usable with adjustment, or unusable.

## Known limitations to expect

- The Digital native renderer paints procedural round, textured, bristle,
  graphite and stipple tips, and bitmap tips whose hardness is 100. Masked and
  non-Normal components are rejected with a visible error; they are not silently
  approximated. The seven test brushes use only the supported subset.
- The “airbrush” here is the production low-flow soft-tip brush. It is not a
  particle/spray generator.
- Spacing is capped at 50%. The 50% brush is intentionally scalloped but still
  overlapping; do not expect a solid-edged line.
- Pressure requires a pressure-sensitive stylus or tablet. A mouse is a valid
  continuity check at full pressure, but not a pressure test.
- Component offsets and per-component scale remain outside the spacing fix;
  these one-component brushes do not exercise that separate limitation.
