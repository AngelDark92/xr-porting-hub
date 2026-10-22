---
name: cavecrew
description: >
  Decision guide for delegating to caveman-style subagents. Tells the main
  thread WHEN to spawn `cavecrew-investigator` (locate code), `cavecrew-builder`
  (1-2 file edit), or `cavecrew-reviewer` (diff review) instead of doing the
  work inline or using vanilla `Explore`. Subagent output is caveman-compressed
  so the tool-result injected back into main context is ~60% smaller — main
  context lasts longer across long sessions.
  Trigger: "delegate to subagent", "use cavecrew", "spawn investigator/builder/reviewer",
  "save context", "compressed agent output".
---

Cavecrew = three subagent presets that emit caveman output. Same job as Anthropic defaults (`Explore`, edit-style agents, reviewer); difference is the tool-result they return is compressed, so main context shrinks per delegation.

## Under Hermes (delegate_task, not presets)

Hermes does not read `.github/agents/` and has no named agent presets. Run `cavecrew-investigator`,
`cavecrew-builder` and `cavecrew-reviewer` as `delegate_task` children: paste the matching role contract
from [references/hermes-roles.md](references/hermes-roles.md) verbatim into each child's `context`, with
the goal and every path. Children inherit the parent's tools and model (no per-child tool list, no `haiku`
pin) and cannot call `clarify` — keep the `ambiguous. ask:` terminal line. A child's summary is a
self-report: the parent re-reads the cited lines and runs the checks itself.

## Numeric style

Write numeric quantities and ordinals as digits (`1`, `2`, `3`, `1st`, `2nd`), not spelled-out number words. Preserve exact quotes, identifiers, commands, and established names unchanged. Apply this to delegated prompts, subagent output, and the main-thread summary.

## When to use cavecrew vs alternatives

| Task | Use |
|---|---|
| "Where is X defined / what calls Y / list uses of Z" | `cavecrew-investigator` |
| Same but you also want suggestions/architecture commentary | `Explore` (vanilla) |
| Surgical edit, ≤2 files, scope obvious | `cavecrew-builder` |
| New feature / 3+ files / cross-cutting refactor | Main thread or `feature-dev:code-architect` |
| Review diff, branch, or file for bugs | `cavecrew-reviewer` |
| Deep code review with rationale + alternatives | `Code Reviewer` (vanilla) |
| One-line answer you already know | Main thread, no subagent |

Rule of thumb: **if you'd want the subagent's output in 1/3 the tokens, pick cavecrew. If you'd want prose, pick vanilla.**

## Why this exists (the real win)

Subagent tool results get injected into main context verbatim. A vanilla `Explore` that returns 2k tokens of prose costs 2k tokens of main-context budget every time. The same finding from `cavecrew-investigator` returns ~700 tokens. Across 20 delegations in one session that's the difference between context exhaustion and finishing the task.

## Output contracts

What main thread can rely on per agent:

**`cavecrew-investigator`**
```
<Header>:
- path:line — `symbol` — short note
totals: <counts>.
```
Or `No match.` Always file-path-first, line-number-attached, backticked symbols. Safe to grep with `path:\d+`.

**`cavecrew-builder`**
```
<path:line-range> — <change ≤10 words>.
verified: <re-read OK | mismatch @ path:line>.
```
Or one of: `too-big.` / `needs-confirm.` / `ambiguous.` / `regressed.` (terminal first token).

**`cavecrew-reviewer`**
```
path:line: <emoji> <severity>: <problem>. <fix>.
totals: N🔴 N🟡 N🔵 N❓
```
Or `No issues.` Findings sorted file → line ascending.

## Chaining patterns

For every delegated task in this project, pass the repository's `AGENTS.md` and
the external build contract: the repository patches several apps for the Galaxy XR
(Steam Link VR, 4XVR) and the root `AGENTS.md` app registry lists each app's package,
exact bases, source roots and extension payload — give every child the target app's
row and keep its edits inside that app's sources. All generated outputs, temporary checkouts, logs and
project caches belong under `../builds/steamlink-patches/` (on this workspace,
`D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/builds/steamlink-patches`).
Gradle uses `gradle/root` and `gradle/patches`; mixed retained fixtures/tools/evidence
use `build/`; native CMake uses `extensions/<name>/build-*` there. Do not create
repository-local build trees or clean retained inputs wholesale. See
[the build layout](../../../diagnostics/build-layout/README.md) and the local
[agent presets](../../../.codex/agents/). Reviewers must check output routing,
release consumers and clean/input separation. Static routing checks and cached
compiler passes do not establish a successful Morphe Gradle or GitHub CI build.

For patch/test changes in this repository (any app), include fresh-checkout input availability in the investigator/reviewer task. Use the [Morphe skill](../morphe-patches/SKILL.md) CI guidance: local ignored decoded APKs and decoded trees are not GitHub inputs, and absent-input audits must be distinguished from mandatory portable tests.

For build/CI failures, give the investigator the exact commit/run/job and first
failed task. Ask the reviewer to compare imported test libraries with Gradle test
dependency declarations, and to check that the full Gradle test/Android/catalog
gate precedes release preparation. A manual classpath or desktop fat JAR may
contain undeclared libraries: compressed reports must label fallback checks as
diagnostic, not Gradle/CI proof. Include actual commands, compiler/runtime versions
and executed/skipped counts; only the corrected SHA's workflow can establish its
CI result. Main thread retains responsibility for this final verification.
For external release bundles, reviewers must verify exact-version canonical
absolute attestation paths (`..` is rejected even in absolute patterns) and the
Gradle-only clean before release rebuilding, preventing stale preflight uploads.
Run the release path regression with the actual toolkit glob dependency. Local
Packages setup uses `tools/Configure-GitHubPackages.ps1`; never ask for a token
in chat, print it, or store it in the repository.

**Locate → fix → verify** (most common):
1. `cavecrew-investigator` returns site list.
2. Main thread picks 1-2 sites, hands paths to `cavecrew-builder`.
3. `cavecrew-reviewer` audits the diff.

**Parallel scout** (when investigation is broad):
Spawn 2-3 `cavecrew-investigator` calls in one message (different angles: defs vs callers vs tests). Aggregate in main thread.

**Single-shot edit** (when site is already known):
Skip investigator. Hand exact path:line to `cavecrew-builder` directly.

**Repo-wide text pass** (descriptions/labels across many files):
1. Main thread: build the inventory from the generated aggregate, sweep the test tree for
   pinned strings, list mirror surfaces (generated catalogs, readme docs).
2. Write per-batch scratch job files: role contract + allowed file list + locked phrases
   + receipt shape; then one delegate message with one child per directory batch.
3. Reviewer child over the full diff, then the main thread runs the regen pipeline and
   the remnant sweep itself.
Never edit a file a running child still holds — main-thread and child edits to the same
file race and lose edits; hold later waves until the children finish.

## What NOT to do

- Don't use `cavecrew-builder` when you don't already know the file. Spawn investigator first or main thread will eat tokens passing context.
- Don't chain `cavecrew-investigator → cavecrew-builder` for a 5-file refactor. Builder will return `too-big.` and you'll have wasted a turn.
- Don't ask `cavecrew-reviewer` for "general feedback" — it returns findings only, no architecture opinions. Use `Code Reviewer` for that.
- Don't expect prose. Cavecrew output is structured, sometimes terse to the point of cryptic. If a human will read it directly, paraphrase.

## Auto-clarity (inherited)

Subagents drop caveman → normal English for security warnings, irreversible-action confirmations, and any output where fragment ambiguity could be misread. Resume caveman after.
