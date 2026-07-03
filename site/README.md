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

Auto-deploys via `.github/workflows/pages.yml` on push to `main` (uploads `site/`
as a static Pages artifact). Live: https://danielgollas.github.io/jefecheck
