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
| ✅ Done | Update desktop integration files (`src/XDGData/`) |
| ✅ Done | Update Windows installer script (`package/WindowsInstaller/FreeCAD-installer.nsi`) |
| ✅ Done | Update stylesheet names (`src/Gui/Stylesheets/FreeCAD.qss`, preference packs) |
| ⬜ Todo | Customise CoreCAD default color scheme (preference pack `.cfg` files, set default theme at startup) |
| ✅ Done | Update GitHub repository files (issue templates, PR template, FUNDING.yml) |
| 🔮 Future | Add FreeCAD original authors to an Acknowledgements section in the About dialog |
| 🔮 Future | Investigate renaming `FreeCAD`/`FreeCADCmd` binaries to `CoreCAD`/`CoreCADCmd` |

> Internal Python module names (`FreeCADGuiInit.py`, etc.) are intentionally left unchanged
> — renaming them would break the module import system.
> See `.local/BRANDING_CHECKLIST.md` for file-level detail on all items above.
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
- `BUILD_POINTS` — point cloud tools (scan-to-CAD excluded for now)
- `BUILD_REVERSEENGINEERING` — depends on Points; excluded with it
- `BUILD_INSPECTION` — mesh/point cloud inspection; depends on Points, excluded with it
- `BUILD_PLOT` — legacy matplotlib plotter (maintained externally as addon)

**Already OFF upstream** (no action needed): `BUILD_CLOUD`, `BUILD_SANDBOX`

---

### 3. RibbonUI as Default Interface
Bundle the RibbonUI addon as the standard CoreCAD UI, replacing the default toolbar/menu system.

| Status | Task |
|--------|------|
| ✅ Done | Evaluate RibbonUI addon ([APEbbers/FreeCAD-Ribbon](https://github.com/APEbbers/FreeCAD-Ribbon)) |
| ✅ Done | Add as git submodule (`src/Mod/FreeCAD-Ribbon`, tracking `develop` branch) |
| ✅ Done | Auto-load at startup (automatic via `DirModScanner` — no code changes needed) |
| ✅ Done | Remove "FreeCAD X.Y.Z" version string from ribbon bar title (`FCBinding.py`) |
| ✅ Done | Re-sync submodule to build dir on build, not just cmake configure (`src/Mod/CMakeLists.txt`) |
| ✅ Done | Move Global ribbon panels (Tools, Views) to Quick Access Toolbar with separators |
| ✅ Done | Fix QAT regressions: restore separators/ordering and hide Individual views globally |
| ✅ Done | Define and enforce a canonical workbench order in the RibbonUI workbench list |
| ✅ Done | Investigate and fix QAT separators being stripped from `RibbonStructure.json` on restart |
| ✅ Done | Limit Structure group to PartDesign, Part, and Assembly only (update `CreateDefaultRibbonStructure.py` then patch live JSON) |
| ✅ Done | Stamp `RibbonStructure.json` from `CreateStructure.txt` on every build so the build dir never drifts from the committed template |
| ✅ Done | Remove unnecessary vertical separators from PartDesign Modelling and Spreadsheet ribbon groups |
| ✅ Done | Fix TechDraw Dimensions ribbon group rendering empty (invalid command key, missing button definitions, unreliable `_custom` toolbar mechanism) |
| ✅ Done | Split PartDesign Helpers into a "Part" panel (New Body + New Sketch) and a reduced "Helpers" panel moved just before Structure |
| ✅ Done | Fix `CreateStructure.txt` not triggering stamp rebuild (add `.txt` to CMake `DEPENDS` list) |
| ⬜ Ongoing | Test with each upstream rebase |

> **Workbench order & visibility:** Two layers control fresh-install defaults:
> 1. FreeCAD preferences (`DlgSettingsWorkbenchesImp.cpp`): `"Ordered"` default sets canonical order
>    (PartDesign → Sketcher → Part → Assembly → TechDraw → Surface → Mesh → Spreadsheet → Material);
>    `"Disabled"` default hides Draft, FEM, CAM (plus NoneWorkbench, Test, etc.).
> 2. RibbonUI (`CreateDefaultRibbonStructure.py`, `CreateStructure.txt`): `ignoredWorkbenches` list
>    hides the same workbenches from the ribbon tab bar.
>
> Draft, FEM, and CAM are still compiled — only hidden from the UI. Existing installs may need
> `user.cfg` updated (`Workbenches/Ordered`, `Workbenches/Disabled`) and stale `TabOrder` cleared
> from the FreeCAD-Ribbon preference group.
>
> `CreateStructure.txt` is the single source of truth for the ribbon layout. The build system
> stamps `RibbonStructure.json` and `RibbonStructure_default.json` from it on every build, so
> the mutable runtime JSON never persists between builds. For functional testing, reset
> `~/.FreeCAD/user.cfg` (FreeCAD preferences layer) — the ribbon JSON is handled by the build.

---

### 4. Build & Developer Infrastructure
Ensure the project is reproducible and easy to work with.

| Status | Task |
|--------|------|
| ✅ Done | Set up `cMake/CoreCAD_Options.cmake` as home for all build overrides |
| ✅ Done | Set up pre-commit hooks (clang-format, black) |
| ✅ Done | Create `CLAUDE.md` for AI-assisted development |
| ✅ Done | Set up branching strategy (`main` mirrors upstream, `develop` holds customisations) |
| ✅ Done | Display CoreCAD version in title bar, separate from internal FreeCAD build version |
| ⬜ Todo | Define CoreCAD versioning strategy (CI injection, About dialog) |

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

## Branch & Workflow Notes

- `main` — strictly mirrors upstream FreeCAD. No custom changes.
- `develop` — all CoreCAD customisations. Rebased onto `main` after each upstream sync.
- Feature branches (e.g. `CCI-6`) — individual work items, branched from `develop`.
- Upstream sync: `git fetch upstream` → merge `main` → rebase `develop`.
