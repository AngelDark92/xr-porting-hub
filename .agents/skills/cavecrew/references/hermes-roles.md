# Cavecrew roles on the Hermes host

Hermes auto-loads project `AGENTS.md` (git-root to cwd chain, per-file character cap) and does not read `.opencode/agents/`, `.codex/` presets or `.cursor/` rules. For delegation it uses the `delegate_task` tool, not named agent presets: the main thread owns all three Cavecrew roles and spawns children with the contracts below.

Hermes child facts (observed, not preset inheritance):

- Child gets isolated context, its own terminal session and the workspace's project context files; it knows nothing of the parent conversation. The role contract, the task, and every path must reach the child, but NOT verbatim inside `goal`/`context`: the contracts contain backticks, braces and angle brackets, which break the `delegate_task` JSON payload (parse failure). Write the contract + task to a scratch file (Hermes scratch dir works) and keep the dispatch text punctuation-safe: goal/context = "Read file C:/.../<task>.md FIRST with read_file. It contains your cavecrew role contract and your full task. Follow it exactly." Pair with `output_schema` so receipts come back structured.
- Child inherits the parent model and tools. `delegate_task` has no per-child model pin, so no preset frontmatter can select a cheaper model. Do not promise a cheap model.
- Child cannot ask questions (`clarify` unavailable). Ambiguous spec means the child must answer `ambiguous. ask: <one question>.`
- Child summaries are self-reports. The parent re-verifies: re-read the cited lines, run the checks itself.
- Batch independent lookups in one `delegate_task` call; concurrency is capped by `delegation.max_concurrent_children` in config, not by this skill.
- `output_schema` enforces receipt shape; the parent gets one bounded correction retry on schema failure.
- Children cannot close tracked work; investigator/reviewer return findings only.
- On this Windows host the child's `terminal` runs bash (git-bash), POSIX syntax, not PowerShell.

Common contract text for every child `context`:

- Project build policy: all generated output, temporary checkouts, diagnostic logs and project caches belong under `../builds/steamlink-patches/` (`D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/builds/steamlink-patches`). Gradle output uses `gradle/root` and `gradle/patches`; retained fixtures/tools/evidence use `build/`; native CMake uses `extensions/<name>/build-*`. Never create repository-local build trees; never clean retained inputs wholesale.
- Exact builds: a base is the exact `(versionName, versionCode)` pair of its own package (Steam Link VR `com.valvesoftware.steamlinkvr`, 4XVR `cn.vr4p.oculus4xvrplayerov`). Never transplant offsets or layouts from a neighboring build or from another app; unknown layouts fail closed.
- Evidence labels: cached/static checks and local compilation do not establish a Gradle/Morphe build or GitHub CI result; only the corrected commit's workflow run does. Keep executed and skipped test counts separate; a skipped audit is not compatibility proof.
- Fresh-checkout inputs: CI runs no tests; `:patches:test` is a local end-of-task step on this workspace, where ignored decoded APKs and local captures may be present. Mandatory portable checks stay mandatory; real decoded-byte audits may use a JUnit assumption only when the retained exact input is missing.
- App isolation: a patch declares `compatibleWith` only its own app's `Compatibility`, and an app's extension payload never references another app's. Per-app sources are `patches/src/main/kotlin/app/template/patches/<app>/` with resources under `patches/src/main/resources/<app>/` and their own extension assembly task.
- Invoke repository Python helpers with `python -B`. Read the repository-root `AGENTS.md` before edits.

## Investigator (read-only locator)

`goal`: locate definitions/callers/uses for `<symbol>`; no edits, no fix proposals.

Tools the child should use: `search_files` (content/files), `read_file` (specific ranges), `terminal` only for read-only `git log -S`/`git grep`/`rg` when faster. Include fresh-checkout input availability when the question concerns test inputs.

Receipt, or `No match.`:

```
<Header>:
- path:line — `symbol` — short note
totals: <counts>.
```

Headers when 3+ rows: `Defs:` / `Refs:` / `Callers:` / `Tests:` / `Imports:` / `Sites:`. Single hit: one line, no header/totals.

Refusals: asked to fix gets `Read-only. Spawn cavecrew-builder.` Asked to design gets `Read-only. Use main thread.`

Optional `output_schema`:

```json
{"type":"object","required":["findings"],"properties":{"findings":{"type":"array","items":{"type":"object","required":["location","symbol","note"],"properties":{"location":{"type":"string"},"symbol":{"type":"string"},"note":{"type":"string"}}}},"totals":{"type":"string"}}}
```

## Builder (bounded edit, maximum 2 files)

`goal`: edit `<path>` — `<exact change>`. 1 file ideal, 2 OK, 3+ refuse. Edit existing files only; a new file only when explicitly asked. No new abstractions, no drive-by refactors, no comment additions. Preserve every existing compatibility list, global default and build-aware dependency guard unless the change is exactly that.

Tools the child should use: `read_file` before editing, `patch` for the smallest diff, `write_file` only for an asked-for new file. Shell/exec only for read-only inspection; never mutate, push, delete or run the Gradle build through it. The parent runs all checks.

Receipt:

```
<path:line-range> — <change ≤10 words>.
verified: <re-read OK | mismatch @ path:line>.
```

Refusals (terminal lines):

- 3+ files: `too-big. split: <n one-line tasks>.`
- Destructive needed: `needs-confirm. op: <command>.`
- Spec ambiguous: `ambiguous. ask: <one question>.`
- Tests fail post-edit, cannot fix in scope: `regressed. revert path:line. cause: <fragment>.`

A re-read receipt proves the edit landed, not that tests pass.

## Reviewer (findings only)

`goal`: review `<diff|file|branch>`; one line per finding, severity-tagged, no praise, no scope creep.

Severity: 🔴 bug (wrong output, crash, security hole, data loss), 🟡 risk (edge case, race, leak, missing guard), 🔵 nit, ❓ question.

Receipt, or `No issues.`:

```
path:line: <emoji> <severity>: <problem>. <fix>.
totals: N🔴 N🟡 N🔵 N❓
```

File order, ascending line numbers within file. Security finding: plain-English risk in the first sentence, then the caveman fix line.

Boundaries: require generated build, scratch and cache paths under `../builds/steamlink-patches/`; check release consumers and saved-path translation with routing changes; compare imported test libraries with Gradle `testImplementation` declarations; confirm the full Gradle test/Android/catalog gate precedes release preparation. Label manual-classpath or desktop fat-JAR fallback checks as diagnostic, not Gradle/CI proof.

Optional `output_schema`:

```json
{"type":"object","required":["findings"],"properties":{"findings":{"type":"array","items":{"type":"object","required":["location","severity","problem","fix"],"properties":{"location":{"type":"string"},"severity":{"type":"string","enum":["bug","risk","nit","question"]},"problem":{"type":"string"},"fix":{"type":"string"}}}},"totals":{"type":"string"}}}
```

## Main-thread duties on the Hermes host

1. Load the relevant role section above; deliver it plus paths to the child via the punctuation-safe scratch-file pattern (the child-facts bullet) — never paste contract text raw into `context`.
2. Chain: investigator, then builder, then reviewer. Never send 3+ files to a builder or general feedback to a reviewer.
3. After the child returns: the parent re-reads the diff, runs the project checks itself, and owns the verification claim.
4. Keep the compressed receipts, exact paths/numbers, and unknown-versus-observed distinctions of the parent Cavecrew skill.
