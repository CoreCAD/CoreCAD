# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CoreCAD is an open-source mechanical CAD platform forked from FreeCAD. The project name in CMake and code remains `FreeCAD` (the upstream name); branding to `CoreCAD` is an ongoing effort tracked in `.local/BRANDING_CHECKLIST.md`. The internal executable names are `FreeCAD` (GUI) and `FreeCADCmd` (CLI).

## Build System

The recommended build method is **Pixi** (conda-based). CMake presets are also supported directly.

```bash
# Recommended: Pixi tasks
pixi run configure-debug   # Configure debug build (output: build/debug/)
pixi run build-debug       # Build debug
pixi run install-debug     # Install to build/debug/
pixi run freecad-debug     # Run the application (alias: pixi run freecad)
pixi run freecad-test      # Run for testing: clean-room launch, throwaway user.cfg
pixi run configure-release
pixi run build-release

# Direct CMake (alternative)
cmake --preset conda-linux-debug
cmake --build build/debug
cmake --install build/debug
```

Build output goes to `build/debug/` or `build/release/`. Binary at `build/debug/bin/FreeCAD`.
Key build config files: `pixi.toml`, `CMakePresets.json`, `CMakeLists.txt`.

For live GUI/MCP verification, prefer `pixi run freecad-test`. It launches the debug
build with a throwaway `--user-cfg` wiped each run, so every session starts from pristine
code defaults — a persisted preference in `user.cfg` can otherwise silently override a
source-code default (e.g. a feature flag changed in C++ appears to have no effect). The MCP
bridge is unaffected (its auto-start setting lives in a separate JSON under the app-data
dir, not `user.cfg`).

## Testing

Tests use **Google Test** (GTest + GMock). Test sources live in `tests/src/`.

```bash
# All tests via pixi
pixi run test

# All tests via ctest
ctest --test-dir build/debug

# Single test by name pattern
ctest --test-dir build/debug -R "TestName"

# Verbose output
ctest --test-dir build/debug -V

# Run a test binary directly
./build/debug/tests/App_tests_run
```

Test executables are named `<Module>_tests_run` (e.g., `Part_tests_run`, `Sketcher_tests_run`). Tests are conditionally built based on `BUILD_*` CMake flags.

## Code Style & Linting

Pre-commit hooks enforce style. Install them with `pre-commit install`.

```bash
# Run all checks
pre-commit run --all-files

# Specific hooks
pre-commit run clang-format --all-files
pre-commit run black --all-files
pre-commit run pylint --all-files
```

- **C++**: clang-format (LLVM style, 100-char limit, 4-space indent). Config: `.clang-format`
- **Python**: Black (100-char limit) + Pylint. Config: `.pylintrc`
- **Static analysis**: clang-tidy (errors fail CI, warnings do not). Config: `.clang-tidy`
- **Spelling**: codespell with ignore list in `.github/codespellignore`

## Architecture

### Directory Structure

```
src/Base/      - Math (Vector, Matrix, Rotation), serialization, console, Python bindings
src/App/       - Document model, object system, property engine, expression parser
src/Gui/       - Qt GUI, 3D viewer (Coin3D), workbenches, dialogs, property editor
src/Main/      - Entry points: FreeCAD (GUI), FreeCADCmd (CLI)
src/Mod/       - 36 feature modules/workbenches (Part, PartDesign, Sketcher, Assembly, etc.)
src/Ext/       - Extension framework, Python module wrappers
src/3rdParty/  - Vendored deps (zipios, OndselSolver, etc.)
tests/         - Google Test suite
cMake/         - Reusable CMake macros (FreeCAD_Helpers/)
```

### Core Class Hierarchy

The App/Gui split is the central architectural pattern:

- **`App::DocumentObject`** — Base for all objects in a document. Has a property system (dynamic typed attributes), dependency tracking, and recompute mechanism.
- **`App::Feature`** — Abstract base for computable features. Drives the DAG-based recompute model.
- **`Gui::ViewProvider`** — Bridge between App and Gui layers. Every DocumentObject has a corresponding ViewProvider for 3D/UI representation.
- **`Gui::Workbench`** — Context-specific UI (toolbars, menus, panels). Activated when switching workbenches.

### Module Structure

Each module in `src/Mod/` follows this pattern:
```
ModuleName/
├── App/         - Document model, properties, computation logic (C++)
├── Gui/         - ViewProviders, commands, dialogs (C++ + Qt)
├── Init.py      - Module registration (always loaded)
├── InitGui.py   - GUI registration (loaded only in GUI mode)
└── Resources/   - Icons (.svg), UI files (.ui), translations (.ts)
```

### Property System

Properties are typed attributes on DocumentObjects, defined in C++ with `ADD_PROPERTY` macros. They support expressions/formulas, serialization, and the recompute DAG. Access from Python via attribute syntax (`obj.Length`).

### Key Modules

| Module | Purpose |
|--------|---------|
| Part | CSG solid geometry, primitives, OCCT wrappers |
| PartDesign | Feature-based modeling (Pad, Pocket, Fillet, etc.) with Body container |
| Sketcher | 2D constraint-based sketching with solver |
| Assembly | Assembly modeling with OndselSolver for constraints |
| FEM | Finite element analysis (SMESH/NETGEN meshing, various solvers) |
| TechDraw | 2D technical drawing generation from 3D models |
| CAM | Computer-aided manufacturing / toolpath generation |
| Material | Material property database |
| Measure | Dimension/measurement tools |

### Python Integration

C++ modules expose Python APIs via pybind11, SWIG, and PyCXX. The main Python namespaces are `FreeCAD` (App layer) and `FreeCADGui` (Gui layer). Module workbenches are mixed C++/Python.

## CI/CD

GitHub Actions workflows in `.github/workflows/`:
- `sub_lint.yml` — Runs on all PRs: clang-format, black, pylint, codespell, clang-tidy
- `sub_buildPixi.yml` — Cross-platform Pixi/conda builds
- `sub_buildUbuntu.yml` — Linux build + GTest
- `sub_buildWindows.yml` — Windows build + GTest

PRs must pass all CI checks on Linux, macOS, and Windows.
