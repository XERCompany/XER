# video_editor `ui2/` — the editor shell

The VideoEditor UI per `docs/wiki/architecture/ve-ui-rebuild-spec.md`.
**ui2 is the sole shell** — the legacy `tools/video_editor/ui/` tree
was deleted at the swap-over (2026-11-22); the `--ui2` flag and
`XER_VE_UI2` env switch no longer exist and `video_editor` boots
`Ui2::VeApp` directly. The Photo workspace is pending its ui2 rebuild
(M4).

## Layout (spec §5)

```
AppTitleBar ─ workspace switcher · File/Edit/View/Help menus ·
              center command pill (Ctrl+K palette) · left/right
              dock toggles · window controls
─────────────────────────────────────────────────────────────
nav rail │ left dock            │ monitors      │ right dock
Sidebar  │ Tabs: Media Effects  │ Resizable     │ Tabs: Inspector
(collapse│  Audio Text Trans    │ Source|Program│  Color Markers
 d 46px) │                      │  pair         │  History Mix
         │                      │               │  Queue Ext
         │                      │               │  Scopes Trn
─────────────────────────────────────────────────────────────
TimelineStrip ─ tool tabs (V C B N Y U R H Z) + snap + tracks Surface
StatusStrip   ─ status text + autosave/jobs/proxy badges
```

All major bands are `Resizable` panels; `SetOnLayout` captures resolved
fractions into `ShellPrefs` (`ui2_shell.json` next to catalog.db,
overridable per-instance for tests).

## Files

| file | role |
|------|------|
| `ve_app.{h,cc}` | `VeApp` shell widget — structure, menus, palette, workspaces, persistence |
| `ve_theme.{h,cc}` | palette/metric constants + `DarkTokens()`/`ApplyToWindow()` port of `ui/ve_theme.h` onto `Theme::Tokens` NLE fields |
| `workspace_layout.{h,cc}` | `WorkspaceLayout` contract + built-in presets |
| `surface_hooks.h` | `TimelineHooks`/`MonitorHooks`/`MediaLookup` contracts for M2+ surfaces (ported from `ui/timeline_widget.h`) |
| `shortcut_router.{h,cc}` | window-level shortcut dispatch (see below) |
| `shell_widgets.{h,cc}` | `LinearBox`, `DockTabGroup`, `PlaceholderPanel`, `StatusStrip`, `TimelineStrip` |
| `shell_prefs.{h,cc}` | `ShellPrefs` JSON load/save (split fractions, visibility, tabs, workspace) |
| `ui2_shell_smoke.{h,cc}` | `ui2_shell` headless check |

## Shortcut router contract (G7)

`ShortcutRouter` is installed via `WidgetTree::SetKeyHandler` — the same
hook the old shell used (`VideoEditorAppWidget::HandleEvent`). Dispatch
order is the kit's:

1. the focused widget gets the event first (text fields eat letters);
2. a visible overlay gets the next shot;
3. only when neither consumed it AND no modal overlay is active does
   the key handler run.

So unmodified letters (J/K/L, I/O, V/C/B/N/Y…) are safe — they arrive
as `Event::Type::TextInput` (the shared `KeyCode` enum only reports
letters with Ctrl held; the router normalizes both channels).
`KeyDown` covers Space/arrows/Home/End/PageUp/Down/Delete/F5/Escape
and `Ctrl+letter` chords; Alt+letters arrive as `TextInput` with
`is_alt`.

Scopes implement Premiere's last-active-viewer rule: J/K/L (and
source marks) are bound separately under `kScopeProgram`/`kScopeSource`;
`ActivateScope()` — wired to `MonitorHooks::on_activate` in M2 — picks
which fires. Scope bindings shadow `kScopeGlobal`; a handler returning
`false` declines and dispatch falls through.

## Placeholders vs real

The file table above describes the M1 skeleton; M2/M3 since landed the
real monitors (`monitor_panel`/`monitor_surface`/`preview_binding`),
timeline (`timeline_strip`/`timeline_surface`/`timeline_tools`/
`timeline_bridge`), and dock panels (`media_panel`/`effects_panel`/
`inspector_panel` + `bin_surface`/`drag_session`). Remaining dock tabs
are still `PlaceholderPanel`s.

## Boot

```bash
video_editor [project.xerproj]         # the ui2 shell is the only UI
video_editor --smoke                   # includes the ui2_* + kit checks
```
