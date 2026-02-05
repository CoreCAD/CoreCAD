# Branding checklist: Replace "FreeCAD" with your brand

Use this checklist to track branding changes. Mark items when reviewed/updated.

## Binaries & executable names
- [ ] `FreeCAD` (executable name; search for usages in scripts/CI)
- [ ] `FreeCADCmd` (CLI executable)

## Desktop entries / metadata
- [ ] [src/XDGData/org.freecad.FreeCAD.desktop](src/XDGData/org.freecad.FreeCAD.desktop)
- [ ] [src/XDGData/org.freecad.FreeCAD.metainfo.xml.in](src/XDGData/org.freecad.FreeCAD.metainfo.xml.in)
- [ ] [src/XDGData/org.freecad.FreeCAD.xml](src/XDGData/org.freecad.FreeCAD.xml)
- [ ] [src/XDGData/FreeCAD.thumbnailer.in](src/XDGData/FreeCAD.thumbnailer.in)

## Icons, installer assets, and images
- [ ] package/WindowsInstaller/icons/FreeCAD-icon.bmp
- [ ] package/WindowsInstaller/icons/FreeCAD-icon-57px-height.bmp
- [ ] package/WindowsInstaller/icons/FreeCAD.ico
- [ ] package/WindowsInstaller/icons/FreeCAD-icon.xcf
- [ ] package/WindowsInstaller/icons/FreeCAD-clean.ico
- [ ] package/WindowsInstaller/FreeCAD-installer.nsi
- [ ] src/Tools/embedded/Win32/FreeCAD_widget/FreeCAD_widget.ico
- [ ] package/rattler-build/osx/launcher/FreeCAD.cpp (launcher references)

## Source code and GUI
- [ ] src/App/FreeCADInit.py
- [ ] src/App/FreeCADTest.py
- [ ] src/Gui/FreeCADStyle.h
- [ ] src/Gui/FreeCADGuiInit.py
- [ ] src/Gui/FreeCADStyle.cpp
- [ ] src/Main/FreeCADGuiPy.cpp

## Stylesheets, themes, and fonts
- [ ] src/Gui/Stylesheets/FreeCAD.qss
- [ ] src/Gui/Stylesheets/parameters/FreeCAD Light.yaml
- [ ] src/Gui/Stylesheets/parameters/FreeCAD Dark.yaml
- [ ] src/Gui/PreferencePacks/FreeCAD Classic/FreeCAD Classic.cfg
- [ ] src/Gui/PreferencePacks/FreeCAD Dark/FreeCAD Dark.cfg
- [ ] src/Gui/PreferencePacks/FreeCAD Light/FreeCAD Light.cfg
- [ ] src/Mod/TechDraw/Gui/Resources/fonts/Y14.5-FreeCAD.ttf

## Documentation and developer resources
- [ ] src/Doc/FreeCAD.uml
- [ ] src/Doc/sphinx/FreeCAD.rst
- [ ] src/Doc/sphinx/FreeCADGui.rst
- [ ] README.md, CONTRIBUTING.md, other top-level docs (search for brand mentions)

## Localization / translation files
- [ ] src/Gui/Language/FreeCAD.ts
- [ ] src/Gui/Language/FreeCAD.po
- [ ] many `src/Gui/Language/FreeCAD_*.ts` files (translations)

## Build system, CMake, and config variables
- [ ] cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake
- [ ] cMake/FreeCAD_Helpers/FreeCADLibpackChecks.cmake
- [ ] CMake presets and build scripts referencing FREECAD_* variables
- [ ] corecad_manifest.yml (contains name `FreeCAD` and version keys)

## CI, workflows, and scripts
- [ ] .github/workflows/* (many files contain "FreeCAD" text and job names)
- [ ] .github/scripts/run_gui_tests.py (references `FreeCAD` and `FreeCADCmd`)
- [ ] .github/FUNDING.yml (project identifiers)

## Issue templates, PR templates, and community links
- [ ] .github/pull_request_template.md (text referencing FreeCAD)
- [ ] .github/ISSUE_TEMPLATE/* (multiple templates reference FreeCAD and community links)

## Other occurrences (patterns to search/replace)
- [ ] Filenames containing "FreeCAD" (use: `git ls-files '*FreeCAD*'`)
- [ ] Text occurrences in repository (use: `git grep -n "FreeCAD"`)
- [ ] Case-insensitive variants: `freecad` (used in CI variables and urls)

## Checklist instructions
- For each item above: review, update names/resources, and test the resulting build and packaging.
- After renaming binaries or desktop IDs, update CI, packaging manifests, and installer scripts.

If you want, I can (step-by-step):
- run a full repo search and produce a complete CSV of file/line occurrences
- prepare a branch with safe renames for non-binary assets (icons, docs, scripts)

Mark items you want me to act on and I'll proceed.
