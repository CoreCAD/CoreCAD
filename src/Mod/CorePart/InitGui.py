# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

# CorePart workbench — combines Part + PartDesign under a single "Part" tab.
# Pure-Python wrapper: no C++ changes, imports both PartGui and PartDesignGui,
# wires up their commands into combined toolbars and menus.

import FreeCAD as App
import FreeCADGui as Gui


class CorePartWorkbench(Gui.Workbench):
    """Combined Part + Part Design workbench for CoreCAD."""

    def __init__(self):
        self.__class__.Icon = (
            App.getResourceDir()
            + "Mod/PartDesign/Resources/icons/PartDesignWorkbench.svg"
        )
        self.__class__.MenuText = "Part"
        self.__class__.ToolTip = "Part workbench"

    def GetClassName(self):
        return "Gui::PythonWorkbench"

    def Initialize(self):
        # ---- PartDesign modules ----
        import PartDesignGui  # noqa: F401 — registers C++ commands
        import PartDesign  # noqa: F401

        # WizardShaft (optional, numpy-dependent)
        try:
            import traceback
            from PartDesign.WizardShaft import WizardShaft  # noqa: F401
        except RuntimeError:
            import traceback as _tb

            print("{}".format(_tb.format_exc()))
        except ImportError:
            try:
                from FeatureHole import HoleGui  # noqa: F401
            except Exception:
                pass

        # Python-only PartDesign commands
        from PartDesign.InvoluteGearFeature import CommandInvoluteGear

        Gui.addCommand("PartDesign_InvoluteGear", CommandInvoluteGear())

        from PartDesign.SprocketFeature import CommandSprocket

        Gui.addCommand("PartDesign_Sprocket", CommandSprocket())

        # ---- Part modules ----
        import PartGui  # noqa: F401 — registers C++ commands

        try:
            import BasicShapes.CommandShapes  # noqa: F401
        except ImportError as err:
            App.Console.PrintError(
                "'BasicShapes' package cannot be loaded. {err}\n".format(err=str(err))
            )

        try:
            import CompoundTools._CommandCompoundFilter  # noqa: F401
            import CompoundTools._CommandExplodeCompound  # noqa: F401
        except ImportError as err:
            App.Console.PrintError(
                "'CompoundTools' package cannot be loaded. {err}\n".format(err=str(err))
            )

        try:
            bop = __import__("BOPTools")
            bop.importAll()
            bop.addCommands()
            PartGui.BOPTools = bop
        except Exception as err:
            App.Console.PrintError(
                "'BOPTools' package cannot be loaded. {err}\n".format(err=str(err))
            )

        # ---- PartDesign toolbars (shown first in ribbon) ----
        self.appendToolbar(
            "Part Design Helper Features",
            [
                "PartDesign_Body",
                "PartDesign_CompSketches",
                "Sketcher_ValidateSketch",
                "Part_CheckGeometry",
                "PartDesign_SubShapeBinder",
                "PartDesign_Clone",
            ],
        )
        self.appendToolbar(
            "Part Design Modeling Features",
            [
                "PartDesign_Pad",
                "PartDesign_Revolution",
                "PartDesign_AdditiveLoft",
                "PartDesign_AdditivePipe",
                "PartDesign_AdditiveHelix",
                "PartDesign_CompPrimitiveAdditive",
                "Separator",
                "PartDesign_Pocket",
                "PartDesign_Hole",
                "PartDesign_Groove",
                "PartDesign_SubtractiveLoft",
                "PartDesign_SubtractivePipe",
                "PartDesign_SubtractiveHelix",
                "PartDesign_CompPrimitiveSubtractive",
                "Separator",
                "PartDesign_Boolean",
            ],
        )
        self.appendToolbar(
            "Part Design Dress-Up Features",
            [
                "PartDesign_Fillet",
                "PartDesign_Chamfer",
                "PartDesign_Draft",
                "PartDesign_Thickness",
            ],
        )
        self.appendToolbar(
            "Part Design Transformation Features",
            [
                "PartDesign_Mirrored",
                "PartDesign_LinearPattern",
                "PartDesign_PolarPattern",
                "PartDesign_MultiTransform",
            ],
        )

        # ---- Part toolbars ----
        self.appendToolbar(
            "Solids",
            [
                "Part_Box",
                "Part_Cylinder",
                "Part_Sphere",
                "Part_Cone",
                "Part_Torus",
                "Part_Tube",
                "Part_Primitives",
                "Part_Builder",
            ],
        )
        self.appendToolbar(
            "Part tools",
            [
                "Part_Extrude",
                "Part_Revolve",
                "Part_Mirror",
                "Part_Scale",
                "Part_Fillet",
                "Part_Chamfer",
                "Part_MakeFace",
                "Part_RuledSurface",
                "Part_Loft",
                "Part_Sweep",
                "Part_Section",
                "Part_CrossSections",
                "Part_CompOffset",
                "Part_Thickness",
                "Part_ProjectionOnSurface",
                "Part_ColorPerFace",
            ],
        )
        self.appendToolbar(
            "Boolean",
            [
                "Part_CompCompoundTools",
                "Part_Boolean",
                "Part_Cut",
                "Part_Fuse",
                "Part_Common",
                "Part_CompJoinFeatures",
                "Part_CompSplitFeatures",
                "Part_CheckGeometry",
                "Part_Defeaturing",
            ],
        )

        # ---- Menus ----
        self.appendMenu(
            ["&Sketch"],
            [
                "PartDesign_NewSketch",
                "Sketcher_EditSketch",
                "Sketcher_MapSketch",
                "Sketcher_ReorientSketch",
                "Sketcher_ValidateSketch",
                "Sketcher_MergeSketches",
                "Sketcher_MirrorSketch",
            ],
        )

        self.appendMenu(
            ["&Part Design"],
            [
                "PartDesign_Body",
                "Separator",
                "PartDesign_ShapeBinder",
                "PartDesign_SubShapeBinder",
                "PartDesign_Clone",
                "Separator",
            ],
        )
        self.appendMenu(
            ["&Part Design", "Additive Features"],
            [
                "PartDesign_Pad",
                "PartDesign_Revolution",
                "PartDesign_AdditiveLoft",
                "PartDesign_AdditivePipe",
                "PartDesign_AdditiveHelix",
            ],
        )
        self.appendMenu(
            ["&Part Design"],
            ["PartDesign_CompPrimitiveAdditive", "Separator"],
        )
        self.appendMenu(
            ["&Part Design", "Subtractive Features"],
            [
                "PartDesign_Pocket",
                "PartDesign_Hole",
                "PartDesign_Groove",
                "PartDesign_SubtractiveLoft",
                "PartDesign_SubtractivePipe",
                "PartDesign_SubtractiveHelix",
            ],
        )
        self.appendMenu(
            ["&Part Design"],
            ["PartDesign_CompPrimitiveSubtractive", "Separator"],
        )
        self.appendMenu(
            ["&Part Design", "Dress-Up Features"],
            [
                "PartDesign_Fillet",
                "PartDesign_Chamfer",
                "PartDesign_Draft",
                "PartDesign_Thickness",
            ],
        )
        self.appendMenu(
            ["&Part Design", "Transformation Features"],
            [
                "PartDesign_Mirrored",
                "PartDesign_LinearPattern",
                "PartDesign_PolarPattern",
                "PartDesign_MultiTransform",
            ],
        )
        self.appendMenu(
            ["&Part Design"],
            [
                "Separator",
                "PartDesign_Boolean",
                "Separator",
                "Materials_InspectAppearance",
                "Materials_InspectMaterial",
                "Separator",
                "Part_CheckGeometry",
                "Separator",
                "PartDesign_InvoluteGear",
                "PartDesign_Sprocket",
            ],
        )

        self.appendMenu(
            ["&Part"],
            [
                "Part_BoxSelection",
                "Separator",
                "Part_Box",
                "Part_Cylinder",
                "Part_Sphere",
                "Part_Cone",
                "Part_Torus",
                "Part_Tube",
                "Part_Primitives",
                "Part_Builder",
                "Separator",
                "Part_ShapeFromMesh",
                "Part_PointsFromMesh",
                "Part_MakeSolid",
                "Part_ReverseShape",
                "Separator",
                "Part_SimpleCopy",
                "Part_TransformedCopy",
                "Part_ElementCopy",
                "Part_RefineShape",
                "Separator",
                "Part_Boolean",
                "Part_Cut",
                "Part_Fuse",
                "Part_Common",
                "Separator",
                "Part_JoinConnect",
                "Part_JoinEmbed",
                "Part_JoinCutout",
                "Separator",
                "Part_BooleanFragments",
                "Part_SliceApart",
                "Part_Slice",
                "Part_XOR",
                "Separator",
                "Part_Compound",
                "Part_ExplodeCompound",
                "Part_CompoundFilter",
                "Part_ToleranceSet",
                "Separator",
                "Part_Extrude",
                "Part_Revolve",
                "Part_Mirror",
                "Part_Scale",
                "Part_Fillet",
                "Part_Chamfer",
                "Part_MakeFace",
                "Part_RuledSurface",
                "Part_Loft",
                "Part_Sweep",
                "Part_Section",
                "Part_CrossSections",
                "Part_Offset",
                "Part_Offset2D",
                "Part_Thickness",
                "Part_ProjectionOnSurface",
                "Part_SectionCut",
                "Separator",
                "Part_EditAttachment",
                "Separator",
                "Part_CheckGeometry",
                "Part_Defeaturing",
                "Materials_InspectAppearance",
                "Materials_InspectMaterial",
            ],
        )

    def Activated(self):
        # Trigger PartDesign initialization (WorkflowManager etc.)
        try:
            Gui.doCommand("import PartDesignGui")
        except Exception:
            pass

        self._setup_task_watchers()

        # Show task panel if the user prefers it
        try:
            pref = (
                App.GetApplication()
                .GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Mod/PartDesign"
                )
                .GetBool("SwitchToTask", True)
            )
            if pref:
                Gui.Control.showTaskView()
        except Exception:
            pass

    def Deactivated(self):
        Gui.Control.clearTaskWatcher()
        # Mirror C++ Workbench::deactivated() which does "import PartDesignGui"
        # to reset the active Body context.
        try:
            Gui.doCommand("import PartDesignGui")
        except Exception:
            pass

    def ContextMenu(self, recipient):
        try:
            self._setup_context_menu(recipient)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _setup_context_menu(self, recipient):
        selection = Gui.Selection.getSelection()
        if not selection:
            return

        feature = selection[0]
        doc = App.ActiveDocument
        if not doc:
            return

        # Find the PartDesign Body (if any) that contains the first selected object.
        body = None
        bodies = [o for o in doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        for b in bodies:
            try:
                if feature in b.Group:
                    body = b
                    break
            except Exception:
                pass

        # MoveTip: single feature that lives inside a body
        if len(selection) == 1 and body is not None:
            is_pd_feature = feature.isDerivedFrom("PartDesign::Feature")
            is_base_feature = feature.isDerivedFrom("Part::Feature") and (
                hasattr(body, "BaseFeature") and body.BaseFeature == feature
            )
            if is_pd_feature or is_base_feature:
                self.appendContextMenu("", ["PartDesign_MoveTip"])

        if recipient == "Tree":
            if feature.isDerivedFrom("PartDesign::Body"):
                self.appendContextMenu("", ["Std_ToggleFreeze"])

            if bodies:
                add_move_feature = True
                add_move_in_tree = body is not None
                for sel in selection:
                    if add_move_feature and not sel.isDerivedFrom("Part::Feature"):
                        add_move_feature = False
                    if add_move_in_tree and body is not None:
                        try:
                            if sel not in body.Group:
                                add_move_in_tree = False
                        except Exception:
                            add_move_in_tree = False
                    if not add_move_in_tree and not add_move_feature:
                        break

                if add_move_feature:
                    self.appendContextMenu("", ["PartDesign_MoveFeature"])
                if add_move_in_tree:
                    self.appendContextMenu("", ["PartDesign_MoveFeatureInTree"])

            # MultiTransform: exactly one Transformed (not MultiTransform) selected
            transformed_count = sum(
                1
                for s in selection
                if (
                    s.isDerivedFrom("PartDesign::Transformed")
                    and not s.isDerivedFrom("PartDesign::MultiTransform")
                )
            )
            if transformed_count == 1:
                self.appendContextMenu("", ["PartDesign_MultiTransform"])

    def _setup_task_watchers(self):
        """Register PartDesign TaskWatchers equivalent to C++ Workbench::activated()."""

        class FilterWatcher:
            """Watcher whose visibility is controlled by a SelectionFilter string."""

            def __init__(self, filter_str, commands, title, icon=""):
                self.filter = filter_str
                self.commands = list(commands)
                self.title = title
                if icon:
                    self.icon = icon

        class EmptySelectionWatcher:
            """Watcher shown only when nothing is selected ('Start Part')."""

            def __init__(self, commands, title, icon=""):
                self.commands = list(commands)
                self.title = title
                if icon:
                    self.icon = icon

            def shouldShow(self):
                return len(Gui.Selection.getSelection()) == 0

        watchers = [
            # Vertex tools (datum — legacy Part::DatumPoint/Line/Plane)
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Vertex COUNT 1..",
                [
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Vertex Tools",
                "PartDesign_Body",
            ),
            # Vertex tools (PartDesign datum objects)
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Vertex COUNT 1..",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Edge tools
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Edge COUNT 1..",
                [
                    "PartDesign_Fillet",
                    "PartDesign_Chamfer",
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Edge Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Edge COUNT 1..",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Single-face tools
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Face COUNT 1",
                [
                    "PartDesign_NewSketch",
                    "PartDesign_Fillet",
                    "PartDesign_Chamfer",
                    "PartDesign_Draft",
                    "PartDesign_Thickness",
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Face Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Face COUNT 1",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Body selected
            FilterWatcher(
                "SELECT PartDesign::Body COUNT 1",
                ["PartDesign_NewSketch"],
                "Helper Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT PartDesign::Body COUNT 1..",
                ["PartDesign_Boolean"],
                "Boolean Tools",
                "PartDesign_Body",
            ),
            # Plane selected (App::Plane — origin planes)
            FilterWatcher(
                "SELECT App::Plane COUNT 1",
                [
                    "PartDesign_NewSketch",
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Helper Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT App::Plane COUNT 1",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Plane selected (PartDesign::Plane — datum plane)
            FilterWatcher(
                "SELECT PartDesign::Plane COUNT 1",
                [
                    "PartDesign_NewSketch",
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Helper Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT PartDesign::Plane COUNT 1",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Line selected (PartDesign::Line)
            FilterWatcher(
                "SELECT PartDesign::Line COUNT 1",
                ["Part_DatumPoint", "Part_DatumLine", "Part_DatumPlane"],
                "Helper Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT PartDesign::Line COUNT 1",
                ["PartDesign_Point", "PartDesign_Line", "PartDesign_Plane"],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # Point selected (PartDesign::Point)
            FilterWatcher(
                "SELECT PartDesign::Point COUNT 1",
                [
                    "Part_DatumPoint",
                    "Part_DatumLine",
                    "Part_DatumPlane",
                    "Part_CoordinateSystem",
                ],
                "Helper Tools",
                "PartDesign_Body",
            ),
            FilterWatcher(
                "SELECT PartDesign::Point COUNT 1",
                [
                    "PartDesign_Point",
                    "PartDesign_Line",
                    "PartDesign_Plane",
                    "PartDesign_CoordinateSystem",
                ],
                "Datum objects",
                "PartDesign_CoordinateSystem",
            ),
            # No selection — prompt to create a Body
            EmptySelectionWatcher(
                ["PartDesign_Body"],
                "Start Part",
                "Part_Box_Parametric",
            ),
            # Multiple faces
            FilterWatcher(
                "SELECT Part::Feature SUBELEMENT Face COUNT 2..",
                [
                    "PartDesign_Fillet",
                    "PartDesign_Chamfer",
                    "PartDesign_Draft",
                    "PartDesign_Thickness",
                ],
                "Face Tools",
                "PartDesign_Body",
            ),
            # Sketch selected
            FilterWatcher(
                "SELECT Sketcher::SketchObject COUNT 1",
                [
                    "PartDesign_NewSketch",
                    "PartDesign_Pad",
                    "PartDesign_Pocket",
                    "PartDesign_Hole",
                    "PartDesign_Revolution",
                    "PartDesign_Groove",
                    "PartDesign_AdditiveLoft",
                    "PartDesign_SubtractiveLoft",
                    "PartDesign_AdditivePipe",
                    "PartDesign_SubtractivePipe",
                    "PartDesign_AdditiveHelix",
                    "PartDesign_SubtractiveHelix",
                ],
                "Modeling Tools",
                "PartDesign_Body",
            ),
            # Multiple sketches
            FilterWatcher(
                "SELECT Sketcher::SketchObject COUNT 2..",
                [
                    "PartDesign_AdditiveLoft",
                    "PartDesign_SubtractiveLoft",
                    "PartDesign_AdditivePipe",
                    "PartDesign_SubtractivePipe",
                ],
                "Modeling tools",
                "PartDesign_Body",
            ),
            # ShapeBinder selected
            FilterWatcher(
                "SELECT PartDesign::ShapeBinder COUNT 1",
                [
                    "PartDesign_Pad",
                    "PartDesign_Pocket",
                    "PartDesign_Revolution",
                    "PartDesign_Groove",
                    "PartDesign_AdditiveLoft",
                    "PartDesign_SubtractiveLoft",
                    "PartDesign_AdditivePipe",
                    "PartDesign_SubtractivePipe",
                ],
                "Modeling tools",
                "PartDesign_Body",
            ),
            # SubShapeBinder selected
            FilterWatcher(
                "SELECT PartDesign::SubShapeBinder COUNT 1",
                [
                    "PartDesign_Pad",
                    "PartDesign_Pocket",
                    "PartDesign_Revolution",
                    "PartDesign_Groove",
                    "PartDesign_AdditiveLoft",
                    "PartDesign_SubtractiveLoft",
                    "PartDesign_AdditivePipe",
                    "PartDesign_SubtractivePipe",
                ],
                "Modeling tools",
                "PartDesign_Body",
            ),
            # SketchBased feature selected → suggest transformation
            FilterWatcher(
                "SELECT PartDesign::SketchBased",
                [
                    "PartDesign_Mirrored",
                    "PartDesign_LinearPattern",
                    "PartDesign_PolarPattern",
                    "PartDesign_MultiTransform",
                ],
                "Transformation Tools",
                "PartDesign_MultiTransform",
            ),
        ]

        Gui.Control.addTaskWatcher(watchers)


Gui.addWorkbench(CorePartWorkbench())
