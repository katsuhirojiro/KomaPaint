# Speed Dynamics

Speed Dynamics turns how fast the pointer moves into a pressure-like brush
response. It is an **application input capability**, not a property of a brush
file: the master control, the enabled properties and the direction are personal
artist settings, stored with the rest of the input configuration and never
written into a `.kb` document.

It exists because a mouse, a finger and a non-pressure pen always report the
same pressure. Speed is the one expressive signal those devices do have, and it
is useful to a pressure pen too — a quick flick can thin a line without the
artist changing brushes.

## What the artist sees

`SpeedDynamicsSection.qml` is shown in the normal brush properties and in the
native `.kb` brush properties, with a single signed **Speed influence** slider:

| Master | Meaning |
| --- | --- |
| `-100` | slow strokes become thicker/stronger; fast strokes become thinner/lighter |
| `0` | neutral: speed has no influence; strokes are exactly speed-independent |
| `+100` | slow strokes become thinner/lighter; fast strokes become thicker/stronger |

The strength grows continuously with distance from the centre. Returning to the
centre restores speed-independent painting. Double-click resets to neutral;
Shift-drag gives fine control. Four checkboxes choose whether **size**,
**opacity**, **flow** and **spacing** respond. Opacity and size are on by
default; flow and spacing are opt-in because they are more specialised.

A live plain-language sentence under the slider repeats the current behaviour
and names the properties that are responding. It also states that real pen
pressure is still applied. The same control and an equivalent summary appear in
Settings → Input → Tablet, so the setting can be found outside the painting
panel too.

Every control supplies a `help` string to the shared `PropertySlider`. The
question mark explains the control on hover, on keyboard focus and when tapped
(touch), in addition to the visible summary sentence.

Brush Studio exposes the creator's **response weights** as four ordinary
parameters in the Dynamics section:

- `speed-size` — how much this brush may change size with speed;
- `speed-opacity` — how much its opacity may change;
- `speed-flow` — how much its flow may change;
- `speed-spacing` — how much its spacing may change.

Zero removes that property from Speed Dynamics for this brush, whatever the
artist's master is. The weights are part of the brush document; the master and
the checkboxes are not.

## Signal

`PointerStrokeArbiter::process()` feeds every pointer sample through
`input::SpeedTracker`:

- distance and elapsed time are measured between chronological samples in
  view/screen space, so zooming the canvas does not change the response;
- the interval is clamped to a plausible range, and a gap longer than 0.25 s
  starts a fresh measurement instead of reading as an extremely slow stroke;
- an exponential smoothing constant in **time** (`0.045 s`) replaces a fixed
  per-sample weight, so a 1 kHz stylus and a 50 Hz mouse produce the same value
  for the same physical motion;
- stationary samples are treated as zero speed and the value settles visibly;
- equal, backwards, zero, NaN and infinite timestamps or coordinates leave the
  previous value untouched;
- the speed of a sharp reversal is positive (speed is a magnitude, not a
  direction), and a short stroke still produces a bounded value;
- `Down` begins a tracker and resets the speed to zero; `Up`, `Cancel` and
  `Leave` reset it after publishing the final value.

`PointerSample::speedNormalized` is the normalized `[0,1]` result, with
`referenceMaxSpeedPxPerSec` as the top of the range. The existing stabiliser
smooths geometry only; it passes this signal through unchanged because Speed
Dynamics has its own time-based smoothing.

## Mapping

`input::speedFactors(config, speed)` is the single mapping used by every
renderer. It clamps the configuration and the signal, returns neutral factors
when the master is zero or no property is enabled, then:

- `size      = clamp(1 + master × weight × direction × signal × 0.85, 0.15 … 2.5)`
- `opacity   = clamp(1 + … × 0.90, 0.05 … 1.0)`
- `flow      = clamp(1 + … × 0.90, 0.05 … 1.0)`
- `spacing   = clamp(1 − … × 0.85, 0.20 … 2.5)`

`signal` is `2 × speed − 1`: slow is negative, the normal drawing speed is the
midpoint, and fast is positive. Spacing intentionally moves the other way — a
"stronger" stroke lays its stamps closer together. Every result is finite and
bounded for every input.

### Digital

`BrushStroke` applies the factors in one place through `withSpeed()`:

- size scales the footprint used by `BrushRasterizer::effectiveDiameter()` and
  the spacing step;
- opacity and flow scale the per-dab values;
- spacing scales `BrushDefinition::spacing` before `spacingFor()` chooses the
  distance between stamps;
- taper is applied after the size factor, so a taper still reaches the same
  relative shape;
- the factors multiply whatever pressure already produced. A pressure pen's
  pressure response is not replaced or bypassed.

Document strokes also receive the signal in `StampContext.input.speed`, so an
authored `Signal::Speed` binding is evaluated per stamp by `BrushEvaluator`.
Only size, opacity, flow and spacing are accepted as targets; other targets are
reported by the capability inspector.

### Realistic

`MaterialStroke` applies the same factors:

- size scales the material dab footprint and therefore its spacing step;
- opacity scales `contactTransfer`, which is the existing control on how much
  paint a contact deposits;
- flow scales `releaseRate`, the existing control on paint released per dab;
- spacing scales `spacingRatio`.

The authored material `speedTransfer` curve is separate and remains honoured by
the material engine. Speed Dynamics does not invent a second paint model.

## Pressure

Physical pressure stays primary. The enabled Speed Dynamics factors multiply
the definition after pressure has been applied, so:

- a pressure-sensitive pen keeps its full pressure response and gains the speed
  response on top;
- a mouse, finger or non-pressure pen keeps its device pressure (usually a
  constant) and gains a pressure-like variation from speed;
- a real pressure report is never overwritten, and the UI never calls the speed
  value "pressure".

## Supported and unsupported

Genuinely rendered and tested in both engines: size, opacity, flow and spacing.

Stored and preserved but never claimed as a Speed Dynamics property: every
other brush parameter. Rotation, scatter, texture, colour variation and the
physical-paint family do not respond to the master control. Brush Studio still
shows their own authored response controls, and the feature matrix marks what
the renderers actually read.

Limitations that need real-device verification:

- speed quality depends on the driver's timestamps; a driver that reports
  identical timestamps cannot produce a useful speed and the tracker keeps the
  previous value rather than inventing one;
- extremely low report rates (< ~30 Hz) make the signal coarse even though it
  is rate-independent in the steady state;
- pressure and speed together are covered by automated pixel tests, but the
  subjective feel on a particular tablet, mouse or touchscreen still needs
  human testing;
- Realistic flow maps to release rate, so a brush with an almost empty
  reservoir may exhaust it sooner when speed increases flow; this is the same
  physical behaviour as increasing flow by hand.

## Verification

- `tests/unit/TestSpeedDynamics.cpp` — the signal, neutral centre, both
  directions, monotonic strength, independent properties, authored weights and
  inversion, invalid input, sample-rate independence, stationary/short/gap
  strokes, sharp turns, bad timestamps, arbiter publication and reset.
- `tests/unit/TestSpeedRendering.cpp` — Digital and Realistic pixels: neutral
  identity, both directions, size/opacity/flow/spacing independently, a
  mid-stroke speed change, pressure combined with speed, and stored document
  weights.
- `tests/unit/TestBrushUi.cpp` and the Studio model suites cover the packaged
  controls and the new parameters.
