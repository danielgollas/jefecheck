# Muesli notes eval rubric

Every enhancement-prompt or model change must run the eval suite and meet the
gates below before shipping (roadmap standing rule). The grader model scores
each generated notes document against the case's transcript, raw notes, and a
human-written reference, on five dimensions, 1–5 each.

## Dimensions

### 1. Grounding (accuracy) — weight ×2
Are all claims supported by the transcript or the raw notes?
- 5: every claim supported; discrepancies and uncertainties correctly flagged
- 3: minor unsupported paraphrase, no invented facts
- 1: any invented name, number, date, attendee, or commitment

### 2. Completeness
Are the meeting's substantive points present?
- 5: everything in the reference notes is covered
- 3: main thread covered, some secondary points missing
- 1: major decisions or topics missing

### 3. Action items
- 5: all reference action items present with correct owners; none invented
- 3: all present, an owner missing or vague
- 1: action items missing or owners guessed

### 4. User-note integration
- 5: user's emphasis reflected; conflicts resolved in the user's favor and flagged
- 3: user notes incorporated but emphasis lost
- 1: user notes ignored or overridden silently

### 5. Format compliance
- 5: template followed; valid span annotations on every factual paragraph; tasks as `- [ ]`
- 3: template followed; spans present but incomplete
- 1: wrong sections, or no span annotations

## Gates (ship / no-ship)

- **Grounding mean ≥ 4.5 and no case below 4.** A single hallucination is a
  release blocker — accuracy is priority #1.
- Weighted mean across all dimensions ≥ 4.0.
- No dimension mean below 3.5.

A model *downgrade* (e.g. moving a tier from Sonnet to Haiku) additionally
requires the downgraded configuration to score within 0.2 weighted-mean of the
current configuration on the same cases.
