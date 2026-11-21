# 2026-11-21 — Premiere-style NLE visual redesign

Four parallel waves restyled the whole video_editor UI to a modern
NLE reference (deep dark, cyan/teal accents, icon-first chrome).
Verified: build clean, `--smoke` 39/39 OK, live screenshot on a demo
project confirmed all elements.

## Theme foundation (wave A)

`Theme::Tokens` gained — appended at end, additive only
(`src/ui/theme/tokens.h`):

| Token | Dark | Use |
|---|---|---|
| `timecode` | #22d3ee | mono digit readouts |
| `waveform` | #2dd4a0 | waveform traces, meters |
| `track_video` | #3b82f6 | video track chrome |
| `track_audio` | #2dd4bf | audio track chrome |
| `playhead` | #22d3ee | playhead line |
| `warning_chip` | #fbbf24 | amber fx badge on clips |

`VeTheme::kTimecode/kWaveform/kTrackVideo/kTrackAudio/kPlayhead/
kWarningChip/kWarningChipText` mirror them. editor.yaml `Accent`
→ `#22D3EE`. Palette: bg #141417, panel #1a1a20, surface #222229,
input #0e0e12, border #2d2d35; radius_md 7, radius_lg 10.

Shared widget pass: ghost button = muted→foreground glyph lift on
hover; `Item` selected = wash + 2.5px accent edge bar; `Tabs`
underline = surface slab + accent bar; `Badge` rounder; `ScrollArea`
rail only on hover. `VeIcons::DrawToggleIcon` for state pairs.
17 new icon mappings (transport.*, track.*, edit.snap/razor/marker)
— SVGs were hand-authored locally (material-icons path data), not
fetched; swap for real Material Symbols later if desired.

## Panels

- **Monitor** (`monitor_widget.cc` + `source_monitor`): card chrome,
  `SetTitle` + ≡ row, centered 5-icon transport, mono accent
  `HH:MM:SS:FF` at font_title.
- **Nav rail**: ghost monochrome icon strip, accent edge indicator.
- **Timeline** (`timeline_widget.cc`): V/A badge chips, eye/lock/
  M/S/mic icon buttons, rounded clip fills (blue/teal, alt hue),
  amber `fx` chip on effected clips, teal peaks, cyan pill-cap
  playhead, mono ruler labels, corner ghost tool row (magnet wired
  via `on_snap_toggle` at video_editor_app.cc:1573).
- **Right dock**: `VeIcons::DrawSectionHeader` collapsible headers on
  color/effects/inspector/layers/markers/history/mixer panels;
  `color_panel_widget` rewritten — hue wheel (vector-drawn, drag →
  `SetOnCorrection` → replace-or-add `ClipEffect`, undoable) +
  Temperature/Tint sliders with mono readouts; `mixer_panel` —
  vertical teal dB meters, 0/-6/-12/-24 ticks, peak-hold.

## Literal colors left for future tokens

Timeline keeps `kAccentCyan` (active rings), `kClipVideoFill{,Alt}`,
`kClipAudioFill{,Alt}` literals — candidates for `clip_*` tokens.

## Process notes

- Mid-flight commit `ee73eaa` ("refresh NLE theme and color
  correction UI", author repo owner) bundled several waves' files;
  remaining uncommitted bits were the token-adoption edits in
  timeline/mixer/color_panel.
- Build lock contention was heavy: parallel `video_editor.exe`/
  `xer_engine_ui_render.exe`/`xer_browser_shell.exe` instances held
  `bin\*.dll`. `Build.ps1`'s lock guard is the sanctioned path;
  killing stale PIDs also works.
