---
title: Show render in folder
summary: Click the message at the end of a render to open the folder the output was written to.
release: "2.0"
areas: [render]
pr: 119
commit: b0f192875acbb88890b6cc10cd0606d3cd4e9f44
date: 2026-06-27
---

## What's new

When a render succeeds, the status message under the progress bar becomes a link:

- After rendering stills: **Rendered N frame(s) — Show in folder ↗**
- After rendering a video: **Wrote out.mp4 — Show in folder ↗**

Click it and the output folder opens in your file browser (Finder, Explorer, or your desktop's file manager). For stills that is the output folder you chose. For a video it is the folder that holds the movie file. You no longer have to go and find your renders yourself.

The link only appears after a successful render. When a render is cancelled or fails, the message stays plain text, and the link is cleared when the next render starts.

## How to use it

1. Render as usual from **File → Render…** (`F6`).
2. When the bar turns green, click the blue **Show in folder ↗** message below it. You can also reach it with the keyboard.

## Under the hood

A small helper wraps the success message in a link. The link's target is only a placeholder; the dialog handles the click itself:

{{quote src/qt/RenderDialog_qt.cpp#L72-L78 "The success message is wrapped as a Show in folder link"}}

The status label accepts link clicks from the mouse and the keyboard. When the link is activated, the label opens the last output folder with the system's default handler:

{{quote src/qt/RenderDialog_qt.cpp#L281-L293 "Clicking the link opens the last output folder"}}
