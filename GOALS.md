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

| Task | Status |
|------|--------|
| Replace README with CoreCAD content | ✅ Done |
| Add CoreCAD logo/SVG icon | ✅ Done |
| Create `corecad/branding.xml` (window title, app name, vendor) | ✅ Done |
| Add CoreCAD Windows installer icons | ✅ Done |
| Fix CLI/GUI startup banner in `src/Main/MainGui.cpp` | ✅ Done |
| Fix CLI startup banner in `src/Main/MainCmd.cpp` | ✅ Done |
| Update LicenseInfo/CreditsInfo strings in `src/Main/FreeCADGuiPy.cpp` | ✅ Done |
| Update desktop integration files (`src/XDGData/`) | ✅ Done |
| Update Windows installer script (`package/WindowsInstaller/FreeCAD-installer.nsi`) | ✅ Done |
| Update stylesheet names (`src/Gui/Stylesheets/FreeCAD.qss`, preference packs) | ✅ Done |
| Customise CoreCAD default color scheme (preference pack `.cfg` files, set default theme at startup) | ⬜ Todo |
| Update GitHub repository files (issue templates, PR template, FUNDING.yml) | ✅ Done |
| Add FreeCAD original authors to an Acknowledgements section in the About dialog | 🔮 Future |
| Investigate renaming `FreeCAD`/`FreeCADCmd` binaries to `CoreCAD`/`CoreCADCmd` | 🔮 Future |

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

| Task | Status |
|------|--------|
| Disable Addon Manager (`BUILD_ADDONMGR=OFF` in `CoreCAD_Options.cmake`) | ✅ Done |
| Remove Workbenches preferences tab (`src/Gui/resource.cpp`) | ✅ Done |
| Decide which upstream workbenches to exclude from CoreCAD | ✅ Done |

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

| Task | Status |
|------|--------|
| Evaluate RibbonUI addon ([APEbbers/FreeCAD-Ribbon](https://github.com/APEbbers/FreeCAD-Ribbon)) | ✅ Done |
| Add as git submodule (`src/Mod/FreeCAD-Ribbon`, tracking `develop` branch) | ✅ Done |
| Auto-load at startup (automatic via `DirModScanner` — no code changes needed) | ✅ Done |
| Remove "FreeCAD X.Y.Z" version string from ribbon bar title (`FCBinding.py`) | ✅ Done |
| Re-sync submodule to build dir on build, not just cmake configure (`src/Mod/CMakeLists.txt`) | ✅ Done |
| Define and enforce a canonical workbench order in the RibbonUI workbench list | ⬜ Todo |
| Test with each upstream rebase | ⬜ Ongoing |

> **Workbench order note:** The RibbonUI workbench list is currently unsorted. Because the Workbenches
> preferences tab has been removed (`src/Gui/resource.cpp`), users have no way to reorder it manually.
> The workbench order is a CoreCAD concern — RibbonUI simply reflects whatever order FreeCAD exposes.
> The canonical order must be defined and enforced by CoreCAD, likely via a startup hook or shipped
> user-config that writes the expected order to the FreeCAD preference store.

---

### 4. Build & Developer Infrastructure
Ensure the project is reproducible and easy to work with.

| Task | Status |
|------|--------|
| Set up `cMake/CoreCAD_Options.cmake` as home for all build overrides | ✅ Done |
| Set up pre-commit hooks (clang-format, black) | ✅ Done |
| Create `CLAUDE.md` for AI-assisted development | ✅ Done |
| Set up branching strategy (`main` mirrors upstream, `develop` holds customisations) | ✅ Done |
| Display CoreCAD version in title bar, separate from internal FreeCAD build version | ✅ Done |
| Define CoreCAD versioning strategy (CI injection, About dialog) | ⬜ Todo |

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
