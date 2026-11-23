---
title: Preferences that persist and take effect
summary: Preferences now save between launches, Cancel undoes your edits, and the settings in the window actually change the app.
release: "2.0"
areas: [preferences]
pr: 131
commit: 6b2aeeba943e7b6f99e5f5bdf3fd61052e32d161
date: 2026-07-19
---

## What's new

Before this change, pressing **Done** in Preferences saved nothing, so most settings went back to their defaults on the next launch, and several controls didn't do anything at all. Each preference was checked, then either wired up or removed, and the window was rebuilt.

- **Saved for real.** Settings load at startup and save when you press Done. **Cancel** puts back everything you changed while the window was open.
- **General:** an RGB background colour and a new **checkerboard background** whose two shades come from that colour. Also start fullscreen, open the Load window at startup, a default browse folder, thumbnails, and the size and fade of on-screen feedback messages.
- **Text:** size, colour, hinting, filtering, gamma and shadow for on-plate labels, with a live preview.
- **Formats:** EXR display-window handling (shows overscan or crop at its real position) and EXR pixel aspect ratio, both applied when OpenImageIO loads the file.
- **Search Paths** and **Remote** (nickname, chat and remote-pointer look) have their own sections.
- Settings that had no effect were removed or hidden.

## How to use it

Open **File → Preferences…** (`Ctrl+P`) and pick a section in the sidebar: General, Text, Formats, Search Paths or Remote. Edits show up straight away. Press **Done** to keep them or **Cancel** to undo them.

## Under the hood

The window keeps a copy of the settings from when it opened. Done writes the current settings to disk. Cancel copies the saved snapshot back and re-reads the text settings.

{{quote src/qt/PreferencesWindow_qt.cpp#L77-L90 "Done persists; Cancel restores the snapshot and the text settings"}}

All settings go through Qt `QSettings` in one place. A stored grey background value carries over into the new RGB keys.

{{quote src/qt/qt_prefs_persist.cpp#L36-L48 "General settings loaded at startup, migrating the old grey background"}}

When an EXR's display window differs from its data window, the image is placed inside the full display window, unless you've turned that off.

{{quote src/gfcimageloaderoiio.cpp#L183-L198 "EXR display window vs data window, controlled by the Formats preference"}}
