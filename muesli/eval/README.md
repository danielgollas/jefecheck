# Muesli notes eval

The accuracy harness that gates every model or prompt change (roadmap standing
rule). Scoring dimensions and ship gates are in [`rubric.md`](rubric.md); the
runner is [`run-eval.ts`](run-eval.ts).

## Running

```sh
cd muesli/eval
ANTHROPIC_API_KEY=... node --experimental-strip-types run-eval.ts            # default product model
ANTHROPIC_API_KEY=... node --experimental-strip-types run-eval.ts --model claude-sonnet-5
```

Exit code 0 = all gates passed; 1 = gate failure; 2 = setup problem.

## Adding cases

Each case is a directory under `cases/`:

```
cases/
└── standup-short/
    ├── transcript.jsonl      # required — storage-format transcript
    ├── raw-notes.md          # required — participant's live notes
    ├── reference-notes.md    # required — human-written "ideal" notes for grading
    ├── glossary.md           # optional
    └── template.txt          # optional — template id (default: "default")
```

Target set (M0 exit criteria): ~20 cases spanning short/long meetings, 2–6
speakers, noisy transcripts (low-conf words), heavy/light raw notes, each
built-in template, and at least one non-English meeting. Real recorded meetings
(consented) beat synthetic ones — synthetic cases are acceptable to bootstrap.

**Privacy**: cases may contain real meeting content. Do not add cases from
meetings without every participant's consent, and prefer redacted or synthetic
data in the public repo.

## Cost note

A full run makes 2 API calls per case (generation + grading). Grading uses a
strong model on purpose — the harness is where we spend money so the product
doesn't have to. For bulk sweeps (e.g. comparing 3 candidate models across all
cases), port the runner to the Batches API for 50% off.
