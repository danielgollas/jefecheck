# Customizable keyboard shortcuts — design

Status: draft, awaiting owner approval. Base: `qt-experimental` (2.0 line).
Order of the three related projects (owner-agreed): **1. shortcuts (this doc)** → 2. timeline zoom and In/Out (built on `feature/panel-polish-E`, which has the ruler) → 3. multi-clip edit tracks (`2026-11-22-multi-clip-tracks-edl-design.md`).

## Why

JefeCheck 2.0 is aimed at **new people**, most of whom already know Premiere, Resolve, Final Cut or Avid. JefeCheck's keys come from the 2006 FLTK app and differ from the editing convention in places people hit early (`Shift+I`/`Shift+O` reset In/Out here; everywhere else they jump to In/Out). The owner wants:

- a Shortcuts section in Preferences where any key can be changed;
- presets, with an **Editing** preset (the NLE convention) as the default and a **JefeCheck 1.7** preset that matches what shipped 1.7 did;
- a Final Cut–style **keyboard map** in the first release, not later;
- **import / export** of shortcut sets so a studio can hand everyone the same keys;
- **J / K / L** transport.

Success: someone from Premiere or Resolve presses I, O, Shift+I, Option+X, J, K, L, Space, arrows and gets what they expect; a 1.7 user picks one preset and gets what shipped 1.7 did; every place that names a key (menus, tooltips, the `H` overlay, the manual) shows the current key.

## Goals

- One list of commands (the registry) is the only place a key is bound.
- Every key binding in the app today moves into it (phase 1 changes no behaviour).
- The 1.7 commands 2.0 dropped come back (channels, track 1–4, favourites, LUT scroll).
- Two built-in presets plus user customization ("Custom" = a preset with overrides).
- Preferences → **Shortcuts** page: keyboard map, searchable command list, key capture, conflict warning, reset, import, export.
- First-launch preset chooser.
- J/K/L, clear In/Out commands, and slots for the timeline project's commands.

## Non-goals

- Mouse button / gesture remapping (wheel zoom, Alt-click load, drag-to-offset stay fixed).
- Multi-key chords (`Ctrl+K, Ctrl+S` style). Single key + modifiers only.
- Per-panel different meanings for the same key beyond the two scopes below.
- JKL speed ramping (2×, 4× on repeated L). Later; see "Later".
- Remote sessions sharing keys. Keys are local; the commands they trigger already sync as today.

## Anti-goals

- A 1.7 user who picks the 1.7 preset finds a shipped-1.7 key doing something else. (Tested key by key against `main`.)
- Typing into a text field, spin box or the chat overlay fires a shortcut.
- Arrow keys stop navigating lists (Playlist, Notes, LUT list) because a global shortcut stole them.
- A key shown in a menu, tooltip or the help overlay is not the key that actually works.

## Constraints

- §1 TU rule: the registry and the Preferences page are Qt-side (`src/qt/`). Commands call existing `jefe::qt::*` bridge functions. The help overlay text lives in `gfcplatemanager.cpp` (rendering chain); the Qt side formats the text and hands it over through one new bridge setter, `jefe::qt::setHelpMessage(std::string)`.
- §41 density rule for the new page (13 px text, 24 px controls, no `setFixedHeight`).
- Object names follow the dotted-leaf scheme (`prefs.shortcuts.*`) so the Appium tests can find them.
- Qt maps `Qt::CTRL` to Cmd on macOS and `Qt::META` to Control; the registry stores Qt's portable text form (`QKeySequence::PortableText`), so a set exported on a Mac imports correctly on Windows (Cmd ↔ Ctrl).
- The existing `--plate-manager-test` "WINDOW menu" check (no two menu actions share a key) must keep passing; it extends to the registry.

## Today (recon)

Keys are bound in four places:

| Where | What | Count |
|---|---|---|
| `MainWindow_qt.cpp` menu `QAction`s | File, View, Window, Help items (Ctrl+O, F, V, M, T, F2–F9, H, N…) | ~30 |
| `MainWindow_qt.cpp` standalone `QShortcut`s | Ctrl+1–4 layouts, Ctrl+, prefs, all-plates variants (Shift+F, Shift+V, Shift+M, Alt+T, Ctrl+Alt+R, Shift+Alt+R) | ~13 |
| `GlViewport_qt.cpp:394` `keyPressEvent` switch (viewport focus only) | Space, Left/Right, X/C, Home/End, Up/Down, 8/9/0, `<`/`>`, I, O, Shift+I/O, Alt+I, Escape (pencil), Return (chat) | ~20 |
| `GlViewport_qt.cpp` held-key drags | W gamma, E exposure, Q brightness, D contrast, S saturation (hold + left-drag) | 5 |

Also bound inside single panels (stay there, see Scopes): Playlist Enter / Delete / Backspace / Shift+Up/Down, the LUT cube's F, the FX scrub field.

Shipped 1.7 (`main`) bound more keys than 2.0 does today; see Presets.

Text that names keys by hand: the help overlay string (`gfcplatemanager.cpp:42`), tooltips across `src/qt/`, `docs/manual.md`, `docs/quick-start.md`.

## Approaches

- **A. Every shortcut is a `QAction`, all owned by one registry. (Chosen by the owner.)** Each command has an id, name, category, scope and a key per preset. Viewport keys move out of `keyPressEvent` into actions. Menus show keys automatically; `QKeySequenceEdit` gives capture; Qt's `ShortcutOverride` already keeps single-letter keys out of text fields. Held drag keys are a second, small list in the same registry.
- B. Own command table and dispatcher in an app-wide event filter. Full control, but re-implements Qt shortcut resolution and menus need their key labels synced by hand.
- C. Only menu shortcuts customizable. Cheap, but the keys people use most live in the viewport. Misses the goal.

## Design

### Registry — `src/qt/Shortcuts_qt.{h,cpp}`

```cpp
namespace jefe::shortcuts {
enum class Scope { Global, Player };       // see "Scopes"
enum class Category { File, Playback, InOut, Timeline, Plate, Color, Layout, Panels, Help };

struct Command {
    const char* id;            // stable, e.g. "playback.play-pause"; never renamed once shipped
    QString     name;          // shown in Preferences, e.g. "Play / Pause"
    Category    category;
    Scope       scope;
    QList<QKeySequence> editing;   // keys in the Editing preset (0–2)
    QList<QKeySequence> classic;   // keys in the JefeCheck 1.7 preset (0–2)
    std::function<void()> run;
};

struct HoldKey {               // hold-a-key-and-drag; not a QAction
    const char* id;            // e.g. "color.drag-gamma"
    QString name;
    Qt::Key editing, classic;  // Qt::Key_unknown = unbound in that preset
    // Optional: hold this key, then press one of `with` (e.g. hold L + Up/Down).
    // Empty = hold-and-drag. Each entry runs its own action.
    QList<QPair<Qt::Key, std::function<void()>>> with;
};

class Registry {
public:
    static Registry& instance();
    void registerCommand(Command c);         // at startup, before attach()
    void attach(QWidget* window, const QList<QWidget*>& playerWidgets);
    QAction* action(const char* id) const;   // menus add these; never create their own keys
    QKeySequence key(const char* id) const;  // current key, for tooltips / help
    QString label(const char* id) const;     // native text, e.g. "⇧I" on macOS, "Shift+I" elsewhere
    Qt::Key holdKey(const char* id) const;

    QString preset() const;                  // "editing" | "classic"
    void setPreset(const QString&);          // keeps no overrides
    void setOverride(const char* id, const QKeySequence&);   // empty = unbound
    void clearOverride(const char* id);
    bool isCustom() const;                   // any override present
    QStringList conflicts(const char* id, const QKeySequence&) const;  // same scope or Global↔Player

    bool exportTo(const QString& path) const;
    bool importFrom(const QString& path, QString* error);
signals:
    void changed();                          // tooltips, help text, Preferences refresh
};
}
```

- Commands are registered in one file (`ShortcutCommands_qt.cpp`) so the full list is readable in one place.
- Menus keep their structure but get the registry's `QAction` instead of building their own (`viewMenu->addAction(reg.action("plate.fit"))`).
- Every existing `QShortcut` and every key case in `GlViewport_Qt::keyPressEvent` is removed. The viewport's `keyPressEvent` keeps only: chat-mode typing, Return-to-chat, Escape-to-put-the-pencil-down, and held-key tracking (now keyed by `Registry::holdKey`).

### Scopes

- **Global** (`Qt::ApplicationShortcut`): fires wherever focus is, unless the focused widget claims the key via `ShortcutOverride` (text fields and spin boxes do for printable keys). Used for: File, Panels, Help, Layout, plate transforms (as today).
- **Player** (`Qt::WidgetShortcut`, the action added to the viewport and to the timeline's two painted widgets, `TimelineScrubber_Qt` and `TimelineTracks_Qt`, only): fires only when one of those has focus. Not the Plate Manager dock and not the timeline's transport controls: their sliders, combos, spin boxes and buttons use arrows, Home/End and Space themselves and don't claim them through `ShortcutOverride`. Used for transport, In/Out, timeline, track cycling — the keys that are also navigation keys. Today these fire only with viewport focus; adding the two timeline widgets is the only widening. (My call, from the builder check.)
- **Panel keys stay where they are** and are not in the registry: Playlist Enter / Delete / Backspace / Shift+Up/Down, the LUT cube's F, the FX scrub field's keys. They mean something only inside one widget. The Shortcuts page lists them read-only in a "Inside panels" group so they can be found. The LUT cube's F currently loses to the Global F (Fit); fix it here by having `LutCloudWidget` claim F through `ShortcutOverride` while it has focus. (My call.)
- Remote chat mode: while chat mode is on, the viewport accepts `ShortcutOverride` for every key so typed letters go to the chat. Today the Global bare letters (F, V, M, T, H, N) probably fire while typing chat; this is a fix, applied in both presets. (My call.)
- **Modifiers match exactly.** Today's viewport handler ignores Shift on most keys, so Shift+Left, Shift+Space, Shift+8 and so on work by accident. `QAction` matches exact modifiers and those accidental variants are dropped (listed in the release notes, not registered as hidden aliases). A command may hold **up to two keys per preset** (needed for Left/Right + X/C in 1.7 and `,`/`.` + `<`/`>`); the canonical stored keys for play direction are `,` and `.` (unshifted, which match reliably across platforms), with `<`/`>` as the second key. (My call.)
- Conflict rule: two commands may not share a key in the same scope, and a Global key may not equal any Player key.

### Presets

Two built-in presets. The **Editing** preset is the default; **JefeCheck 1.7** reproduces what shipped FLTK 1.7 did (`main`: `UICallbacks.cpp` `globalCB`, `GlViewport.cpp` `FL_KEYDOWN`), the keys returning users' fingers know. (Owner: match shipped 1.7, not today's qt-experimental keys.)

Rules:
- **1.7** = shipped 1.7's keys, plus a 2.0 key wherever that key is free in 1.7. Where a 1.7 key now belongs to a 2.0 command (`V` is Flip in 2.0, Last Frame in 1.7), the 1.7 meaning wins and the 2.0 command keeps its other key or goes to the key 1.7 used for it.
- **Editing** = today's 2.0 keys, changed only where the NLE convention (Premiere, Resolve, Final Cut, Avid) differs, plus the 1.7 commands 2.0 dropped, on keys that don't clash with that convention.

**Commands 2.0 dropped and this project brings back** (backends exist in `gfcPlateManager`; each needs a bridge call):
- R / G / B / A: toggle the red / green / blue / alpha channel on the active plate. (Dropped because bare letters at app scope broke typing; Player scope and `ShortcutOverride` fix that.)
- 1–4: show track A–D on the active plate.
- Shift+1–5: apply colour-correction favourite 1–5; Ctrl+Shift+1–5: save the active plate's colour correction as favourite 1–5.
- Shift+F1–F5: load FX-stack favourite 1–5; Ctrl+F1–F5: append it; Ctrl+Shift+F1–F5: save the active plate's stack as favourite 1–5.
- Previous / next LUT on the active plate (1.7: hold L + Up / Down).

| Command | Editing (default) | JefeCheck 1.7 |
|---|---|---|
| Play / Pause | Space | Space |
| Play backward / Pause / Play forward | J / K / L | — (L is the LUT hold key in 1.7) |
| Step back / forward | Left / Right | Left / Right, X / C |
| Go to start / end | Home / End | Z / V, Home / End |
| Play direction reverse / forward | `,` / `.` (also `<` / `>`) | same |
| Loop once / loop / bounce | 8 / 9 / 0 | 8 / 9 / 0 |
| Set In / Out at playhead | I / O | I / O |
| Go to In / Go to Out | Shift+I / Shift+O | — |
| Reset In / Out to the timeline ends | Alt+I / Alt+O | Shift+I / Shift+O |
| Clear In and Out | Alt+X | — |
| Set In and load from here | Shift+Alt+I | Alt+I |
| Cycle track on active plate | Up / Down | Up / Down |
| Show track A–D on active plate | 1–4 | 1–4 |
| Previous / next LUT | — (unbound; bind in Preferences) | hold L + Up / Down |
| Toggle R / G / B / A channel | R / G / B / A | R / G / B / A |
| CC favourite apply / save | Shift+1–5 / Ctrl+Shift+1–5 | same |
| FX favourite load / append / save | Shift+F1–F5 / Ctrl+F1–F5 / Ctrl+Shift+F1–F5 | same |
| Fit / fit all plates | F / Shift+F | F / Alt+F, Shift+F |
| Flip / flip all | V / Shift+V | Ctrl+8 / Ctrl+Alt+8 |
| Flop / flop all | M / Shift+M | M, Ctrl+9 / Shift+M, Ctrl+Alt+9 |
| Text mode / all | T / Alt+T | T / Alt+T |
| Reset plate / all | Ctrl+R / Ctrl+Alt+R | same |
| Reset colour / all | Shift+R / Shift+Alt+R | same |
| Fullscreen | F11, Cmd+Ctrl+F | Ctrl+F, F11 |
| Hide controls | Ctrl+Alt+F | Ctrl+Alt+F |
| On-screen help | H | H |
| Histogram / all | Ctrl+H / Ctrl+Alt+H | same |
| Load Sequence Manager | Ctrl+L | Ctrl+L |
| Preferences | Ctrl+P, Ctrl+, | Ctrl+P |
| Quit | standard Quit (Cmd+Q / Ctrl+Q) | same |
| Remote chat: start typing / show-hide chat | Return / — | Y / Ctrl+Y (Return also sends) |
| Layouts single / side / top-bottom / 2×2 | Ctrl+1–4 | Ctrl+1–4 |
| Panels F2–F9, Notes N, Manual F1, File menu (Ctrl+O, Ctrl+S, Ctrl+Shift+O) | unchanged | unchanged (1.7's F2–F6 opened windows 2.0 merged or renamed; the 2.0 panel keys are kept) |
| Timeline zoom in / out, fit, fit to In–Out | `=` / `-`, `\`, Shift+`\` | same |
| Go to / follow playhead (timeline) | picked by the timeline project from free keys | same |
| Hold and drag: gamma / exposure / brightness / contrast / saturation | W / E / Q / D / S | W / E / Q / D / S |

Notes:
- Ctrl+1–5 vs Shift+1–5 vs bare 1–4 vs Ctrl+Shift+1–5 are four different layers of the digit keys, as in 1.7.
- "Clear" (Editing) and "Reset to the ends" (1.7) do the same thing in JefeCheck: In goes to the first frame, Out to the last.
- Hold L + Up/Down is a hold-key combination; `HoldKey` gains an optional paired key list so the registry can express it. Only 1.7 binds it.
- "Up/Down = previous/next edit" is the NLE convention; the edit-tracks project decides whether Editing moves track cycling to make room.
- Timeline-project commands are registered by that project; their keys are reserved here so both presets are complete.

### Persistence

- `QSettings`: `Shortcuts/preset` (`editing` | `classic`), `Shortcuts/overrides/<id>` = portable key text (empty string = deliberately unbound), `Shortcuts/holdOverrides/<id>`. Loaded at startup by `loadPreferences()`'s Qt side; written on Preferences → Done, reverted on Cancel (same snapshot model as the rest of Preferences, §27).
- An override equal to the preset's key is dropped, so "Custom" only shows when something really differs.
- A stored override for an id that no longer exists is ignored and kept (so a downgrade/upgrade round trip doesn't lose it).

### Import / export — `.jcshortcuts`

JSON:

```json
{ "format": "jefecheck-shortcuts", "version": 1,
  "basedOn": "editing",
  "keys":  { "playback.play-pause": "Space", "inout.clear": "Alt+X" },
  "hold":  { "color.drag-gamma": "W" } }
```

- Export writes the preset name plus **every** command's current key (not just overrides), so the file means the same thing even if a later version changes a preset.
- Import sets the preset to `basedOn`, then applies every key as an override; unknown ids are reported in one line and skipped; a file that creates conflicts imports anyway and the conflicting rows show the warning.

### Preferences → Shortcuts

Sixth section in `PreferencesWindow_Qt`'s sidebar, after Remote.

```
 Preset [ Editing ▾ ]  (Custom)        [Import…] [Export…] [Reset to preset]
 ┌──────────────────────────────────────────────────────────────────────┐
 │ esc  F1 F2 F3 …                                                       │
 │  `  1  2  3  4  5  6  7  8  9  0  -  =  ⌫        modifier: [none][⇧][⌃][⌥][⌘] │
 │ ⇥  Q  W  E  R  T  Y  U  I  O  P  [  ]  \                               │
 │     A  S  D  F  G  H  J  K  L  ;  '  ⏎       keys coloured by category │
 │  ⇧   Z  X  C  V  B  N  M  ,  .  /                                     │
 │            space                        ← ↑ ↓ →  Home End             │
 └──────────────────────────────────────────────────────────────────────┘
 [Search commands…           ]                 Key: I  ·  I  Set In at playhead
 Playback                                           ⇧I  Go to In
   Play / Pause                 Space                ⌥I  Clear In
   Play forward                 L                    ⇧⌥I Set In and load from here
 In / Out
   Set In at playhead           I
   …
 ⚠ Alt+X is used by "Clear In and Out". [Replace] [Cancel]
```

- **Keyboard map** (`ShortcutKeyboard_Qt`, custom-painted): US ANSI layout plus F-row, arrows, Home/End. Each key is tinted by the category of the command bound to it on the current modifier layer, with a short name inside when it fits. Modifier toggles (none, Shift, Ctrl/⌃, Alt/⌥, Cmd on macOS) pick the layer; holding a physical modifier while the page has focus shows that layer too. Clicking a key shows, on the right, everything that key does with each modifier. Dragging a command from the list onto a key assigns it (with the conflict check). Hold keys are drawn with a small "hold" mark.
- **Command list**: grouped by category, filtered by the search box (matches name and key text). Double-click or Enter on a row opens a `QKeySequenceEdit` in place; Escape cancels; Backspace on an empty capture unbinds. A row that differs from the preset shows the orange "changed" label colour and a reset dot (same convention as the FX panel). Hold keys are a separate "Hold and drag" group in the same list.
- **Conflict**: capture of a taken key shows the inline warning with Replace (moves the key, the other command becomes unbound) or Cancel. No modal dialogs.
- Changing the preset combo with overrides present asks inline: "Switching discards N custom keys. [Switch] [Keep]".
- Non-US layouts: the map labels keys by Qt key names on an ANSI shape; the list is authoritative. (Known limit, noted in the manual.)

### First launch

When `Shortcuts/preset` is unset, the first time the main window shows, an inline banner at the top of the viewport (not a modal) says: "Keyboard shortcuts follow Premiere, Resolve and Final Cut. Used JefeCheck 1.7? [Use 1.7 keys] [Keep] [Customize…]". Any choice writes the preset. Headless and test runs skip it (preset defaults to `editing`, or `classic` for tests that assert old keys).

### J / K / L

- `L`: play forward at the current rate (if playing backward, switch to forward). `J`: same, backward. `K`: pause. Uses the existing `setPlayDirection` + play/pause bridge calls.
- Repeated L/J speed-up and K-hold+J/L frame step are out of scope (see Later).

### Everything that names a key

- **Menus**: automatic (they hold the registry's actions).
- **Tooltips**: every tooltip that names a key is built with `Registry::label(id)` and rebuilt on `changed()`. A grep for hard-coded key names in `src/qt/` tooltips is part of the test.
- **Help overlay (`H`)**: generated from the registry by category in the same layout as today, passed through `jefe::qt::setHelpMessage`. The hand-written string in `gfcplatemanager.cpp` becomes the fallback only.
- **Docs**: `docs/manual.md` and `docs/quick-start.md` list the Editing keys, with a short table of the 1.7 differences and a paragraph on the Shortcuts page.

## Tests

- `--shortcuts-test` (headless, no GL):
  - every command id unique; every preset conflict-free under the scope rule;
  - **2.0 parity** (phase 1): every key qt-experimental handles today → its command, under the phase-1 keyset;
  - **1.7 parity** (phase 2): every key shipped 1.7 handled (`main`'s `globalCB` and `GlViewport::handle`) → its command; under `classic` each resolves to it;
  - Editing differences resolve as in the table above;
  - preset switch updates every `QAction`'s key; overrides persist and reload; override equal to preset is dropped;
  - export → import round trip gives identical keys; Mac-style file imports on other platforms (portable text);
  - conflicts() finds Global↔Player clashes;
  - help text contains each command's current label; after an override the new label appears and the old doesn't.
- Extend the existing "WINDOW menu" distinct-keys check to walk the registry.
- `--shortcuts-focus-test` (needs a window): with focus in the Playlist list, Down moves the selection and does not cycle tracks; with focus in a spin box, `I` types; with chat mode on, letters go to chat; with focus on the viewport, Space plays.
- Appium: open Preferences → Shortcuts, capture a key for "Play forward", see the conflict banner, Replace, Done, press the key in the viewport, playback starts. Export then import a file.

## Phases (PRs)

1. **Registry, no behaviour change.** Registry; every command registered with today's qt-experimental keys as its only keyset; all binding places moved over; help overlay generated; tooltips via `label()`; `--shortcuts-test` 2.0 parity + focus test.
2. **Presets and Preferences list.** Editing and shipped-1.7 presets; the brought-back 1.7 commands (channels, track 1–4, CC and FX favourites, LUT scroll) with their bridge calls; J/K/L, Go to In/Out, Clear In/Out; Shortcuts page with list, capture, conflicts, reset, search, import/export; first-launch banner; default becomes `editing`; docs.
3. **Keyboard map.** `ShortcutKeyboard_Qt` on the page, modifier layers, click-to-inspect, drag-to-assign.

All three land before the 2.0 release (owner: the map is in the first version).

## Decided by the owner

- Approach A (Qt actions in one registry).
- Default preset for new installs is Editing; a JefeCheck 1.7 preset exists and matches **shipped** 1.7, not today's qt-experimental keys.
- Keyboard map, import/export and J/K/L are in the first release.
- Shortcuts come before the timeline project, which comes before edit tracks.

## My calls (check these)

- Editing gives Alt+I / Alt+O = clear In / Out and moves "Set In and load from here" to Shift+Alt+I.
- 1.7 preset gets 2.0 keys wherever free in 1.7 (zoom, `\`, F2–F9, N, M for flop); it does not get J/K/L because L is its LUT hold key.
- Editing also binds the brought-back 1.7 commands (R/G/B/A, 1–4, favourites) on non-clashing keys; previous/next LUT is unbound in Editing.
- Two scopes (Global / Player) rather than per-widget keys.
- First-launch chooser is an inline banner, not a dialog.
- Export writes every key, not only the changes.
- Non-US keyboard layouts: ANSI map, list is authoritative.
- No chords, no mouse remapping.

## Left to the builder

- Command id naming beyond the `category.verb-noun` pattern.
- Exact category colours (from the theme tokens; must read in the dark theme) and the map's key sizes at 13 px.
- Whether tooltips subscribe to `changed()` individually or the panels rebuild them in their existing refresh.
- How the Player-scope action gets added to dock widgets that are floated (must still work when a dock floats).
- Where the import/export file dialogs start (default browse path from General is fine).

## Later

- JKL speed ramp (2×, 4×), K-hold + J/L slow step.
- Previous / next edit (with edit tracks).
- Non-US keyboard map layouts.
