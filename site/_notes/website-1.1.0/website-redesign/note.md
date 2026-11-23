---
title: New project website and handbook
summary: The project site is a fast static landing page with one-line installs, screenshots and a one-page handbook.
release: "website-1.1.0"
areas: [website]
pr: 127
commit: c4f85d0eb88a5d6e7b6c6428cb2baee7767d47da
date: 2026-07-04
---

## What's new

The JefeCheck website has a new design: a dark landing page in the style of a digital-cinema tool, plus a new handbook page.

The landing page has:

- a sticky navigation bar that turns into a menu button on narrow screens
- a hero section and the demo video, which plays when you click it
- a strip of key specifications and three feature cards: **Play. Process. Share.**
- a gallery of six screenshots
- one-line install commands for macOS (Homebrew), Windows (winget) and Linux (a `curl` download of the latest release), each with a **Copy** button
- a troubleshooting section that opens and closes
- cards linking into the docs

The new **JefeCheck Handbook** (`docs.html`) is a single page with Quick Start, User Manual, Color & FX and Build from Source sections, and it shares the landing page's navigation and footer.

The site is now plain HTML, CSS and a little JavaScript, with no site generator and no build step. The README now links to it.

## How to use it

Visit the project site. To preview it locally, serve the `site/` folder with any static web server, for example `python3 -m http.server` run inside `site/`.

## Under the hood

The interactions are a few lines of vanilla JavaScript. For example, the install cards' Copy button copies the command and briefly shows "Copied!":

{{quote site/assets/js/main.js#L16-L27 "Copy buttons put the install command on the clipboard"}}

Because the site is static, publishing is just uploading the `site/` folder to GitHub Pages. The deploy runs when a `website-v*` tag is pushed, or by hand:

{{quote .github/workflows/pages.yml#L1-L31 "GitHub Pages deploys the site folder as-is, with no build step"}}
