## Background

Four years at a payments startup owning the ledger service: double-entry, append-only, ~12,000 transactions/second at peak; led its zero-downtime migration from Postgres to a sharded setup. <!--m:{"spans":[[0,1]]}-->

## Signals

- Sound migration design: dual writes with a nightly reconciliation diff that had to be empty for two weeks before flipping reads; the diff caught a timezone bug that would have corrupted month-end. <!--m:{"spans":[[2,3]]}-->
- Clear-headed rollback reasoning: reads never left the old store until the diff was clean, so rollback was stopping dual writes. <!--m:{"spans":[[4,5]]}-->
- Self-aware: volunteered that never rehearsing the full rollback was a mistake. <!--m:{"spans":[[5,5]]}-->

## Concerns

- No formal mentorship experience; leveling-up of juniors happens informally through teaching-style code reviews. <!--m:{"spans":[[6,7]]}-->

## Follow-ups

- Probe leadership scope in a later round: has the candidate led people, not just systems? <!--m:{"spans":[[6,7]]}-->
