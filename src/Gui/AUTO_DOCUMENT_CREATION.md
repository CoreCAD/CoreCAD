# Auto Document Creation

## What it does

`Command::ensureActiveDocument()` (declared in `Command.h`, implemented in `Command.cpp`) ensures an
active document exists before a command runs. If no document is open it calls
`App::GetApplication().newDocument()` and returns `true`. If document creation somehow fails it
returns `false` and the caller should bail out early.

## Why it exists

New users expect to click "New Part" or "New Body" and just start working. Without this helper those
buttons are greyed out until the user first opens *File → New*. By auto-creating a document we make
that step invisible while keeping the underlying architecture unchanged.

## Commands that use it

| Command | File |
|---------|------|
| `Std_Part` | `src/Gui/CommandStructure.cpp` |
| `Std_Group` | `src/Gui/CommandStructure.cpp` |
| `PartDesign_Body` | `src/Mod/PartDesign/Gui/CommandBody.cpp` |
| `Assembly_CreateAssembly` | `src/Mod/Assembly/CommandCreateAssembly.py` |

## Criteria for adding more commands

A command is a good candidate if:

1. It creates a **top-level / root object** (not an operation on existing geometry).
2. It is a natural **entry point** — a user could reasonably want to use it as their first action.
3. Its `isActive()` currently returns `hasActiveDocument()` with no additional conditions.

Operations that require existing geometry (e.g. Pad, Pocket, Sketch) should **not** use this
helper — they genuinely need content to operate on.

## How to revert a command

1. Change `isActive()` back to `return hasActiveDocument();`.
2. Remove the `if (!ensureActiveDocument()) { return; }` guard from `activated()`.
3. For Python commands, restore `return App.ActiveDocument is not None` and remove the
   `if not App.ActiveDocument: App.newDocument()` line from `Activated()`.
