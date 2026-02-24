if(BUILD_ADDONMGR)
  if( NOT EXISTS "${CMAKE_SOURCE_DIR}/src/Mod/AddonManager/CMakeLists.txt" )
    message(FATAL_ERROR "The Addon Manager has been moved into a git submodule. Please run
    git submodule update --init" )
  endif()
  add_subdirectory(AddonManager)
endif(BUILD_ADDONMGR)

if(BUILD_BIM)
  add_subdirectory(BIM)
endif(BUILD_BIM)

if(BUILD_ASSEMBLY)
  add_subdirectory(Assembly)
endif(BUILD_ASSEMBLY)

if(BUILD_CLOUD)
    add_subdirectory(Cloud)
endif(BUILD_CLOUD)

if(BUILD_DRAFT)
  add_subdirectory(Draft)
endif(BUILD_DRAFT)

if(BUILD_FEM)
    add_subdirectory(Fem)
endif(BUILD_FEM)

if(BUILD_HELP)
  add_subdirectory(Help)
endif(BUILD_HELP)

if(BUILD_IDF)
  add_subdirectory(Idf)
endif(BUILD_IDF)

if(BUILD_IMPORT)
  add_subdirectory(Import)
endif(BUILD_IMPORT)

if(BUILD_INSPECTION)
  add_subdirectory(Inspection)
endif(BUILD_INSPECTION)

if(BUILD_JTREADER)
  add_subdirectory(JtReader)
endif(BUILD_JTREADER)

if(BUILD_MATERIAL)
  add_subdirectory(Material)
endif(BUILD_MATERIAL)

if(BUILD_MESH)
  add_subdirectory(Mesh)
endif(BUILD_MESH)

if(BUILD_MESH_PART)
  add_subdirectory(MeshPart)
endif(BUILD_MESH_PART)

if(BUILD_OPENSCAD)
  add_subdirectory(OpenSCAD)
endif(BUILD_OPENSCAD)

if(BUILD_PART)
  add_subdirectory(Part)
endif(BUILD_PART)

if(BUILD_PART_DESIGN)
  add_subdirectory(PartDesign)
endif(BUILD_PART_DESIGN)

if(BUILD_CAM)
    add_subdirectory(CAM)
endif(BUILD_CAM)

if(BUILD_PLOT)
  add_subdirectory(Plot)
endif(BUILD_PLOT)

if(BUILD_POINTS)
  add_subdirectory(Points)
endif(BUILD_POINTS)

if(BUILD_REVERSEENGINEERING)
  add_subdirectory(ReverseEngineering)
endif(BUILD_REVERSEENGINEERING)

if(BUILD_ROBOT)
    if(EIGEN3_FOUND)
      add_subdirectory(Robot)
    else(EIGEN3_FOUND)
      MESSAGE("Due to the missing Eigen3 library the Robot module won't be built")
    endif(EIGEN3_FOUND)
endif(BUILD_ROBOT)

if(BUILD_SANDBOX)
    add_subdirectory(Sandbox)
endif(BUILD_SANDBOX)

if(BUILD_SHOW)
    add_subdirectory(Show)
endif(BUILD_SHOW)

if(BUILD_SKETCHER)
    if(EIGEN3_FOUND)
      add_subdirectory(Sketcher)
    else(EIGEN3_FOUND)
      MESSAGE("Due to the missing Eigen3 library the Sketcher module won't be built")
    endif(EIGEN3_FOUND)
endif(BUILD_SKETCHER)

if(BUILD_SPREADSHEET)
  add_subdirectory(Spreadsheet)
endif(BUILD_SPREADSHEET)

if(BUILD_START)
    add_subdirectory(Start)
endif(BUILD_START)

if(BUILD_SURFACE)
  add_subdirectory(Surface)
endif(BUILD_SURFACE)

if(BUILD_MEASURE)
    add_subdirectory(Measure)
endif(BUILD_MEASURE)

if(BUILD_TECHDRAW)
    add_subdirectory(TechDraw)
endif(BUILD_TECHDRAW)

#add_subdirectory(TemplatePyMod)

if(BUILD_TEST)
  add_subdirectory(Test)
endif(BUILD_TEST)

if(BUILD_TUX)
    add_subdirectory(Tux)
endif(BUILD_TUX)

if(BUILD_WEB)
    add_subdirectory(Web)
endif(BUILD_WEB)

# FreeCAD-Ribbon: Pure-Python ribbon UI addon (git submodule, no CMakeLists.txt of its own)
if(BUILD_GUI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/InitGui.py")
    install(
        DIRECTORY FreeCAD-Ribbon/
        DESTINATION Mod/FreeCAD-Ribbon
        PATTERN ".git" EXCLUDE
        PATTERN ".github" EXCLUDE
        PATTERN ".gitignore" EXCLUDE
        PATTERN ".pre-commit*" EXCLUDE
    )
    # Copy to build tree so the module is available without installing.
    # Use add_custom_command so the copy re-runs on build when source files change,
    # not only at cmake configure time.
    file(GLOB_RECURSE _ribbon_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/*.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/*.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/*.qss"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/*.ui"
    )
    set(_ribbon_stamp "${CMAKE_BINARY_DIR}/Mod/FreeCAD-Ribbon/.copy_stamp")
    add_custom_command(
        OUTPUT "${_ribbon_stamp}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon"
            "${CMAKE_BINARY_DIR}/Mod/FreeCAD-Ribbon"
        # Stamp RibbonStructure.json from CreateStructure.txt on every build so
        # the build directory never drifts from the committed template. CoreCAD
        # locks the ribbon layout at build time; user-written JSON is unwanted.
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/CreateStructure.txt"
            "${CMAKE_BINARY_DIR}/Mod/FreeCAD-Ribbon/RibbonStructure.json"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/FreeCAD-Ribbon/CreateStructure.txt"
            "${CMAKE_BINARY_DIR}/Mod/FreeCAD-Ribbon/RibbonStructure_default.json"
        COMMAND ${CMAKE_COMMAND} -E touch "${_ribbon_stamp}"
        DEPENDS ${_ribbon_sources}
        COMMENT "Syncing FreeCAD-Ribbon to build directory"
    )
    add_custom_target(FreeCAD_Ribbon_sync ALL
        DEPENDS "${_ribbon_stamp}"
    )
endif()
