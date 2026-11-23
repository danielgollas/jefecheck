---
title: Default bit depth in Preferences, and a status bar you can hide
summary: Set the bit depth for new loads in Preferences, and hide or show the status bar from the View menu.
release: "2.0"
areas: [preferences, viewer]
pr: 125
commit: 7570176e8c10a0f0ffd1c51e09d4026c67c770b6
date: 2026-06-28
---

## What's new

The default bit depth for newly loaded media (8, 16, 16-half or 32-float) has moved out of the status bar and into Preferences, next to the decode filter. It is a setting you pick once, so it now lives with the other engine defaults rather than taking up space in the status bar.

The status bar itself can now be hidden. **View → Show Status Bar** is a checkable item, and JefeCheck remembers your choice the next time it starts.

## How to use it

1. Open **File → Preferences…** and go to the **Engine** page.
2. Choose a value under **Default bit depth**. It applies to sequences you load from then on with Quick Load or drag and drop. Plates you have already loaded keep their depth until you reload them. The Load Sequence Manager still has its own per-track depth controls.
3. To hide the status bar, uncheck **View → Show Status Bar**. Check it again to bring it back.

Both settings are saved straight away and restored at launch.

## Under the hood

The Preferences combo lists the four depths and writes the choice to the shared settings, and to the `Engine/defaultTextureFormat` key, as soon as it changes. A saved value it doesn't recognise falls back to 16-half:

{{quote src/qt/PreferencesWindow_qt.cpp#L285-L302 "The bit-depth combo on the Engine page"}}

At startup the main window reads the saved depth and decode filter and applies them without opening Preferences, so a saved value is in effect from the first load:

{{quote src/qt/MainWindow_qt.cpp#L78-L85 "Saved engine defaults are applied at launch"}}

The status-bar toggle reads `UI/statusBarVisible` (shown by default), applies it, and saves every change:

{{quote src/qt/MainWindow_qt.cpp#L540-L556 "View → Show Status Bar, persisted between sessions"}}
