# AGENTS.md

This subtree contains project documentation for the stealth fork: implementation plans, verification notes, external research, standards references, and imported sample material used to guide or validate code changes.

- Put active design notes, hardening tasks, migration plans, and dated implementation snapshots in `docs/Plans`.
- Put verification reports, research writeups, and external analytical material in `docs/Researches`.
- Treat `docs/Samples` as reference material first. It contains imported code, fingerprint tooling, traffic dumps, and other external artifacts; do not rewrite, normalize, or “clean up” those materials unless the task is explicitly about updating the reference corpus.
- Treat `docs/Standards` as standards/reference text. Do not edit copied RFC material unless the task explicitly requires refreshing or replacing the reference.
- Keep documentation claims evidence-backed. When describing stealth behavior, route policy, TLS fingerprints, or runtime guarantees, verify them against checked-in code and tests rather than copying assumptions from older notes.
- Preserve the language and naming style of the document you are editing. This subtree already mixes English and Russian documents, and some files are intentionally date-stamped snapshots.

## Progressive disclosure

- Current stealth implementation status and threat model: `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`
- Verification notes and research reports: `docs/Researches`
- External samples, captures, and imported reference code: `docs/Samples`
- Standards and RFC references: `docs/Standards`
- Root repository guidance: `AGENTS.md`
- Stealth subsystem-specific guidance: `td/mtproto/stealth/AGENTS.md`
- Code and regression sources for technical claims: `td/mtproto/stealth` and `test/stealth`