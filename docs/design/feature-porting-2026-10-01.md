# Fork feature split — 2026-10-01

All branches and the merged main are for `barrulus/bumbriel`. No upstream PR or
upstream push is part of this work. Base: upstream commit `26cd1fcb`.

## Branches and porting order

| Branch | Base | Content |
| --- | --- | --- |
| `feat/audio-reactive-effects` | `26cd1fcb` | Optional PipeWire helper, source config, packed shader inputs, per-output cadence, isolated capture support, examples and tests |
| `feat/scene-transitions` | audio branch | Typed scene programs, snapshot composition, window transitions, workspace transitions/presentation and scene/audio integration |
| `feat/carousel-controls` | scene branch | Camera controls, picking, configurable backdrop, tests, and installed backdrop asset |
| `docs/effects-porting` | carousel branch | Preserved cross-feature design/evidence records and this handoff |

Audio is independently usable on upstream's existing effect kinds and is reserved
for weegs710 to submit: the idea and guidance were theirs. The other feature
branches are stacked; compare each against the base listed above to review its
own changes. Once a dependency lands elsewhere, replay only the dependent branch's
own commits. No authorship has been impersonated; weegs can submit from their fork.

The old `feat/scene-audio-effects` branch remains available unchanged. The fork's
previous main (`0eaaf298`) is preserved as `archive/main-before-feature-split-2026-10-01`
and remains in main's ancestry. The new main uses the newer feature tree, as requested;
it does not restore the obsolete shader collection/configuration format.

## Rebase changes

- Retain upstream's native Xwayland implementation; omit the obsolete satellite supervisor edit.
- Keep upstream's view file split and place animation/lifecycle/state changes in their new files.
- Preserve upstream's per-effect scaling; uniform-only updates reject scale changes so they use normal geometry damage.
- Exercise the audio helper supervisor under upstream's process-wide child reaper.
- Update the window scene config fixture for the optional carousel backdrop stage, checking presence before dereferencing it.
- Install the carousel backdrop beside the preset that references it.

## Validation

- Audio: build with `audio_helper`, tests and test IPC enabled; 115 Meson tests passed.
- Audio: all eight audio compositor checks passed; the additional per-output TIME check passed.
- Scene before carousel controls: 131 Meson tests passed.
- Combined rebased feature tree: all 310 headless compositor checks passed.
- Combined tree initially exposed the stale optional-stage fixture; its corrected config-load and native-reaper supervisor tests passed.

The tests use isolated headless compositors and private PipeWire fixtures. They do
not claim fresh interactive native-session or physical microphone/device validation.
The original checkout acquired unrelated, uncommitted `workspace-ripple` edits
during this work; those were left untouched and are outside this split.

## Shader audit

The old main contains 122 user-facing shader files across `docs/examples` and
`examples`. They were compared with the new branch's examples and the local
`~/.config/umbriel/shaders` collection. Shader fixtures in `umbrielfx/tests` were
excluded. Exact matches ignore whitespace and comments. Same-name or similar-code
matches indicate likely ports, not pixel equivalence.

**Not every old shader can be confirmed as ported.** In particular, the local
collection has no identified ports for film-grain, fire, fisheye-rgb, pixel-mosaic,
the eight lightning/melt/ripple/whirlpool open/close programs, and several older
cursor programs (adaptive, blueglow, comet variants, ripple and trail). Older
screen grayscale/warmtint and standalone neon/portal bleed variants also remain
unverified. A vignette exists in the new examples, but its formula differs from
the old screen vignette. These originals are preserved by the archive/history.

Renamed or superseded candidates include rainbow-ripple → rainbow,
rainbow-ripple-overlay → rainbow-overlay, adaptive-text-v3 → adaptive-text-v4,
and rorschach → rorschach2. Their existence does not prove every old variant was
retained. Most other ring/window shaders have identifiable local counterparts.

See [the full shader inventory](shader-port-audit-2026-10-01.md) for every path
and candidate. No local configuration files were changed by the audit.
