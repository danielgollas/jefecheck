# JefeCheck Website

Static plain-HTML site for JefeCheck. No build step, no Jekyll.

## Structure

- `index.html` — landing page
- `docs.html` — handbook (Quick Start / Manual / Color & FX / Build)
- `assets/css/main.css`, `assets/js/main.js` — shared styles + interactions
- `assets/images/`, `assets/DemoVideo.mp4` — media

## Local preview

```bash
cd site
python3 -m http.server 4000
# open http://localhost:4000
```

## Deployment

Publishing is **tag-driven**, not on every push to `main`. Deploy a new version of
the site by pushing a `website-v*` tag:

```bash
git tag website-v1.0.0
git push origin website-v1.0.0
```

`.github/workflows/pages.yml` (triggered by `website-v*` tags, or manually via the
Actions tab → "Run workflow") uploads `site/` as a static Pages artifact and deploys
it. Live: https://danielgollas.github.io/jefecheck
