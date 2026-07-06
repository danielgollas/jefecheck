# JefeCheck Design Tokens

Portable design tokens for the JefeCheck UI, extracted so they can be lifted into
the shared **Claude Design** system. Qt QSS has no variable support, so this file
is the single source of truth; `jefecheck_dark.qss` applies these values by hand.
When Claude Design consumes these, map the semantic roles (not the raw hexes).

The aesthetic: **discreet** — small, transparent controls with a thin border and a
*subtle hint of color* on hover/active, rather than filled blocks. VFX-dark,
Nuke/Resolve-adjacent.

## Core palette (raw)

| Token | Value | Notes |
|-------|-------|-------|
| `--surface-0` | `#1a1a1a` | window / dialog / input wells |
| `--surface-1` | `#202020` | panel background |
| `--surface-2` | `#242424` | raised control (combo, checkbox) |
| `--border` | `#3d3d3d` | default 1px border |
| `--border-strong` | `#4a4a4a` | hover border (neutral) |
| `--text` | `#dcdcdc` | primary text |
| `--text-muted` | `#9a9a9a` | labels, secondary |
| `--text-dim` | `#6a6a6a` | disabled |

## Accent (warm / orange)

| Token | Value | Role |
|-------|-------|------|
| `--accent` | `#d4771e` | active / focus ring |
| `--accent-dim` | `#b86819` | pressed border |
| `--accent-hint` | `#8a5a2a` | subtle border tint on hover (the "hint of color") |
| `--accent-tint-bg` | `#2e2620` | subtle pressed/checked fill (not a bold block) |
| `--accent-on-tint` | `#e8b884` | text on the tinted fill |
| `--accent-select-bg` | `#7a4a1e` | text selection background (muted, not bright) |

## Semantic roles (from the JEF-4 remote panel)

| Token | Value | Role |
|-------|-------|------|
| `--primary-border` | `#4c6577` | primary action (slate) border |
| `--primary-text` | `#a6c0d2` | primary action text |
| `--primary-tint-bg` | `#263038` | primary hover fill |
| `--danger-text` | `#c98b82` | destructive action (End/Leave) text |
| `--danger-border` | `#4a3a3a` | destructive border (→ `#6a4444` on hover) |
| `--success` | `#5bb07a` | connected / OK status dot |
| `--warning` | `#d4a01e` | connecting / warn |
| `--error` | `#e0836c` | error text |

## Shape & rhythm

| Token | Value |
|-------|-------|
| `--radius-sm` | `4px` (inputs, buttons) |
| `--radius-md` | `6px` (segmented ends) |
| `--radius-lg` | `8px` (cards, lists, sections) |
| `--pad-btn` | `3px 11px` |
| `--pad-input` | `3px 7px` |
| `--font-size-control` | `11px` (buttons/labels) · `12px` base |

## Component recipes (semantic)

- **Button (default, "discreet")** — transparent bg · `--border` · `--radius-sm` ·
  small · text `--text`. Hover: bg `#2d2d2d`, border `--accent-hint`. Pressed/checked:
  bg `--accent-tint-bg`, border `--accent`, text `--accent-on-tint`.
- **Button (primary)** — transparent bg · border `--primary-border` · text
  `--primary-text` · semibold. Hover: bg `--primary-tint-bg`.
- **Button (danger)** — transparent · border `--danger-border` · text `--danger-text`
  · small. Hover: subtle red-tinted fill.
- **Input / combo / spinbox** — well `--surface-0/2` · `--border` · `--radius-sm` ·
  focus border `--accent`. Spinbox steppers: flat, muted triangles.
- **Checkbox** — 15px · `--radius-sm` · `--surface-2` · border `--border-strong`.
  Checked: muted-accent fill (`#c07429`). Hover: `--accent-hint`. (TODO: swap the
  fill for a checkmark SVG for extra polish.)
- **Segmented control** — two joined transparent buttons; checked = `--primary-tint-bg`
  + `--primary-border`.
- **Collapsible section** — flat disclosure header (triangle + title), hover bg, thin
  divider; content area. See `CollapsibleSection_qt`.

## Open items (JEF-13)

- Checkmark / radio-dot as SVG resources (AUTORCC is on) instead of a fill.
- Replace inline `setStyleSheet` in `FXParamPanel_qt` (and audit other call sites)
  with these token styles.
- Decide the final accent story with Claude Design: keep warm orange, or move to the
  slate "primary" as the app accent.
