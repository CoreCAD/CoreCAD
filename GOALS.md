# CoreCAD — Project Goals & Progress

## Vision

CoreCAD is a curated, open-source mechanical CAD platform built on FreeCAD's foundation.
It exists for engineers and makers who want a focused, stable, and disciplined environment
for mechanical design — without the distraction of experimental or domain-specific workbenches
that fall outside core mechanical CAD.

**Principles:**
- Stability over breadth
- Clarity over experimentation
- Long-term reliability over rapid feature expansion

---

## Architecture Progress (standing scorecard)

> Progress toward the goals of `corecad-strategy/ARCHITECTURE.md` (frozen v1.0.0).
> **Keep this updated as work lands** — it's the high-level dashboard. Last reviewed **2026-06-17**.

**Headline:** the architecture is **fully validated** (the §11 POC is complete — all 8
principles exercised against working code), but **implementation is early and narrow**: nearly
all real work landed in **one module (PartDesign/Body)**; the rest of the system still runs on
the old substrate. Proof ≈ done; production ≈ keystone converted, building not yet.

**Rough proportions:** validation **~100%** · core-substrate conversion (PartDesign Body model)
**~75%** · system-wide implementation **~15–20%**.

### Principles (P1–P8)

| | Principle | Status | Where it stands |
|---|---|---|---|
| P1 | Shapes are values | ✅ Validated | Largely inherent in OCCT/FreeCAD; confirmed (tool bodies referenced, not consumed) |
| P2 | Features are functions | ✅ Validated | Explicit `BaseFeature`/`Profile` links; DAG propagation proven |
| **P3** | **References, not ownership** | 🟡 Real progress, PartDesign-only | De-ownership is the *only* model in PartDesign (Stages 1–2 + 3a). Open: other modules still own; `insertObject`→`Group` remnant (3b); authored-sketch cleanup gaps |
| P4 | Every result modelable | 🟡 Partial | Sketch-on-boolean-face proven; Part-boolean→PartDesign-body & Surface gaps open |
| P5 | Topological names maintained | ✅ Validated | Element-map byte-identical across save/reopen |
| P6 | Absorb workflows, not workarounds | 🟡 Ongoing | De-ownership removes the `Group` crutch; `SubShapeBinder`/`Clone` still present elsewhere |
| P7 | Robustness through honesty | 🟡 Gaps characterized, unfixed | Fail-loud works where wired; silent-frozen refs, "body up-to-date while Tip invalid," bowtie-sketch silence open. Honesty-contract phase deferred |
| P8 | Programmatic equivalence | 🟡 Strong, one thread | Shared App/Python service + MCP exist; UI parity "by construction," not yet "by diff" |

### Structural pieces

| Piece | Status | Note |
|---|---|---|
| Body/Feature reference model (§3–4) | 🟡 ~75% | Converted keystone; Stages 1–2 + 3a done. Remaining: 3b cliff + `Group` remnant |
| Document type system / `.cpart` format (§7) | 🔴 Not started | **The long pole — *is* Stage 3b.** Highest-leverage next step (finishes P3 in PartDesign + unlocks §7) |
| Multi-body ops (§5) | 🟡 Partial | Cross-body Cut proven; split-body identity (§4.7 component-id) is a known gap with no cheap fix |
| UI contract (§8) | 🟡 Partial | Ambiguity prompts exist; config-selector + face-click-to-sketch not built |
| Other workbenches (Assembly, TechDraw, FEM, CAM, Sketcher integration) | 🔴 Untouched | Still upstream FreeCAD on the old ownership substrate |

---

## Current Sprint — Week of 2026-06-06 (POC Steps 2–6)

**Focus:** make real ownership→reference-model progress on a small, usable slice — *not* a full migration. Build the smallest real thing, use it, then correct. Each step: build → test (App/Part/PartDesign) → manual check → commit, in small tested increments.

**Branch:** `phase5/poc-step1` (already includes 11 tested upstream kernel fixes).

**Guardrails:** ARCHITECTURE.md is **frozen at v1.0.0 this week** — no edits. Deferred: POC Steps 7–8 (variants, STEP import), the rename (rides with the Phase 5 fork-break), branding, ribbon, icons.

**Escape hatch:** Day 2 (reference-based boolean) is the risk. If it balloons past a day, fall back to the veneer, log the debt in `POC_LOG.md`, and keep moving. Hitting Day 3 by Friday is a win, not a miss.

| Status | Day | Task |
|--------|-----|------|
| ⬜ | 0 | Freeze ARCHITECTURE.md (no edits this week); open `phase5/poc-step1` build as daily driver |
| ✅ | 1 | POC Step 2 — smoke-test to reproduce the auto-spawn blocker — **located**: the WIP's Pad-path anchor walk already spawns Body B correctly (verified via MCP: free sketch on global plane + existing Body A → Body B with valid 1000mm³ solid). Real remaining gap is the *sketch-creation* path (`shouldCreateBody`), not Pad. |
| ✅ | 1 | POC Step 2 — sketch-creation rework: sketches are now born **free** (no Body, no DlgActiveBody modal); `getBody` autoActivate turned OFF so the lone Body isn't silently activated. Pad anchor walk is the sole spawn decider. Commit `89ffeb709c`. |
| ✅ | 1 | POC Step 2 — build + tests + manual + commit — verified live via MCP: free sketch on a global plane (no modal) → Pad spawns a 2nd Body (1000 mm³ solid) beside BodyA; 122/122 App/Part tests pass |
| ✅ | 2 | POC Step 3 — boolean Cut already references its tool Body via `Boolean::Tools` (`PropertyLinkList`, global scope, "not owned"); the GUI command sets `Feat.Tools = [...]` with no group-move. **The Day-2 risk was pre-paid by Phase 2** (`924077753f`). No new code needed. |
| ✅ | 2 | POC Step 3 — manual verify (live MCP): Cut of BodyA by referenced BodyB → `BodyA.Tip = CutFeat` (modelable Body, vol 840), `BodyB` independent (not in BodyA.Group, parent group None) & intact (vol 320), Up-to-date, no errors |
| ✅ | 3 | POC Step 4 — verified live (MCP): real `PartDesign_NewSketch` on a boolean-derived face (`CutFeat:Face5`) attaches `FlatFace` + nests in Body A; Pad extends Body A (`Pad_A2.BaseFeature = CutFeat`, `BodyA.Tip = Pad_A2`, vol 860). Cross-body lineage resolves: BodyA tip transitively depends on BodyB via `CutFeat.Tools`; full DAG recomputes clean (0 dirty). No CoreCAD code needed. |
| ✅ | 3 | POC Step 4 — manual verify done (above); also no code change → nothing to build/commit beyond docs |
| ✅ | 4 | POC Step 5 — verified live (MCP): saved multi-body doc, closed, reopened → `CutFeat.Tools=['BodyB']`, BaseFeature chain, sketch attachment to boolean-derived face `CutFeat:Face5`, and BodyB independence all survived; **0 dirty objects on open** (reopened already up-to-date). Clean round-trip, no code needed. |
| ✅ | 4 | POC Step 6 — verified live (MCP): edited `BodyB_pad.Length` 20→6 (through-hole→blind pocket). Propagated across the reference boundary: `CutFeat` 840→904, `BodyA` 860→924; tip/BaseFeature preserved, no errors. **Toponaming held the boss's `CutFeat:Face5` attachment through the topology change.** Cleanly reversible (→20 restores 860/840). No code needed. |
| ⬜ | 5 | Model something small *yourself* in the build (daily-driver test) |
| ✅ | 5 | Write `POC_LOG.md` entries (Steps 3–6 + sprint-close): what works, what's still veneer, what the reference model still needs |
| ⬜ | 5 | Model something small *yourself* in the build (daily-driver test) — **carried into next sprint** |
| ⬜ | 5 | Decide next week from real friction → **decided: intra-body de-ownership (below)** |
| ✅ | — | **2026-06-16: Steps 2–6 re-run through the shared App-layer path** (post `045cc0e941`). Rebuilt to HEAD (prior binary predated the auto-spawn lift), PartDesign 8/8 green. Built one `.cpart` authored Steps 1→6, every spawn/extend through the real `PartDesign.resolveBaseBody` + `body.newObject` (same calls the GUI makes). All 🟢 on the API path: Step 2 independent Body B w/ 🟦/🟧 palette colours, no `App::Part`; Step 3 cross-body Cut (vol_A 4000→3360, B referenced not consumed); Step 4 sketch on `CutAB:Face5` → `resolveBaseBody` returns Body A for **extend, not spawn** (body count stayed 2), element map populated through boolean; Step 5 save/reopen **0 dirty**, element-mapped name byte-identical, no re-bind; Step 6 `Pad_A.Length` 10→8 propagates A→Cut→C, Body B not downstream, anchor identity preserved, reverts clean. **Caveat:** UI side is 🟢-by-construction (`decideBaseBody` wraps `resolveBaseBody`) not yet 🟢-by-diff — buttons not driven + two-file diff still open. Logged in `POC_LOG.md` 2026-06-16. |
| ✅ | — | **2026-06-16: Steps 7–8 substrate assessment.** §7.7 Variant config: `App::VarSet` exists but is only a flat variable bag — no options, no active-option switching, no per-option override layer; the Variant object is a near-from-scratch build on VarSet + expressions. §7.8 STEP re-import: `src/Mod/Import` produces **static `Part::Feature`** shapes (`StepShape` is not a Feature); no parametric Import feature, no source-hash/translator round-trip, no element-map-preserving re-import — a from-scratch build. **Both are net-new feature work, not verification.** Marked 🔴 (blocked on missing substrate) in the §11.2 tracker. |

> Note: the project is being renamed **CoreCAD → Cruth**; this doc still uses "CoreCAD" pending the deferred rename pass.

---

## Next Sprint — Week of 2026-06-15 (Intra-body de-ownership spike)

**Focus:** the unvalidated core. Last sprint proved the **cross-body** reference
model (booleans reference tool bodies, round-trips, DAG propagates). This sprint
attacks **intra-body ownership**: today features are still exclusive members of
`PartDesign::Body`'s `GeoFeatureGroupExtension`. Target (ARCHITECTURE §3.2/§3.3):
the Body's pipeline is **derived** by walking the `BaseFeature` chain back from
the Tip — no `GeoFeatureGroup`, containers are "derived views, not prisons".

**Thesis (why incremental works):** ownership is a *runtime interpretation layer*
over data that is already reference-based (`BaseFeature` is an explicit
`PropertyLink`; `LinkScope::Child`/`isAllowed()` are runtime checks, not
serialized semantics). De-ownership = removing runtime checks, phase by phase.
Phases 1–2 (Child→Global, direct cross-body refs) are done. See POC_LOG
"incremental de-ownership" design note.

**Guardrails:** ARCHITECTURE.md still frozen at v1.0.0. The end state is reached
via a **source-of-truth flip** (tree view + delete stop trusting `Group`, start
deriving from the chain) — sequence it, but each is a real flip. The Body's
**Origin/coordinate context** is the one role `BaseFeature` does NOT carry —
needs a deliberate design answer before the group is pulled.

**Escape hatch:** if a step balloons, keep ownership working as a backstop, log
the debt in POC_LOG, and move to the next concern. Reaching the delete-semantics
flip by Friday is a win.

| Status | Day | Task |
|--------|-----|------|
| ⬜ | 0 | Decide upstream sync vs cherry-pick: we are 557 commits behind `upstream/main`. Evaluate `26c895c30d` (PartDesign: relink base profile — relevant to reorder/delete), `aa0eb1716c` (toponaming Python API), `99422b51cc` (AllowCompound warning). Prefer a periodic sync into `main` over ad-hoc picks. |
| ✅ | 1 | **Feasibility probe** done (live MCP): a feature **can** be the Body's Tip while living at document level (out of `Body.Group`) — shape recomputes from `BaseFeature` alone and **`Body.Shape` follows the out-of-group Tip** (vol 1240). `Group` is **not** load-bearing for recompute/shape. Only two runtime guards exist: `Body::removeObject` retreats the Tip (mutation path), and the GeoFeatureGroup placement/Origin transform (coordinate context). |
| ✅ | 1 | Probe findings written to POC_LOG (2026-06-09 entry). Experiment C gives a concrete repro of the coordinate-context gap (`BaseFeature` doesn't carry Body placement) for the Day-5 design. |
| ✅ | 2 | **Tree derives from chain** (commit `1298bd49cb`): `ViewProviderBody::claimChildren` override walks the `BaseFeature` chain from Tip (pipeline order), nests profiles under their feature, surfaces auxiliary objects (Origin/datums/orphan sketches) so nothing vanishes. Build clean; App 464/ Part 274/ PartDesign 8 tests green; verified live (MCP): normal body tree unchanged `[Origin,Pad1,Pad2]`, **and a de-owned Tip (Pad2 pulled out of `Group`) now still appears in the tree** — old group-based code would have dropped it. Tree-only flip; 3D scene still routes through the group extension (Day 3/4 scope). |
| ✅ | 3 | **New-feature placement** (commit `6e9085ecc6`): `Body::addObject` gains a flag-gated path (`DeownedFeatureCreation`, off by default → `addObjectDeowned`) that wires `BaseFeature = old Tip` and `Tip = new feature` (plus `_Body` + origin relink) **without** adding to `Body.Group`. Covers the dominant tip-append gesture; mid-pipeline insert/reorder stay on the legacy group path (Day 4). Build clean; App 464 / Part 274 / PartDesign 8 green (flag off = legacy unchanged). Verified live (MCP, flag on): two-feature body (Pad1→boss Pad2) via `body.addObject()` → `Group` empty, `Tip`=Pad2, `Pad2.BaseFeature`=Pad1, **Body vol 1080** (shape follows de-owned Tip), tree `[Origin,Pad1,Pad2]` via Day-2 chain-derived `claimChildren`, correct tip visibility. |
| ✅ | 4 | **Delete semantics by reference.** *Feature delete DONE* (commit `d2ae2c90de`): `Body::removeObject` flag-gated path (`removeObjectDeowned`) heals the `BaseFeature` chain directly — chain successor (found by `getNextSolidFeatureByChain` doc scan) relinks to the removed feature's base, Tip retreats, Group never consulted; `ViewProvider::onDelete` resolves the Body via `_Body`/`getFeatureBody` not Group-based `findBodyOf`. App 464/Part 274/PartDesign 8 green (flag off unchanged). Live: middle-delete on a de-owned Pad1→Pad2→Pad3 body heals Pad3.Base→Pad1, Tip stays Pad3, no cascade, recomputes to vol 1012. *Remaining:* (a) **body retirement** — *reframed 2026-06-10*. The original "body delete = delete exclusively-owned features by chain" wording was prison/ownership thinking, inverted from the doc: Bodies are **derived views downstream of the chain** (`Body.Shape ← Tip ← BaseShape`), and per §3.3/§4.7 **the user does not destroy Bodies** — they auto-**retire** when their Tip stops producing a component. On the FreeCAD substrate the §4.6/§4.7 component-id machinery doesn't exist yet, so "delete body" is a substrate artifact. **Decision (option 2): stub auto-retire — DONE** (commit `3491245b43`): when a feature-delete empties a Body's chain (Tip→null), the Body removes itself in `removeObjectDeowned` (self-removal is the last action; `removeObject` may destroy the object out-of-transaction). A small step *toward* §4.7, not the cascade. Per §4.6 the Body retires even if still referenced — dangling refs fail loudly (P7), not suppressed. Debt logged in POC_LOG 2026-06-10. App 464/Part 274/PartDesign 8 green (flag off unchanged). Live (MCP, flag on): single-feature body → delete its only feature → **Body retires** (Origin goes too, no orphan shell); two-feature body → delete **tip** → **Body survives**, Tip retreats, vol 1000 → delete last feature → **Body retires**. (b) **body-eviction regression — RESOLVED** (already fixed by Phase 2 `924077753f`): `PartDesign::Boolean` no longer inherits `GeoFeatureGroupExtension`; tool bodies live in a plain `Tools` link list ("not owned") so they're never re-parented into the Boolean, making delete-time eviction structurally impossible. Verified not-reproducible live 2026-06-10 (flag off *and* on): cross-body Cut, delete it → both bodies stay in `Part.Group`. **Day 4 fully done.** |
| ✅ | 5 | **Origin/coordinate-context design** (POC_LOG 2026-06-12): no gap in the target model — context comes from a feature's *attachment* (`AttachmentSupport`/§8.5 anchor walk), not its containment. Three settled decisions: (1) attachment defines the frame; (2) one document-level Origin per `.cpart`, not per-Body; (3) **the modeling Body has no `Placement` — it is the derived visual result and owns nothing** (load-bearing; already implied by §3.3). Experiment-C divergence was an artifact of a Body placement the architecture deletes. Substrate path: cheap interim guard (pin de-owned `Body.Placement` to identity), later atomic flip (Origin→document level, drop Body placement). Open research item (Sean): "coordinate system always references world origin" — holds as semantic root, with the caveat that relative attachments stay stored by-reference, not baked. Ergonomic caveat: loses drag-the-body gesture. |
| ✅ | 5 | **Validation** (POC_LOG 2026-06-12): all four checks pass live (MCP, flag ON) on a multi-body de-owned model (Body A `PadA1←PadA2←CutAB`, cut by referenced tool Body B). (1) **De-owned structure** — both `Group`s empty, chain + `Tools=[BodyB]` correct, BodyB independent, 0 dirty/no errors. (2) **DAG propagation both ways** — intra-body `PadA1` 10→12 (vol_A 840→1008) and cross-body tool `PadB1` 20→8 (vol_A 840→936), both reverse clean. (3) **Round-trip** — save/close/reopen preserved empty Groups + chain + Tools + independence, reopened 0 dirty. (4) **Eviction regression + heal** (inside `App::Part`) — delete cross-body `CutAB` → no eviction (part.Group unchanged, both bodies stay parented), Tip self-heals `CutAB→PadA` (vol→1000), BodyB intact. Rebuilt to HEAD first; baseline App 464/Part 274/PartDesign 8 green (flag off). Daily-driver friction: `PartDesign::Boolean` has no `addObjects` Python binding (set `.Tools` directly). Flag restored to default off. **Intra-body de-ownership sprint Days 0–5 complete.** |

> Carry-over from last sprint: live test doc at `/tmp/poc_step5_roundtrip.FCStd`
> (BodyA cut by referenced BodyB, extended by a sketch-on-boolean-face Pad).

### Substrate flip — Stage 2 COMPLETE (2026-06-17)

The de-ownership model is now the **only** path, not a flag-gated experiment. Full plan +
staging in `corecad-strategy/FLIP_PLAN.md`.

| Status | Cut | What |
|--------|-----|------|
| ✅ | 2.1 | `ViewProviderBody::claimChildren3D` derives the 3D scene graph from the chain (commit `06efd1cb42`). |
| ✅ | 2.2 | `DeownedFeatureCreation` default flipped → on; de-ownership is the default GUI path (commit `b16f06a159`). |
| ✅ | 2.3 | `Body::addObject`/`removeObject` route through the de-owned impls unconditionally; legacy Group branches + the flag **deleted** (commit `2cd2828ac4`). PartDesign 8/8. |

**The architectural bet is closed in practice: one model, no flag, no dual path.** `Group`
is left present but dormant. Stages 3–4 (one document-level Origin, drop `Body.Placement`,
remove `GeoFeatureGroupExtension`) are **deferred to the `.cpart` document-format work** —
the irreversible, user-invisible part, where Stage 3 *is* essentially the `.cpart` format.

> ⚠️ **Flagged loose end (found 2026-06-17 during a `Body.cpp` cleanup):** the "no dual
> path" claim has an asterisk. `Body::addObject`/`removeObject` are de-ownership-only, but a
> **second, still-live `Group`-writing path** survives: `Body::insertObject` (→ `setBaseProperty`
> → `getPrev/NextSolidFeature`, which read `Group` order). It is reached from
> `Body::onDocumentRestored`, `PartDesign::Feature`, `FeatureTransformed`, the GUI insert
> commands, and the `BodyPy` Python API. So mid-pipeline insert, restore, and transformed-feature
> wiring still depend on `Group`. This is not cruft to delete piecemeal — it is the unfinished
> remainder of the flip and belongs in **Stage 3b** scope (resolve when removing `Group`/
> `GeoFeatureGroupExtension`), not a quick patch.

> 📦 **Deferred refactor — `Body.cpp` modularity (analysed 2026-06-17, do AFTER this phase).**
> `Body.cpp` (~850 lines) is ~⅓ framework-mandated DocumentObject hub code (can't move), so
> line count is a weak smell. Two real wins, both deliberately deferred until the phase closes:
> (1) **Extract base-body resolution** — `walkAnchorChain` + `MaxAnchorWalkDepth` (anon ns) +
> `Body::spawnAutoBody` + `Body::resolveBaseBody` (~90 lines, currently lines 77–184). It's a
> self-contained "which Body owns this sketch?" policy (§8.5/§4.6) mis-housed on `Body` as
> statics; extracting to free functions in the `PartDesign` namespace (`BaseBodyResolver.{h,cpp}`)
> makes it independently testable. **Blast radius is small — only 2 callers:** `decideBaseBody`
> (Gui `Command.cpp:98`) and the Python binding `Module::resolveBaseBody` (`AppPartDesignPy.cpp:74`).
> (NB: leave the `bodyPalette`/`paletteColorFor` identity-colour helper in `Body.cpp` — it's used
> by the ctor + `setupObject`, a *different* concern from resolution; I mis-grouped it earlier.)
> (2) **Decompose `onChanged`** — a 5-branch dispatcher (~77 lines) into private per-property
> helpers; in-file readability only. The remaining bloat (dual Group/chain ordering) should be
> shed by finishing 3b, not by filing it into tidier boxes.

---

## POC Follow-Up Concerns

Real questions surfaced *during* the POC that are out of POC scope but must not
be lost. The POC proves a mechanism works; these are the gaps between "mechanism
proven" and "product a user can actually drive." Each is a deliberate follow-up,
not a defect in the POC.

| Status | Source | Concern |
|--------|--------|---------|
| ⬜ Todo | Step 7 (Variant configs, 2026-06-16) | **No user-facing way to choose a configuration.** The mechanism works — switching the active option rebuilds the model — but the only way to switch today is editing the active-option property in the property panel. A user needs a first-class selector: a dropdown in the toolbar (SolidWorks/Onshape model — pick "M6 / M8" and the part updates). The ARCHITECTURE (§7.7 "MVP Scope") explicitly defers the configuration-table editor UI to post-POC, so this is expected; recording it so the UI work is scheduled, not assumed. |
| ⬜ Todo | Step 8 (STEP re-import, 2026-06-16) | **Full parametric STEP-import feature is product plumbing, descoped from the POC.** A live spike proved the only *architectural* question — stable face identity for history-less imported geometry (geometric-signature fingerprint, green/red match) — so the core contract is validated. The remaining build is real but is feature work, not validation: a parametric Import feature keyed on a **source content hash** (not file path), auto-spawning its BRep into a Body, the native C++ face-signature seeder (`BRepAdaptor_Surface`/`BRepGProp`/`BRepLProp` — see OCCT notes), and the **yellow case** (symmetric-feature ambiguity under re-positioning, needs topological-context/curvature signature components). Schedule as a product feature. |

---

## Goals

### 1. Branding
Distinguish CoreCAD from FreeCAD visually and in metadata, while respecting the FreeCAD
community and keeping internal compatibility intact.

| Status | Task |
|--------|------|
| ✅ Done | Replace README with CoreCAD content |
| ✅ Done | Add CoreCAD logo/SVG icon |
| ✅ Done | Create `corecad/branding.xml` (window title, app name, vendor) |
| ✅ Done | Add CoreCAD Windows installer icons |
| ✅ Done | Fix CLI/GUI startup banner in `src/Main/MainGui.cpp` |
| ✅ Done | Fix CLI startup banner in `src/Main/MainCmd.cpp` |
| ✅ Done | Update LicenseInfo/CreditsInfo strings in `src/Main/FreeCADGuiPy.cpp` |
| ✅ Done | Update desktop integration files (`src/XDGData/`) and all downstream `org.freecad.FreeCAD` references |
| ✅ Done | Update Windows installer script (`package/WindowsInstaller/FreeCAD-installer.nsi`) |
| ✅ Done | Update stylesheet names (`src/Gui/Stylesheets/FreeCAD.qss`, preference packs) |
| ✅ Done | Stage branding assets in build tree only (source tree never modified, no accidental commits) |
| ✅ Done | Stage Windows installer icons (`*.ico`, `*.bmp`) in build tree via `corecad_stage_assets()` |
| ⬜ Todo | Fix Assembly/Material icon paths in `corecad-assets` to match their QRC layout |
| ⬜ Todo | Customise CoreCAD default color scheme (preference pack `.cfg` files, set default theme at startup) |
| ⚠️ Partial | Update GitHub repository files (issue templates, PR template, FUNDING.yml) — see note below |
| 🔮 Future | Add FreeCAD original authors to an Acknowledgements section in the About dialog |
| 🔮 Future | Investigate renaming `FreeCAD`/`FreeCADCmd` binaries to `CoreCAD`/`CoreCADCmd` |

> Internal Python module names (`FreeCADGuiInit.py`, etc.) are intentionally left unchanged
> — renaming them would break the module import system.
> See `.local/BRANDING_CHECKLIST.md` for file-level detail on all items above.
> See `.local/BRANDING_RIPPLE_ANALYSIS.md` for the full impact analysis of remaining work.
>
> ⚠️ **GitHub repository files:** Issue templates, PR template, and FUNDING.yml are done.
> `CONTRIBUTING.md` (repo root) still has ~15 FreeCAD references that should be CoreCAD
> (exception: "FreeCAD project association" refers to the upstream legal entity and should stay).
>
> 🔮 **Future** items are deferred due to complexity or dependency on other work. Binary renaming
> would affect CI, packaging, addon compatibility, and all desktop integration files.
> Acknowledgements should credit Juergen Riegel, Werner Mayer, Yorik van Havre, and the broader
> FreeCAD community — important for LGPL2+ good practice.

---

### 2. Curated & Locked Workbench Set
Users cannot add or remove workbenches at runtime. The included workbench set is controlled
by the developer at build time via `cMake/CoreCAD_Options.cmake`.

| Status | Task |
|--------|------|
| ✅ Done | Disable Addon Manager (`BUILD_ADDONMGR=OFF` in `CoreCAD_Options.cmake`) |
| ✅ Done | Remove Workbenches preferences tab (`src/Gui/resource.cpp`) |
| ✅ Done | Decide which upstream workbenches to exclude from CoreCAD |

**Excluded workbenches** (set OFF in `cMake/CoreCAD_Options.cmake`):
- `BUILD_ROBOT` — robotics simulation
- `BUILD_OPENSCAD` — OpenSCAD integration
- `BUILD_WEB` — embedded web browser
- `BUILD_TUX` — mascot/novelty module
- `BUILD_BIM` — architecture/BIM (out of mechanical CAD scope)
- `BUILD_IDF` — PCB board file import (too niche)
- `BUILD_POINTS` — point cloud tools (scan-to-CAD is a specialty workflow, not core mechanical CAD)
- `BUILD_REVERSEENGINEERING` — scan-to-CAD reverse engineering; depends on Points
- `BUILD_INSPECTION` — mesh/point cloud inspection; depends on Points
- `BUILD_PLOT` — legacy matplotlib plotter (maintained externally as addon)

**Already OFF upstream** (no action needed): `BUILD_CLOUD`, `BUILD_SANDBOX`

---

### 3. RibbonUI as Default Interface
Bundle a ribbon UI as the standard CoreCAD UI, replacing the default toolbar/menu system.
CoreRibbon (`src/Mod/CoreRibbon/`) is the clean, native implementation replacing FreeCAD-Ribbon.

| Status | Task |
|--------|------|
| ✅ Done | Evaluate RibbonUI addon ([APEbbers/FreeCAD-Ribbon](https://github.com/APEbbers/FreeCAD-Ribbon)) |
| ✅ Done | Add as git submodule (`src/Mod/FreeCAD-Ribbon`, tracking `develop` branch) |
| ✅ Done | Consolidate Part + PartDesign into a single "Part" ribbon tab (`src/Mod/CorePart/InitGui.py`) |
| ✅ Done | Implement CoreRibbon v1 (`src/Mod/CoreRibbon/`) — ~600 LOC, 5 focused files, no JSON config |
| ✅ Done | Suppress FreeCAD-Ribbon when `BUILD_CORERIBBON=ON` (default) |
| ✅ Done | Build and test CoreRibbon; fix height clipping, tab order, workbench filtering |
| ✅ Done | Apply `styles/ribbon.qss` stylesheet via controller (`_load_stylesheet()` in `ribbon_controller.py`) |
| ✅ Done | Move Individual Views to title row; suppress Individual Views + Structure from workbench panels |
| ⬜ Todo | Tune button sizes — promote primary actions to large icons, keep secondary small |
| ⬜ Todo | Edit panel names and panel contents (names currently come from FreeCAD toolbar names) |
| ✅ Done | Remove FreeCAD-Ribbon submodule once CoreRibbon is confirmed stable |

> **CoreRibbon configuration** lives entirely in FreeCAD parameters at
> `User parameter:BaseApp/Preferences/Mod/CoreRibbon`. Key defaults (in `config.py`):
> - `IgnoredWorkbenches` — excludes Part, PartDesign, None, Test, Draft, FEM, CAM
> - `IgnoredToolbars` — excludes Clipboard, Edit, File, Help, Macro, View, Workbench,
>   Individual Views, Structure
> - `TabOrder` — CorePart → Sketch → Assembly → Drawing → Surface → Mesh → Spreadsheet → Material
> - `DefaultWorkbench` — CorePartWorkbench
>
> No JSON config files. All settings are readable/writable from the FreeCAD Preferences dialog
> (parameter editor) or `App.ParamGet(...)` from the Python console.
>
> For functional testing, reset `~/.FreeCAD/user.cfg` to clear any cached workbench state.

---

### 4. Build & Developer Infrastructure
Ensure the project is reproducible and easy to work with.

| Status | Task |
|--------|------|
| ✅ Done | Set up `cMake/CoreCAD_Options.cmake` as home for all build overrides |
| ✅ Done | Build-tree branding asset staging (`cMake/CoreCAD_Branding.cmake`, `scripts/branding-assets.txt` allowlist) |
| ✅ Done | Set up pre-commit hooks (clang-format, black) |
| ✅ Done | Create `CLAUDE.md` for AI-assisted development |
| ✅ Done | Set up branching strategy (`main` mirrors upstream, `develop` holds customisations) |
| ✅ Done | Display CoreCAD version in title bar, separate from internal FreeCAD build version |
| ⬜ Todo | Define CoreCAD versioning strategy (CI injection, About dialog) |
| ⬜ Todo | Unify branding pipeline into a single `corecad_brand_module()` CMake macro — currently each module must be wired up by hand, and Python-first workbenches (those with `PYSIDE_WRAP_RC`) require an extra step that C++-first ones don't. The macro should accept a flag for whether a Python `_rc.py` is needed and handle both `qt_add_resources` and `PYSIDE_WRAP_RC` from the staged QRC automatically. Also audit all modules using `PYSIDE_WRAP_RC` that don't yet have `corecad_stage_resources` wired up. |

> **Versioning note:** CoreCAD uses a display-only version (`CoreCADVersionMajor/Minor/Patch/Suffix`
> in `corecad/branding.xml`) that is shown in the title bar. The internal FreeCAD build version
> (`BuildVersionMajor/Minor/Point`) is left untouched so workbenches and addons continue to pass
> their compatibility checks (e.g. `FreeCAD.Version()[1]`). This avoids the addon-breakage problem
> seen in Ondsel ES, which overwrote the FreeCAD version with CalVer and broke third-party addons.
> Current CoreCAD version: **0.1.0-dev**.
>
> Remaining work: inject the version from `corecad_manifest.yml` at CI build time, and surface the
> CoreCAD version in the About dialog alongside the underlying FreeCAD version.

---

### 5. Start Page & Examples

Curate the Examples section on the start page to reflect CoreCAD's mechanical CAD focus.

| Status | Task |
|--------|------|
| ✅ Done | Remove BIM/Arch examples (`BIMExample.FCStd`, `ArchDetail.FCStd`) — `BUILD_BIM=OFF`, workbench disabled |
| ✅ Done | Remove `draft_test_objects.FCStd` — developer test artifact, not a polished user example |
| ✅ Done | Remove `Schenkel.stp` — obscure German name, raw STEP file with no description |
| ✅ Done | Add Utah Teapot mesh examples (`utah-teapot.stl`, `utah-teapot.obj`) — public domain / CC0, good Mesh workbench test assets |
| ⬜ Todo | Add a replacement STEP import example: English name (e.g. `BracketImport.stp`), simple mechanical part, demonstrates STEP round-trip capability |

> The replacement STEP example should: use a recognisable mechanical part (bracket, flange, etc.),
> have a descriptive English filename, and ideally include a matching `.FCStd` alongside it to
> show the imported result. The four remaining examples (`EngineBlock.FCStd`,
> `PartDesignExample.FCStd`, `FEMExample.FCStd`, `AssemblyExample.FCStd`) cover the core
> mechanical CAD workflows and are the right set to keep.

---

### 6. New-User Experience
Remove friction for first-time users — commands that create top-level objects should work
immediately without requiring a manual "File → New" step first.

| Status | Task |
|--------|------|
| ✅ Done | Add `Command::ensureActiveDocument()` helper to `Command` base class |
| ✅ Done | `Std_Part`, `Std_Group`, `PartDesign_Body` auto-create a document if none is open |
| ✅ Done | `Assembly_CreateAssembly` always creates a new document for root assemblies |
| ✅ Done | Audit remaining top-level commands — extend to Part primitives, Spreadsheet, FEM_Analysis |
| ✅ Done | Fix `isThereOneRootAssembly()` crash on cold start — `Gui.activeDocument()` was None |
| ✅ Done | Fix `Part_Tube` cold-start — Python command in `BasicShapes/CommandShapes.py` missed the pattern |
| ✅ Done | `Mesh_Import` auto-creates a document if none is open (file dialog cancel does not create a document) |

> Root assemblies always land in a fresh document (`App.newDocument()` in `Activated()`).
> Sub-assemblies nested inside an already-active assembly stay in that assembly's document.
> The helper `ensureActiveDocument()` is in `src/Gui/Command.cpp`; see
> `src/Gui/AUTO_DOCUMENT_CREATION.md` for criteria and revert instructions.
>
> **TechDraw intentionally excluded:** a drawing page only makes sense alongside existing geometry,
> so `TechDraw_PageDefault` and `TechDraw_PageTemplate` remain gated on `hasActiveDocument()`.
> **Sketcher intentionally excluded:** users should create a Body first; the PartDesign workflow
> then prompts for a sketch automatically.
> **Part_Tube note:** implemented as a Python command in `src/Mod/Part/BasicShapes/CommandShapes.py`,
> not alongside the other C++ parametric primitives in `CommandParametric.cpp`.

---

### 7. CorePart — Unified Part Creation Workbench

Eliminate context switching between the Part and PartDesign workbenches by making CorePart
the single entry point for all part creation. The raw Part and PartDesign workbenches remain
under the hood but are hidden from users.

**Strategic split:**
- **Open layer (CoreCAD repo):** parametric foundation, container model, dialog quality,
  Part/PartDesign consolidation. All built on the existing `App::Part` container work.
- **Proprietary layer (private repo, like CoreRibbon):** premium UX interactions — smart
  face manipulation, inline constraint visualization, advanced dialogs. Not open-sourced yet.

**What Part has that must be preserved in CorePart:**

| Category | Commands | Why |
|----------|----------|-----|
| Multi-body Booleans | Fuse, Cut, Common, XOR | Combining solids from different Bodies/imports |
| Imported geometry | Defeaturing, ShapeFromMesh, RefineShape, CheckGeometry | STEP imports aren't Bodies |
| Compounds | Compound, BooleanFragments, Slice, SliceApart, ExplodeCompound | Multi-solid work, FEA prep |
| Wire/face operations | Extrude/Revolve on arbitrary wires, MakeFace, RuledSurface | Non-sketch geometry |
| Topology repair | MakeSolid, ReverseShape, Offset, Offset2D, Scale | Shape repair, no PartDesign equivalent |
| Join operations | JoinConnect, JoinEmbed, JoinCutout | Hollow shape topology (pipes, vessels) |

Everything else (primitives, sketch-based features) is covered by PartDesign with better dialogs.

#### Open Layer Tasks

| Status | Task |
|--------|------|
| ✅ Done | `App::Part` container — all Part operation results placed inside active Part |
| ✅ Done | Smart CAD import — adapt placement to file structure (four structural cases) |
| ✅ Done | Gate Part and PartDesign commands on active Part or Body context |
| ⬜ Todo | Suppress raw Part and PartDesign workbenches from the UI (hidden, not removed) |
| ⬜ Todo | **Harmonise boolean tool-body contract:** `Part::Cut` shows input bodies as dependency tree children; `PartDesign::Boolean` leaves them as independent objects but makes them invisible. Both preserve §5.3 tools-persist but the UX contracts differ — users cannot predict what happens to inputs without knowing which workbench they used. Standardise on one convention (see `.local/compact-phase3.md` Deferred §2). |
| ⬜ Todo | Surface essential Part-only operations (multi-body Boolean, Defeaturing, Compounds) in CorePart ribbon |
| ⬜ Todo | Improve Part primitive dialogs to match PartDesign quality |

#### Proprietary Layer Tasks (UX — private repo)

Informed by competitive audit of Fusion 360, Onshape, SolidWorks, Plasticity, and Shapr3D.
The Shapr3D model is the target: interactions always work, parametric history is created silently
in the background, users never need to think about which mode they are in.

**Tier 1 — implement first:**

| Status | Task |
|--------|------|
| ⬜ Todo | **Face-click-to-sketch:** clicking a planar face immediately activates sketch tools on that plane — no separate "create sketch" dialog (Shapr3D model). **Known blocker:** `SketchWorkflow::findAndSelectPlane()` silently swallows face clicks when no face is pre-selected; only planes (origin/datum) appear in its list. Fix: default `NewSketchUseAttachmentDialog` to `true`, or route face-hover clicks to the attachment dialog. See `.local/compact-phase3.md` Deferred Items §1. |
| ✅ Done | **Sketch creation cycle guard:** `SketchWorkflow.cpp:232` — before placing sketch, `activeBody->getInListEx(true).count(supportObj)` detects DAG cycle; auto-creates and activates new Body. Fully verified 2026-04-22: DAG logic via MCP (4c), GUI tests 4a.1–4a.3 manually confirmed in running CoreCAD. |
| ⬜ Todo | **Press/Pull with graceful fallback:** drag any face → creates Pad/Pocket if sketch origin exists, Move Face feature otherwise — never refuses to act (Fusion/Shapr3D model) |
| ⬜ Todo | **Unified Add/Remove dialogs:** consolidate opposite-pair commands into single dialogs with an Add/Remove material toggle — Extrude (Pad↔Pocket), Revolve (Revolution↔Groove), Loft (AdditiveLoft↔SubtractiveLoft), Sweep (AdditivePipe↔SubtractivePipe), Helix (AdditiveHelix↔SubtractiveHelix). Matches Inventor/Fusion UX; eliminates the need to choose the "right" command upfront. 100% Python task panels in the proprietary layer; dispatch to the appropriate PartDesign command on confirm. **Prerequisite:** CorePart ribbon surfacing these commands and raw Part/PartDesign workbenches suppressed, so the unified dialog is the sole entry point. |
| ⬜ Todo | **Inline constraint status coloring:** black = fully defined, blue = under-constrained, red = over-constrained as persistent geometry overlays (SolidWorks model) |

**Tier 2 — after core is stable:**

| Status | Task |
|--------|------|
| ⬜ Todo | **Section view (clipping plane):** interactive display-only clipping plane to expose interiors — no geometry created or modified. Click a face to place the plane; toggle which side is clipped by view direction. Replaces `Part_SectionCut` for inspection (that tool incorrectly creates `BooleanFragments` geometry). API: `view.toggleClippingPlane(pla=...)` (`View3DPy.cpp:321`). 100% Python, zero C++ changes needed. |
| ⬜ Todo | **Interference & clearance analyser:** highlight faces of two or more solids that overlap or are within a threshold distance. API: `shape.proximity(other, tol)` and `shape.distToShape(other)` (`TopoShapePyImp.cpp:2500, 2864`). Colour-code interfering faces in the 3D view; show clearance gap measurements. |
| ⬜ Todo | **Geometry metrics panel:** persistent panel showing area, volume, centre of gravity, and bounding box dimensions for the selected object — no Python console required. API: `shape.Area`, `shape.Volume`, `shape.CenterOfGravity`, `shape.BoundBox` (all properties on `TopoShape`). |
| ⬜ Todo | **Geometry Doctor:** guided repair flow for imported STEP/IGES geometry. Detect issues via `shape.check()` / `shape.isValid()`, suggest and apply fixes via `shape.fix()` / `shape.fixTolerance()` (`TopoShapePy.cpp:259-837`). Before/after 3D preview. |
| ⬜ Todo | **Face origin inspector:** select a face → highlight the feature that created it using `shape.getElementHistory()` and `shape.mapShapes()` (`TopoShapePy.cpp:1127-1136`). Directly addresses the "where did this face come from?" gap that no CAD tool solves well. |
| ⬜ Todo | **"Extrude to face" snap:** dragging an extrusion snaps to opposing faces with visual highlight |
| ⬜ Todo | **Timeline scrub:** roll feature history back to any point for editing (Fusion model) |
| ⬜ Todo | **History opt-in mode:** direct modeling by default, feature list available on request without changing the UI (Shapr3D model) |
| ⬜ Todo | **Multi-face simultaneous drag:** select multiple faces and push/pull together (Plasticity/Shapr3D) |

**Tier 3 — defer:**

| Status | Task |
|--------|------|
| 🔮 Future | **Boolean debugger:** after a Fuse/Cut/Common, colour-code output faces by which input solid they came from. API: `shape.mapShapes()` (`TopoShapePy.cpp:1094-1126`). Solves a real frustration when booleans produce unexpected topology. |
| 🔮 Future | **Interactive offset previewer:** live slider for `shape.makeOffsetShape()` and `shape.makeOffset2D()` with real-time 3D preview. Useful for machining and mold design. Current Part Offset dialogs have no preview. |
| 🔮 Future | **Optimal bounding box visualiser:** display the minimum-volume oriented bounding box via `shape.optimalBoundingBox()` (`TopoShapePy.cpp:1227`). Useful for nesting, shipping volume, and stock material sizing. |
| 🔮 Future | Live cross-section view (SolidWorks "Live Section") — superseded by Section View above for inspection; revisit only if 2D annotation on section is needed |
| 🔮 Future | History-free NURBS direct modeling (Plasticity model) — wrong paradigm for mechanical CAD |

> The proprietary layer follows the same pattern as CoreRibbon: private repo, copied into the
> build tree by CMake, never modifies upstream files. LGPL2+ compliance: new code that *uses*
> the FreeCAD API as a separate module can be proprietary; only modifications to existing
> LGPL2+ files must remain open.

---

### 8. Sketcher UX

| Status | Task |
|--------|------|
| ⬜ Todo | Simplify `Sketcher_ToggleDrivingConstraint` — see note below |

> **`Sketcher_ToggleDrivingConstraint` does two unrelated things:**
>
> - **No selection** — changes a global `constraintCreationMode` flag; all dimension
>   toolbar icons swap between driving and driven variants as a side effect.
> - **With selection** — one-shot toggle of the selected constraint; ignores the flag.
>
> The global mode shift is unexpected. Users clicking the button to convert one constraint
> end up in a persistent "reference dimension mode" with no obvious indication.
>
> **Proposed fix:** remove the global mode flag entirely. Dimensions are always created as
> driving; the toggle button acts only on selected constraints. This eliminates hidden state
> and the icon-swap side effect across the toolbar. A second T1 icon
> (`Sketcher_ToggleConstraint_Driven`) will also be needed once the command is split — flag
> this to the icon team when the time comes.
>
> **Files to change:** `CommandConstraints.cpp` — `CmdSketcherToggleDrivingConstraint::activated()`
> and `updateAction()`, plus all `addCommandMode("ToggleDrivingConstraint", ...)` registrations.

---

## Research Areas

Unsolved problems across the CAD industry worth investigating — not committed goals,
but worth revisiting as CorePart matures.

- **Constraint impact visualisation:** when dragging a face, show in real time which existing
  dimensions and constraints are affected. No current tool does this.
- **Fine-grained undo in direct edit mode:** every tool treats an entire drag as a single undo
  step. Finer granularity (pause mid-drag = new undo point) would be a meaningful improvement.
- **Face origin transparency:** selecting a face should clearly indicate which feature created it.
  Only Onshape surfaces this well; others leave users guessing in complex models.
- **Sketch reuse:** using one sketch profile as input to multiple features is awkward in every
  tool. A cleaner referencing model could reduce duplication in parametric models.
- **Taper/draft on drag:** adding a draft angle during face manipulation without opening a dialog
  (Fusion shows a secondary input handle; SolidWorks requires the feature dialog).
- **Tolerance inspector:** `shape.getTolerance()`, `overTolerance()`, `inTolerance()` are all
  Python-accessible (`TopoShapePy.cpp:1142-1220`) but have no UI. An inspector that identifies
  faces/edges with bad tolerances could prevent downstream assembly issues.
- **Mesh/tessellation inspector:** `shape.tessellate()`, `getPoints()`, `getFaces()` expose the
  render mesh directly. A visualiser showing triangle quality, degenerate faces, and edge length
  distribution could help diagnose rendering and FEM mesh problems.
- **Curvature/surface quality heatmap:** `shape.reflectLines()` highlights curvature
  discontinuities. A proper heatmap overlay (G0/G1/G2 continuity colouring) would aid surface
  quality review — common in industrial design but absent from FreeCAD entirely.
- **Document health dashboard:** document-wide analysis of orphaned objects, broken links, and
  recompute errors — beyond the per-object property panel that exists today.
- **Camera animation / presentation studio:** `view.startAnimating()`, `setCamera()`,
  `setCameraOrientation()` are all Python-accessible (`View3DPy.cpp:80-171`). A keyframe
  animator with orbit presets and video export could be valuable for product visualisation.

---

## Long-Term Architecture: Renderer Independence

CoreCAD currently inherits FreeCAD's Coin3D renderer (an OpenInventor-derived scene graph from
the early 1990s). Coin3D works well for today's needs but has a visible ceiling: no
physically-based rendering, no GPU instancing for large assemblies, no modern post-processing
(SSAO, proper MSAA), and OpenGL is deprecated on macOS.

**The likely future:** Vulkan is becoming the low-level GPU standard on Linux/Android; Metal
is the equivalent on macOS/iOS; DX12 on Windows. The most promising cross-platform abstraction
over all three is **wgpu** (the WebGPU implementation in Rust, with Python bindings via
`wgpu-py`). WebGPU is being standardised by browser vendors and will not be abandoned. If the
industry converges on a single modern GPU API, wgpu is the most likely vehicle.

Qt3D is the lower-risk alternative — already a Qt dependency, familiar paradigm, prior
FreeCAD community discussion — but it is less future-proof than wgpu/Vulkan long-term.

**The abstraction principle (apply now, pay off later):**

The proprietary UX layer must never call Coin3D APIs directly. All renderer interaction should
go through FreeCAD's existing Python view API (`view.toggleClippingPlane()`, `ViewProvider`
methods, etc.). Where the Python API doesn't expose something, write a thin named adapter
rather than reaching into Coin3D. This creates a clean seam:

```
CoreCAD proprietary UX
          ↓
   RenderInterface         ← thin named adapters ("highlight face", "place clipping plane",
          ↓                   "draw overlay annotation") — no Coin3D types leak through
Coin3D today / wgpu tomorrow
```

**Performance note:** This abstraction adds no appreciable overhead. The abstraction layer
sits *above* the render loop — it makes setup calls ("highlight this face") at interaction
frequency, not per-frame. The actual render loop (tessellation, GPU draw calls) runs
entirely in C++ at full speed regardless. Python dispatch overhead is microseconds; CAD
geometry recompute is milliseconds to seconds. The bottleneck is always OCCT, never the
render call setup.

**Immediate actions (low cost, high future value):**
- When building proprietary features that touch the 3D view, define what the renderer
  needs to do in terms of *operations* (highlight, annotate, clip) not Coin3D primitives
- Document which proprietary features touch the renderer and through which adapter
- When FreeCAD's Python view API is insufficient, add the missing call to the open-source
  layer (a thin C++ exposure) rather than bypassing it

**Watch list:** `wgpu-py`, Qt3D activity in upstream FreeCAD, any OCCT move toward Vulkan
output (OCCT 7.x roadmap has `TKOpenGl` being supplemented with a Vulkan backend).

---

## Branch & Workflow Notes

- `main` — stable CoreCAD fork. Merged from `develop` when stable. Rebased onto `upstream/main` when pulling in upstream changes.
- `develop` — active CoreCAD development. Rebased onto `main` after each upstream sync. Merged into `main` when stable.
- Feature branches (e.g. `CCI-6`) — individual work items, branched from `develop`.
- Upstream sync: `git fetch upstream` → `git rebase upstream/main` on `main` → rebase `develop` onto `main`.
