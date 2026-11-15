# Feature notes on the JefeCheck website

Status: design, 2026-11-14. Branch `feature/feature-notes` from `main`.

The tool itself is generic and lives in dev-workspaces as the `dw:feature-notes` skill. Its design is in dev-workspaces at `docs/specs/2026-11-14-feature-notes-design.md`. This doc covers only how JefeCheck adopts it.

## Why

The per-PR "release notes" pages (FX Panel, Plate Manager, Panel Polish, PR #137, so far private claude.ai artifacts) should be public on the JefeCheck site, for promotion, education and a reference of delivered work. Each claim links to the code on GitHub and to the test that checked it.

## Current state

- `site/` on `main` is plain static HTML (`index.html`, `docs.html`, `assets/css/main.css`, `assets/js/main.js`), with no build step.
- `.github/workflows/pages.yml` deploys `./site` on `website-v*` tags or manually.
- `build.yml` runs no tests. The headless tests (`--fx-panel-test`, `--ui-density-test`, `--fx-test`, `--render-test`, …) are run by hand, mostly on macOS.
- The Qt work, which is all of the planned first content, is on `qt-experimental`, not `main`.

## Adoption

- Run `dw:feature-notes` set-up. That creates `feature-notes.yml`, `site/_notes/areas.yml`, and vendors `tools/feature-notes/`.
- Config:
  - `repo: danielgollas/jefecheck`, `notes_dir: site/_notes`, `out_dir: site/features`, `base_url: /jefecheck/features/`
  - the theme links `site/assets/css/main.css`, with the accent `#e8a020` on black
  - `tests:` names the headless test commands
- Add a "What's new" link to the nav in `index.html` and `docs.html`.
- `pages.yml` gains full history, the PR-ref fetch, and the build step before upload.
- Port the four existing pages into notes as the first content.
- Starting areas: Viewer, Plates, FX, Colour, Timeline, Load, Playlist, Review, Remote, Render.
- Phase 2: a CI job runs the offscreen-safe headless tests on Linux, so notes can link real CI runs.

## Still open (owner)

Whether Qt / 2.0 notes may be published before 2.0 is on `main`:
- **Preview now:** `preview_refs: [qt-experimental]`, with a "Preview: in 2.0" badge.
- **Wait:** publish them when 2.0 lands on `main`.
