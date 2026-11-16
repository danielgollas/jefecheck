---
title: Review summaries and review packages
summary: Export every note in a session as a PDF, text or CSV summary, or bundle the whole review into one file that opens on another machine.
release: "2.0"
areas: [review]
pr: 134
commit: f83ba58f300a6c0c5f5bdef187355626d44110a4
date: 2026-10-05
---

## What's new

Two ways to take a review away from the machine it happened on.

- **Review summary.** One document with every round of notes on every media in the session, as PDF, TXT or CSV. The PDF is grouped by media, then round, then frame. Each frame entry has a thumbnail rendered through the plate's colour correction and LUT, with only that round's notes burned in.
- **Review package.** A single `.jcreview` file (an uncompressed tar archive) that holds the session, its notes, any LUTs that don't ship with JefeCheck and, if you choose, the media itself. When you open it, JefeCheck finds the media, merges the notes and opens the session.

Exporting a summary puts back what it touches along the way: the playlist selection, in/out points, current frame, play state and the Recent Sessions list.

## How to use it

1. **File → Export Review Summary…** and pick PDF, Text or CSV.
2. **File → Export Review Package…**, choose a file and tick **Include media** if the footage should travel with it. The dialog shows how much the media adds, writes the file in steps and can be cancelled.
3. On the other machine, **File → Open Review Package…** and pick the `.jcreview`.

There are command-line versions too: `--export-summary <out>`, `--export-package <out> [--no-media]` and `--open-package <file>`.

## Under the hood

Each media is looked up in a fixed order: the copy inside the package, then the original path, then a fingerprint search under Preferences → Search Paths, and finally a "Locate…" prompt that asks you to confirm if the file you pick doesn't match the fingerprint.

{{quote src/qt/ReviewPackage_qt.cpp#L488-L527 "The relink order: packaged copy, original path, fingerprint search, then Locate"}}

Notes are union-merged. Rounds and notes from the package that you don't have yet are added, and nothing local is deleted.

{{quote src/gfcNoteMerge.cpp#L15-L48 "Merging adds missing rounds and notes and never removes local ones"}}

A package can't write outside its cache folder. Absolute entry names, `..` segments, backslashes and anything with a `:` (drive letters, alternate streams) are refused before any bytes are written.

{{quote src/gfcTarArchive.cpp#L75-L92 "Entry names that could escape the extraction folder are refused"}}

The package cache is never cleared yet, and opening a package has no progress bar or cancel.
