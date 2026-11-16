---
title: A working FX stack with one effects panel
summary: Add, reorder, toggle and tune GLSL effects on the active plate from a single FX panel, and see the result in the viewport.
release: "2.0"
areas: [fx]
pr: 124
commit: 79832293dcf2881c9cec7f2a449ed3d109ef1af5
date: 2026-06-28
---

## What's new

The shader effects pipeline now works in the Qt interface. Before this change, turning on any effect could leave the viewport black, including the other plates. Effects now draw on screen as expected.

The effects UI is now one **FX** panel for the active plate. It replaces the separate FX Stack browser that was on F2. In the panel:

- **+ Add FX** opens a menu grouped by category, built from each effect's menu name.
- Each effect in the stack is a card with an on/off checkbox, a remove button and inline editors for its parameters: numbers, toggles and choice lists.
- Texture inputs have a source picker: **Previous** (the output of the effect before it) or **Track A–D**.
- 3D cube and 1D LUT parameters have a picker listing the loaded LUTs.
- Drag a card to move the effect up or down the stack.

Every effect loads at startup, so there is no separate "available effects" list. When the dock is narrow, the inputs shrink and long names are clipped, but the on/off and remove buttons stay visible.

This PR also includes some smaller fixes. The step-back transport button now shows `|◀`, so it no longer looks like reverse playback. The on-screen feedback message (zoom, reset, flip) is sized correctly on Retina displays, and its text now fades out with the box. Flip and flop animations play through without needing a forced repaint.

## How to use it

1. Press **F3** to show the **FX** dock.
2. Click **+ Add FX** and pick an effect from its category submenu. It is added to the active plate.
3. Change its parameters on the card, or untick the checkbox to bypass it.
4. Drag cards to reorder the stack, or click the remove button to delete an effect.

## Under the hood

Qt's OpenGL widget draws into its own framebuffer, not framebuffer 0. The effects code came from the old FLTK interface and rebound 0 to "return to the screen", so everything drawn after the first effect went to a buffer that was never shown. The viewport now passes Qt's real framebuffer to that code on every paint:

{{quote src/qt/GlViewport_qt.cpp#L110-L119 "Publishing Qt's default framebuffer each frame"}}

The Add FX menu splits each effect's `Category/Subcategory/Name` path into nested submenus:

{{quote src/qt/FXParamPanel_qt.cpp#L181-L212 "Building the categorized + Add FX menu"}}

When you drop a card in a new place, the stack moves that effect from its old position to the new one:

{{quote src/gfcfxstack.cpp#L97-L105 "Reordering the FX stack"}}
