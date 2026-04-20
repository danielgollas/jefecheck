# JefeCheck Website

Jekyll-based GitHub Pages site for JefeCheck.

## Local development

Requires Homebrew Ruby (the Ruby version manager; system Ruby is too old):

```bash
brew install ruby
```

First-time setup (installs gems to `./vendor/bundle`, not system-wide):

```bash
cd site
/opt/homebrew/opt/ruby/bin/bundle config set --local path 'vendor/bundle'
/opt/homebrew/opt/ruby/bin/bundle install
```

Run the local server:

```bash
./serve.sh
# or directly:
/opt/homebrew/opt/ruby/bin/bundle exec jekyll serve --baseurl ''
```

Opens at http://localhost:4000

## Structure

- `_config.yml` — Jekyll configuration
- `_layouts/default.html` — Dark-themed page layout
- `index.html` — Landing page
- `*.md` — Documentation pages (manual, quick-start, building)
- `manual-images/` — Screenshots (copied from `docs/manual-images/`)

## Deployment

Deploys automatically via `.github/workflows/pages.yml` on push to `main`.
Live site: https://danielgollas.github.io/jefecheck
