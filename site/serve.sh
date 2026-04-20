#!/bin/bash
# Local Jekyll development server for the JefeCheck site.
# Uses Homebrew Ruby + Bundler with a local vendor/bundle path.
set -e
cd "$(dirname "$0")"

RUBY=/opt/homebrew/opt/ruby/bin
export PATH="$RUBY:$PATH"

if [ ! -d "vendor/bundle" ]; then
    echo "First-time setup: installing gems to vendor/bundle..."
    bundle config set --local path 'vendor/bundle'
    bundle install
fi

# Override baseurl for local dev (production uses /jefecheck)
bundle exec jekyll serve --baseurl '' --livereload "$@"
