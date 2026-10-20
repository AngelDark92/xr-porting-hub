# ASW integration defects — validation and fix plan (2026-10-20)

Scope: the 9 findings in the owner-supplied analysis of the shipped `gxr-spacewarp` path
(layer + driver + Companion). Read-only validation against the working trees; no source
edits, no builds, no device actions. This document is the plan; execution needs owner go.

Trees inspected:
- `steamlink-patches` @ `dev`, clean (`git status --short` empty).
- `GalaxyXR-Driver` @ `dev`, 27 modified files from a concurrent session (curve-preview /
  tray / settings-presentation work). `stream-frame-page.ts` is among them; the cited line
  317 matched the working tree on read. Do not revert or absorb that session's edits.

Labels: VERIFIED = re-read in source or executed here. CORRECTED = the analysis's mechanism
holds but a detail is wrong. NOT RE-DERIVED = plausible, not independently reproduced here.

---

## 1. Verdict per finding

| # | Finding | Verdict | Evidence |
|---|---|---|---|
| 1 | Only the first projection is selected; the rest are skipped | **VERIFIED** (code). Per-build role naming **NOT RE-DERIVED** | `extensions/gxr-spacewarp-layer/src/swp_dispatch.cpp:698-708` breaks at the first `XR_TYPE_COMPOSITION_LAYER_PROJECTION`, then uses `views[0]/views[1]` only |
| 2 | `uSadScale` is required in GME and H1 but inactive, so setup fails | **VERIFIED** (executed) | GME declares it at `src/swp_shaders.h:146`, H1 at `:197`, neither body reads it. `glslang -l -q` on both composed programs: reflection lists every other uniform, **no `uSadScale`**. Shipped `libgxr_spacewarp.so` (438488 B, sha256 `7fe4b20b…`) carries the same single declaration in both cores, unused |
| 3 | History stores swapchain texture ids, not snapshots; no src/dst separation | **VERIFIED** (code); the "writes after release" half is the repo's established pattern, not itself a defect | `swp_dispatch.cpp:143-151` (`FrameRecord` ids), `:504-519` (push ids), `:851-852` (warp from `realHistory.back()` into the pending texture). No `src != dst` check exists on the missed path (the real path has one at `:800`) |
| 4 | Timing conventions differ by one interval | **VERIFIED** (code + tests pin the wrong convention) | `swp_dispatch.cpp:237-247` divides from `arrivalNewest`; `swp_shaders.h:324` applies `1.0 - uScaleFactor`; `swp_est_worker.cpp:635` clamps `< 1.0` to 1.0. At one interval past newest → scale 1.0 → zero displacement. Tests assert exactly that at `tests/swp_dispatch_tests.cpp:486-488` ("one progress span -> 1.0") |
| 5 | Left eye written before right success; inherited GL state | **VERIFIED** (code) | `spacewarp_est.cpp:293-297` (`l && r`, no rollback); `swp_est_worker.cpp:85-121` saves only FBO/program/active unit/4 texture bindings/viewport; `:367-379` sets no viewport for the warp target |
| 6 | Driver compatibility guard reads hardcoded RVAs with no bounds check | **VERIFIED**, one detail **CORRECTED** | `VrlinkTap.cpp:137-143` memcmp's `module + kImplRva` / `kWorkerRva` with no `SizeOfImage`/`VirtualQuery` check. The pins are **16-byte prologue windows** each (`VrlinkTap.h:74-79`), not "two short byte sequences" — stronger than stated, still unbounded |
| 7 | Pending enable can race an explicit Off | **VERIFIED**, recovery **CORRECTED (stronger agreement with the code, weaker impact)** | `VrlinkTap.cpp:81-89` snapshots `enabled && !published`, releases the lock, then `DoPublish(true)` never re-checks `enabled` (`:199-201`). It does self-heal: `DeviceProvider::RunFrame` calls `UpdateEncoderSettings` every frame (`DeviceProvider.cpp:198,221`), which calls `SetConfig(false)` → `DoPublish(false)` retracts. Window ≈ one frame, not a stuck state |
| 8 | "Active" overstates the evidence | **VERIFIED** | Driver `status:"on"` means published only (`ConfigLoader.cpp:2194-2196`, `VrlinkTap.cpp:97-100`). UI prints "Async Spacewarp: Active" + "the headset synthesizes the intermediate frames" (`src-lit/features/stream-frame-page.ts:317-318`), also in the field tip `:311` and `messages.xlf:2606,2618`. `spacewarp-status.ts:11-14` already admits the driver cannot prove synthesis |
| 9 | Optical Flow exclusion reads cached status | **VERIFIED** | `src-lit/services/ofxr-session.ts:168` checks `isArmed()`/`status()` without a refresh; `selectVersion` in the same file refreshes first and says why (`:132-134`) |
| 10 | Toggle does not force half-rate rendering | **VERIFIED** (design fact, not a defect) | `swp_dispatch.cpp:137` `kMaxConsecutiveMissed = 2`; no cadence control anywhere in the layer. It is late-tick extrapolation, not VD-style half-rate motion smoothing |

Test coverage confirms the analysis's last paragraph: the dispatch suite fakes the estimator
(`tests/swp_dispatch_tests.cpp:110-160`) and builds **one projection + one overlay**
(`:365-401`), so findings 1, 2, 3, 4 and 5 cannot fail there. Finding 2's shader-syntax gate
compiles shaders but never links/reflects them (`tests/swp_shader_syntax.py` header).

## 2. What this means for the feature

- **As shipped, synthesis never runs.** Finding 2 makes `SwpInit` fail on a conforming GLES
  driver, so the session is stood down permanently. The driver still publishes 1 and the
  Companion still shows *Active*. Every other finding is downstream of a path that cannot
  execute.
- **Fixing 2 alone is not safe to ship.** It turns on a path with the timing inversion (4),
  the src/dst aliasing (3), the single-projection assumption (1) and the partial-stereo
  fallback (5). Fixing 4 without 3 makes the read-write conflict nonzero for the first time,
  so **3 must land with or before 4**.
- Finding 1 is a release gate for both bases, not only 5002363: on either base at most one
  video projection would move, so the composite shows motion on one layer and staleness on
  the other.

## 3. Fix plan

### Phase 0 — evidence (read-only, no code)

- **E0.1** Re-derive the per-base projection slot table from the decoded scene libs: for each
  persistent header (+0x1998/+0x19c8 on 5001712; +0x17e8/+0x1b90/+0x1bc0 on 5002363, per
  `builds/steamlink-patches/build/asw-20261017/01-steamlink-layers/REPORT.md:48-51,158`) trace
  `QSVLRendererXR::SetupSwapchains` (`0x10C780`, families at `0x10C840/0x10C8B0/0x10C920`) to
  the swapchain each header's views point at, and whether the frame path writes it.
  Output: `{submission index, role, extent, swapchain pair}` per base.
  This also closes the repo's own open question "which of the 2.0.20 pair is opaque vs masked"
  (`REPORT.md:192`); the analysis's naming is not in any repo record.
- **E0.2** If E0.1 is ambiguous, land one temporary one-shot topology log in the layer
  (layer index, type, viewCount, subImage extent, swapchain), enabled only in a
  session that already passed the enable gate, and read it from a patched-client run.

Gate: a slot table both bases agree with, or an explicit "unknown → do not warp" decision.

### Phase 1 — estimator can initialize (P0)

- **F1.1** Drop `uSadScale` from `gmeDefs` (`swp_est_worker.cpp:497-502`) and `h1Defs`
  (`:514-520`); drop the `sadScale` members from `GmeLocs`/`H1Locs` and the two
  `glUniform1f` calls (`:316`, `:360`). Keep it in the SAD core, which uses it.
- **F1.2** Extend `tests/swp_shader_syntax.py` to link each composed program
  (`glslang -l -q`) and assert the reflection name set equals the `ULocDef` arrays parsed
  from `swp_est_worker.cpp`. It fails today on `uSadScale`, passes after F1.1, and catches
  the next drift.
- **F1.3** Rebuild `libgxr_spacewarp.so` with `extensions/gxr-spacewarp-layer/Build-Native.ps1`,
  update `GXR_SPACEWARP_LIBRARY_SHA256` (`GxrSpacewarpLayerPatch.kt:27`) and any gate JSON
  that pins the payload, run `tests/Run-Tests.ps1` and `:patches:test`.

Note: this alone makes the shipped UI claim true only in the "init succeeded" sense; do not
ship it without Phase 2/3.

### Phase 2 — composition (P0)

- **F2.1** Replace the single-projection scan (`swp_dispatch.cpp:697-720`) with the Phase 0
  slot table: select every video projection, pass everything else through. Keep the existing
  `viewCount != 2` refusal per slot.
- **F2.2** Per-slot history: one `FrameRecord` chain per swapchain pair (key = the projection's
  `subImage.swapchain`), not one global `realHistory`. Pose/FOV/epoch/predDisp stay per slot.
- **F2.3** Set the warp viewport from `view.subImage.imageRect` (offset + extent) instead of
  inheriting the app's last viewport; refuse a slot whose rect extent differs from the
  estimator's configured input size.
- Gate: both video projections of one tick consume the same scale factor and the same
  arrival clock.

### Phase 3 — output correctness (P1)

- **F3.1** Own the warp destination: render each eye into an owned staging texture, then blit
  into the pending swapchain image only after **both** eyes succeeded. Removes the partial
  left/right write *and* the src==dst read-write conflict. If staging is rejected (memory /
  bandwidth), the minimum is: refuse when `src == dst` for either eye and restore both
  destinations on partial failure — the mixed-pair risk stays.
- **F3.2** Force and restore raster state in the warp pass: disable `GL_SCISSOR_TEST`,
  `GL_BLEND`, `GL_DEPTH_TEST`, `GL_CULL_FACE`; set the colour mask; restore all of it with the
  existing `SaveState`.
- **F3.3** Masked projection: if E0.1 confirms the fovea mask lives in alpha, warp RGB and
  keep alpha from the unwarped `src` sample (`uPreserveAlpha` uniform).
- Optional hardening: copy the two estimator source frames into an owned ring at real-tick
  time so history can never alias a recycled swapchain image. Only needed if E0.1 shows a
  pool shallower than the two-frame history window.

### Phase 4 — timing (P1, ordered after F3.1)

- **F4.1** `SwpScaleFactorForMissedTick` (`swp_dispatch.cpp:237-247`): measure progress from
  `arrivalPrev`, so one skipped interval yields 2.0 and the shader's `1 - scale` is correct.
  Update the comment block, `README.md`'s scale paragraph, and the two assertions at
  `tests/swp_dispatch_tests.cpp:486-491` ("one interval -> 2.0", "two -> 3.0 (cap)").
- **F4.2** Re-check `kMaxConsecutiveMissed = 2` against the corrected scale (the second missed
  tick now sits at the 3.0 cap).

### Phase 5 — host + Companion honesty (P2)

- **F5.1** `VrlinkTap::TryInstall`: bound-check both RVAs against the module image
  (`SizeOfImage` from `IMAGE_NT_HEADERS`, or `VirtualQuery`) before the memcmp; distinct
  fail-closed log. Keeps the 16-byte pins.
- **F5.2** `DoPublish(true)`: after taking `stateLock`, bail if `!enabled` (the snapshot in
  `OnVrlinkPublish` is advisory). One line; removes the one-frame stale ON.
- **F5.3** UI truth: status line and field tip stop asserting synthesis. Suggested:
  "Async Spacewarp: Active" → keep the state word, body → "The running stream carries the
  request; frames are synthesized only when the headset client patch is active." Update
  `messages.xlf:2606,2618`. The field tip's "smooths motion between streamed frames" should
  say it fills late display ticks (max 2 in a row), not that it halves the render rate.
- **F5.4** `ofxr-session.ts:166-176`: `await this.refreshStatus()` before the conflict check
  (same reason `selectVersion:132-134` already does).

### Phase 6 — verification

- Layer: `tests/Run-Tests.ps1`, `tests/swp_shader_syntax.py`, `:patches:test`, then a
  Manager-mimic apply on the exact release APK (the `.so` pin changed).
- Driver: `cargo test --locked --lib` (info seam untouched but the crate is in scope),
  `npm test` for the two GUI files, `npm run build:ui`.
- Runtime: fresh device evidence only. Needs Root Team gates (runbook + @verify + owner).
  No headset claim from any of the above.

## 4. Owner decisions

1. **Staging pair (F3.1) vs guard-only.** Staging is correct and costs 2 full-res textures +
   2 blits per missed tick. Guard-only is cheaper and leaves the mixed-pair risk. Recommend
   staging.
2. **Multi-projection policy (F2.1).** Warp every video projection (correct), or disable
   synthesis on bases with more than one video projection until the slot table is proven.
   Recommend warp-all once Phase 0 lands; disable otherwise.
3. **Masked-alpha policy (F3.3).** Confirm from E0.1 before coding; if the mask is not in
   alpha, drop the uniform.
4. **"Active" wording.** Keep the state word and fix the body (recommended), or rename the
   state to "Request delivered".

Out of scope for this pass: true half-rate synthesis (finding 10). That is a cadence feature,
not a defect fix; document it or schedule it separately.

## 5. Not established by this validation

- The per-build role naming in the analysis's table (background / masked fovea / additional
  projection). The code-level defect is independent of the naming; the *consequence* is not.
- Any runtime behaviour on the headset: no build, install, logcat or frame capture was run.
- Whether the estimator's budget holds on device once Phase 1 lets it initialize.

## 6. Execution log (append-only)

### 2026-10-20, wave 1

Owner go received: fix all validated findings, add the ASW mode option beside the toggle,
review at the end. Half-rate mode decision: the carrier value itself is the mode
(0 off / 1 late frames only / 2 half rate) on the existing id 0x48, so no second id, no new
RE, and an older host publishing 1 keeps the old behavior.

Landed in the main thread (source only, not yet built):

- Setting chain: `Config.h` (`spacewarpMode`), `ConfigLoader.cpp` (parse + default
  serialization + info seam `publishedValue`), `FrameProcessor.cpp` call site.
- GUI: `types.ts`, `driver-defaults.ts`, `stream-frame-page.ts` (select beside the switch,
  per-row reset covers both, honest status body, new tip), `ofxr-session.ts` (refresh
  before the exclusivity check), `messages.xlf` (tip and status rewritten, two option
  labels added).
- Layer: `swp_vts.cpp` carries the mode value and exposes `SwpVtsMode()`;
  `swp_dispatch.cpp` scale now measures from `arrivalPrev`; `swp_dispatch_tests.cpp`
  assertions corrected; layer README records the mode semantics.
- Docs: `Docs/StreamFrame.md` section, `GalaxyXRDriverGUI/README.md` paragraph.

Checks run: `npm run build:ui` (tsc clean, all three locale bundles, verify OK);
`vitest` on `settings-presentation.test.ts` + `galaxy-settings.test.ts` (159 passed).

Delegated (wave 1): A projection identity (read-only RE), B estimator uniforms +
reflection regression, C VrlinkTap bounds/publish-desired + mode value.

Still open: projection-slot restructure and staging rollback (both wait on A/B), estimator
raster-state fix (B), `.so` rebuild + Kotlin pin (after B), full checks, final review.

### 2026-10-20, execution (after wave 1 returned)

Projection identity resolved by disassembly (`builds/steamlink-patches/build/asw-fix-20261020/RE-projection-identity.md`):
5001712 submits `[base, foveated]` (both drawn); 5002363 submits `[underside, base,
foveated]` and only base+foveated are ever drawn — the underside layer is acquired and
released but never cleared, attached or bound. The foveal mask is the swapchain alpha
channel (`layerFlags=6`), not a separate texture. The pre-fix layer therefore warped the
underside projection on 5002363 and left the fovea stock on both bases.

Landed:

- Estimator: `uSadScale` removed from GME/H1 (definitions, uniform lookups, uploads); the
  shader-syntax gate now LINKS every composed program and fails when a declared uniform is
  absent from reflection (`tests/swp_shader_syntax.py`). Passes 9 compositions, 3 QCOM
  dialect probes; math port 20/20.
- Dispatch: two selected video slots (audited order, underside skipped by index), per-slot
  history, owned SRGB8_ALPHA8 staging committed by blit only after every eye of every slot
  succeeded, `src == dst` guard, forced raster state + explicit viewport in the estimator,
  alpha preserved at the unwarped uv, scale measured from `arrivalPrev` (one skipped
  interval = 2.0), half-rate mode on alternate presented ticks, bridge poll moved ahead of
  the projection checks, GL table extended with the five raster-state entries (the host
  suite caught them missing).
- VTS: carrier value 0/1/2 with `SwpVtsMode()`; older hosts publishing 1 keep the old
  behavior.
- Driver: `spacewarpMode` in Config/ConfigLoader/defaults/types/state/page + info seam
  `publishedValue`; mode select beside the toggle (Late frames only / Half rate); status
  body no longer asserts headset synthesis; OF exclusivity now refreshes live state.
- Pin: `libgxr_spacewarp.so` rebuilt, staged, `GXR_SPACEWARP_LIBRARY_SHA256` =
  `9a5783b018fe2cd14c7f262972eb77305af7f3bd4d0e97cebc9f7a832d809ded`.

Checks (2026-10-20): shader syntax + reflection PASS; math port PASS; dispatch host suite
2 remaining failures, both pre-existing at HEAD under this MSVC harness (25 failures at
HEAD, including the same two); `:patches:test` BUILD SUCCESSFUL, 223 tests / 0 failures /
14 skipped (assumption-gated decoded audits); GUI `build:ui` clean, 159 focused tests pass;
`SpacewarpToggleTest` compiles, links and passes 40 + 19 + 7 checks / 3 scenarios after
teaching its fake module a PE header and adding the `VrlinkFallbackTap` stubs.

Not verified: driver MSBuild/CI compile (C++ changes are source-checked only; the
SpacewarpToggleTest TU compiles the real `VrlinkTap.cpp` + `ConfigLoader.cpp`), headset
runtime, and the half-rate mode on device.

### 2026-10-20, review fixes (reviewer wave)

The reviewer's nine findings were folded in:

- Staging format is now the DESTINATION's internal format, probed with
  `glGetTexLevelParameteriv` (the OLED calibration patch can select 10-bit / FP16 video
  swapchains, and an ES blit requires identical formats). Unresolvable format = stand-down
  with a log, not a silent per-blit failure.
- Staging textures and both copy FBOs are deleted in `xrDestroySession` under the same
  context guard as the MV textures (they leaked four full-resolution images per reconnect).
- Every (src, dst) pair is validated (`glCheckFramebufferStatus`) before the first blit; the
  remaining mid-commit failure is logged as a mixed-composite risk, not as a clean
  stand-down.
- The host assertion now records all four extrapolation pairs and requires four distinct
  staging destinations outside both swapchain id sets.
- New 3-projection host test: `[underside, base, foveated]` selects slots 1/2, never
  touches the underside swapchains (acquired by the client), and maps slot 0 → base.
- README describes staging + commit; the mode-flip reset now actually happens (a 1 <-> 2
  change takes the reset branch via `lastMode`).
- `EnsureStagingLocked` restores the caller's active unit and binding.
- Leftover debug prints removed.

Rebuilt and re-staged `.so`: sha256
`867356426b648e91119661dc3c1b7be6aa4312a35315be11fac4f4130f64435e` (pin updated).
Re-ran: shader syntax + reflection PASS, math PASS, `:patches:test` BUILD SUCCESSFUL
(223 tests / 0 failures / 14 skipped), host dispatch suite still at the two pre-existing
failures only, GUI checks unchanged.
