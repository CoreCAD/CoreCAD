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
| Create `bin/branding.xml` (window title, app name, vendor) | ✅ Done |
| Add CoreCAD Windows installer icons | ✅ Done |
| Fix CLI banner in `src/Main/MainGui.cpp` (still says "FreeCAD contributors") | ✅ Done |
| Update desktop integration files (`src/XDGData/`) | ⬜ Todo |
| Update Windows installer script (`package/WindowsInstaller/FreeCAD-installer.nsi`) | ⬜ Todo |
| Update stylesheet names (`src/Gui/Stylesheets/FreeCAD.qss`, preference packs) | ⬜ Todo |

> Internal Python module names (`FreeCADGuiInit.py`, etc.) are intentionally left unchanged
> — renaming them would break the module import system.

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
| Evaluate RibbonUI addon (HakanSeven12/FreeCAD_RibbonUI) | ⬜ Todo |
| Add as git submodule | ⬜ Todo |
| Auto-load at startup (modify `src/Gui/FreeCADGuiInit.py` or startup hook) | ⬜ Todo |
| Test with each upstream rebase | ⬜ Ongoing |

---

### 4. Build & Developer Infrastructure
Ensure the project is reproducible and easy to work with.

| Task | Status |
|------|--------|
| Set up `cMake/CoreCAD_Options.cmake` as home for all build overrides | ✅ Done |
| Set up pre-commit hooks (clang-format, black) | ✅ Done |
| Create `CLAUDE.md` for AI-assisted development | ✅ Done |
| Set up branching strategy (`main` mirrors upstream, `develop` holds customisations) | ✅ Done |
| Define CoreCAD versioning strategy | ⬜ Todo |

> **Versioning note:** CoreCAD currently tracks the upstream FreeCAD version number.
> Independent versioning was attempted but reverted — the FreeCAD version is deeply
> embedded in workbench compatibility checks, path versioning, and addon metadata.
> A future strategy should consider a display-only CoreCAD version (e.g. in branding.xml
> or About dialog) that is separate from the internal build version used for compatibility.

---

## Branch & Workflow Notes

- `main` — strictly mirrors upstream FreeCAD. No custom changes.
- `develop` — all CoreCAD customisations. Rebased onto `main` after each upstream sync.
- Feature branches (e.g. `CCI-6`) — individual work items, branched from `develop`.
- Upstream sync: `git fetch upstream` → merge `main` → rebase `develop`.
