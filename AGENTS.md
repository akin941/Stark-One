# AGENTS.md — working agreement

Read this before writing code. It applies to every coding agent (OpenCode is the
primary implementer) and to humans.

## Read order

1. [PROJECT.md](PROJECT.md) — what and why, definition of done
2. [ARCHITECTURE.md](ARCHITECTURE.md) — normative structure, APIs, layering
3. [TASKS.md](TASKS.md) — pick the **first unfinished task**, only that one
4. [HARDWARE.md](HARDWARE.md) / [WOKWI.md](WOKWI.md) — when the task touches hardware or the simulator
5. [DECISIONS.md](DECISIONS.md) — before proposing any change to a decided approach

## Non-negotiables

* **One task at a time.** Finish it, satisfy every acceptance criterion, stop.
* **No simulator conditionals.** `#ifdef WOKWI` and friends do not exist in this
  codebase (ADR-0010). `grep -ri wokwi components/ apps/ main/` must stay empty.
* **GPIO numbers live only in `components/stark_board`.**
* **Layering is enforced** (ARCHITECTURE §2, `scripts/check_layers.py`). Pure cores
  must not include ESP-IDF headers — the host test build is what catches it.
* **Warnings are errors.** No `-Wno-*` on our components; no undocumented pragmas.
* **No TODOs.** Planned work belongs in TASKS.md. `NOTE:` comments explaining
  non-obvious decisions are encouraged.
* **No secrets.** Ever. Not in code, not in tests, not in diagrams, not in commits.
* **Doc updates ship with the change**, in the same commit.
* **Feature scope is bounded** by [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md).

## When you are unsure

* If the task needs a decision this plan does not make → write an ADR in
  DECISIONS.md, flag it in the PR, and do not decide silently.
* If the task appears to need work a later task owns → stop and say so. Scope growth is
  a planning bug, not something to solve in code.
* If a documented fact looks wrong (a pin, a version, a Wokwi part name) → verify it,
  fix the document, and note the correction in the PR.

## Per-task checklist

```
[ ] Only this task's files changed
[ ] idf.py build clean, zero warnings
[ ] scripts/fmt.sh --check clean
[ ] scripts/test_host.sh green (if the task defines host tests)
[ ] scripts/test_wokwi.sh green (if the task defines scenarios)
[ ] Every acceptance criterion in TASKS.md demonstrably met
[ ] Docs updated in the same commit
[ ] PR names the task ID and lists the ACs as a checklist
```

## Commit and PR style

* Commit subject: `STARK-00NN: <imperative summary>` (≤ 72 chars).
* Body: what changed, why, and how it was verified. Paste the relevant log lines or
  test output — "works" is not evidence.
* One task per branch, one branch per PR.

## Style quick reference

C11, 4-space indent, 100-column soft limit, `snake_case`, braces always, one public
header per component, `stark_<component>_<verb>()` for public symbols, `static` for
everything else. Full contract in [ARCHITECTURE.md §4](ARCHITECTURE.md).
