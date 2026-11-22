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

This is the **JefeCheck 2.0** site (the Qt app). It is **not published yet**.
The live site at https://danielgollas.github.io/jefecheck is the classic 1.7
(FLTK) site, deployed from `main` by `website-v*` tags.

`.github/workflows/pages.yml` on this line runs only by hand (Actions tab →
"Run workflow"), so nothing here goes live until we choose to publish it.
