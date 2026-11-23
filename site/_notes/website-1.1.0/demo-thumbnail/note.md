---
title: Demo video that grows from a thumbnail
summary: The landing page shows a small play-now thumbnail, and clicking it grows the demo video player out of it.
release: "website-1.1.0"
areas: [website]
pr: 130
commit: 7c60a903818608a536b7c9c8da078e11a2761585
date: 2026-07-04
---

## What's new

The demo section on the project's landing page has changed. Before, it showed a large 16:9 poster with a play button drawn on top. Now it shows a compact thumbnail, a screenshot of the review interface with its own play button. When you click it, the thumbnail disappears and the full video player grows out of the same spot with a short scale-in animation. The demo then starts playing with the normal video controls.

The video is no longer fetched when the page loads. It downloads only when you click, so visitors who never watch the demo don't download the large file. The player is also sized to the video's own shape and letterboxes it instead of cropping it.

## How to use it

Open the landing page and click the thumbnail under the hero. Use the video controls that appear to pause, seek or go fullscreen.

## Under the hood

The section holds two elements: a button with the thumbnail and a hidden player. The video uses `preload="none"`, so the browser doesn't fetch it until it is played:

{{quote site/index.html#L80-L91 "The thumbnail button, and the hidden player that doesn't preload the video"}}

A click hides the thumbnail, opens the player and starts playback:

{{quote site/assets/js/main.js#L40-L51 "Clicking the thumbnail swaps it for the player and plays the video"}}

The grow effect is a CSS keyframe animation that scales the player up from a little over a quarter of its size:

{{quote site/assets/css/main.css#L47-L53 "The scale-in animation and the letterboxed video"}}
