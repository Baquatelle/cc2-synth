# AI_USAGE.md

Per the assignment's AI Usage rules, this documents how AI tools were used
on this project, using the [AI Assessment Scale](https://aiassessmentscale.com/)
(AIAS) as the reporting framework. Levels run 1 (no AI) to 5 (full AI, no
human input beyond the prompt). Each entry below is *level-tagged per task*,
not for the project as a whole, because different parts of the work used AI
differently.

## Summary table

| Task | AIAS level | Tool | What the AI did | What the human did |
|---|---|---|---|---|
| Repository audit (this doc set) | AIAS 3 — AI-assisted editing/review | Perplexity | Read the existing repo (file tree, commit history, README) and pointed out that `PLAN.md` and `KNOWN_ISSUES.md` were referenced but missing, and that no commits yet existed from the second team member | Reviewed the findings, decided what to actually fix, wrote/approved the final wording, decided the doc-checker was worth adding, requested and approved the PR |
| `scripts/check_docs.py` | AIAS 3 — AI-assisted co-drafting, human-verified | Perplexity + local Python execution | Drafted the script and **ran it against the real README content in a sandbox** to confirm it correctly flags the missing `KNOWN_ISSUES.md` link and passes once the file exists | Specified the requirement ("catch dead doc links"), reviewed the logic, chose to include it in the repo |
| `PLAN.md` / `KNOWN_ISSUES.md` content | AIAS 2–3 — AI-assisted drafting from real project data | Perplexity | Drafted structure and prose from the actual commit history and README, rather than inventing content | Corrected/filled in team-specific facts (roles, actual test results), is responsible for final accuracy |
| Core synth engine / DSP code (FM, percussion, sampler, filter, envelope) | Team member to fill in | — | — | — |
| UI layer (sequencer, XY pad, oscilloscope, particle field) | Team member to fill in | — | — | — |

**Rule followed:** AI was not asked to "solve the assignment." It was asked
to review an existing, mostly human-written project and fill two concrete,
already-identified documentation gaps, and to produce a small, independently
testable utility script — not new audio/DSP logic, which needs the original
authors' judgment about the DSP design.

## Sample prompts used

> "Read this GitHub repo completely and explain what it does — check the
> actual files, not just the README."

> "What is broken or missing in this repo? What's left to build before
> submission?"

> "Write a small Python script that checks every Markdown file in the repo
> for local links that point to files that don't exist, and test it against
> the real README content before we ship it."

*(Add your own prompts here — for the engine/DSP/UI code, if either of you
used an AI tool for boilerplate, debugging, or refactoring suggestions,
list the actual prompt and what you kept vs. rewrote. If you wrote the DSP
code yourselves without AI assistance, say so explicitly — that's AIAS
level 1 for that task, and just as valid to report.)*

## What we deliberately did not do

- Did not ask AI to design the `Voice` class hierarchy, the FM/percussion/
  sampler DSP, or the OOP-relationship choices — those are the graded
  learning outcomes of the assignment and are the team's own design work.
- Did not use AI to generate decorative visuals; the three visualizations
  (oscilloscope, particle field, status panel) all read live engine state
  and serve a function, per the assignment's "no merely decorative
  illustrations" rule.
