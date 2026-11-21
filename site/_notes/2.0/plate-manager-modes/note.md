---
title: The Plate Manager shows the plates you're looking at
summary: The Plate Manager shows only the plates in the current layout, marks the active plate in orange, and rearranges its cards to fit wherever you dock it.
release: "2.0"
areas: [panels, viewer]
commit: 3b22b035037b545090c02712e7e132f3a4cae1b0
date: 2026-11-13
hero: after-window-quad.png
---

## What's new

The Plate Manager used to show all four plate cards all the time, even with one plate on screen. It now has three modes:

- **Active:** one card, for the plate you're working on.
- **Visible** (the default): one card per plate on screen. That's 1 in the single layout, 2 side by side or stacked, and 4 in the 2×2.
- **All:** all four, as before. Cards for plates that aren't on screen get a dashed border.

### One plate on screen, one card in the dock

In the single layout, the old dock still showed four cards, three of them for plates you couldn't see. Visible mode shows only plate A, and each card is down to two rows. The header row is gone: a small map of the plates sits in the dock's title bar.

![The old Plate Manager: four plate cards in a 2 by 2 grid, even though only plate A is on screen](before-bottom.png)

![The new Plate Manager in the single layout: one two-row card for plate A, and the plate map in the title bar](after-bottom-single.png)

### Four plates fit beside the Timeline

In the 2×2 layout on a 1280-wide window, the four cards sit in two rows of two next to the Timeline, in a dock 178 px tall instead of 202. Zoom, X, Y and rotation move into a **Transform** popup (the `1.00×` button), and contrast, brightness and saturation into a **Color** popup. Rows always come out even, so a wide window shows 2×2 or one row of four, never 3 + 1.

![The new Plate Manager in the 2×2 layout: four two-row cards with Color buttons, plate A framed in orange](after-bottom-quad.png)

### Which plate is active? Look for orange

When more than one plate is on screen, the active plate gets a thin orange frame in the viewport, the same colour as its card and its square in the title-bar map. Click a square in the map to make that plate active.

![The whole window in the 2×2 layout: the active plate's viewport frame, its card and its map square are all orange](after-window-quad.png)

All mode keeps every card and marks the ones that aren't on screen with a dashed grey border:

![All mode in the single layout: plate A in orange, plates B, C and D dashed](after-bottom-all.png)

### The layout follows the dock

- **Top or bottom:** wide, two-row cards in even rows beside the Timeline, at most two rows tall. Drag the divider narrower and the cards drop to one scrolling column instead of being cut off.
- **Left or right:** tall cards in one column that scrolls to keep the active plate in view.
- **Floating:** the cards sit where their plates sit on screen, and the window resizes to fit them.

![The floating Plate Manager in the 2×2 layout: four cards arranged like the viewport](after-floating.png)

### Also in this change

- The **Panels** menu is now **Window**, and every panel has a key: Plate Manager F2, FX F3, LUTs F4, Remote F5, Render F6, Notes F7, Timeline F8, Playlist F9. On the Mac it also has Minimize (⌘M), Zoom and Bring All to Front.
- Zoom goes from 0.01× to 100×, and pan from −99,999 to 99,999 px. Sessions saved with larger values load clamped.
- The mode is remembered between launches.
- In a remote session, the dock updates its cards and map as soon as the host changes the layout.
- Fixed: a crash when a card switched form and back, and Ctrl+P being bound twice (it's now only Preferences).

## How to use it

1. Press **F2** to show the Plate Manager.
2. Choose **View → Plate Manager Shows → Active, Visible or All**, or right-click in the panel.
3. Click a square in the title-bar map to make that plate active.
4. Drag the dock to the side, the bottom or out as a floating window; the cards rearrange to fit.

## Under the hood

Which cards appear is a small, GL-free function of the mode and the viewport layout:

{{quote src/gfcPlateLayout.cpp#L18-L25 "Which plates each mode shows"}}

Cards in a row that has to wrap are spread evenly, so four cards that fit three per row go 2 + 2:

{{quote src/gfcPlateLayout.cpp#L34-L61 "Arranging the cards for each dock shape"}}

The orange frame is drawn after the plates, on screen only, so renders never include it:

{{quote src/gfcplatemanager.cpp#L1137-L1143 "The active-plate frame"}}
