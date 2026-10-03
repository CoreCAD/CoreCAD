// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Cruth contributors

#pragma once

#include <string>
#include <vector>

class QString;

namespace App
{
class Document;
class DocumentObject;
}  // namespace App
namespace Gui
{
class Command;
class CommandManager;
}  // namespace Gui
namespace Part
{
class Part2DObject;
class TopoShape;
}  // namespace Part
namespace PartDesign
{
class Body;
}

/// Helpers shared by the PartDesign feature commands.
namespace PartDesignGui::CommandSupport
{

bool hasAnyBody();
bool hasAnySketch();
std::string pythonNameList(const std::vector<std::string>& names);
std::vector<std::string> allEdgeNames(const Part::TopoShape& shape);
void copyAppearance(App::DocumentObject* to, App::DocumentObject* from);
PartDesign::Body* selectedBody(Gui::Command* cmd);
bool resolveBaseBodyForNewFeature(Gui::Command* cmd, PartDesign::Body*& body);
void finishFeature(
    const Gui::Command* cmd,
    App::DocumentObject* feature,
    App::DocumentObject* prevSolidFeature = nullptr,
    bool hidePrevSolid = true,
    bool updateDocument = true
);
App::DocumentObject* startFeature(Gui::Command* cmd, PartDesign::Body* body, const char* type);
void warnWrongSelection(const QString& text);

}  // namespace PartDesignGui::CommandSupport

void CreatePartDesignProfileBasedCommands(Gui::CommandManager& rcCmdMgr);
void CreatePartDesignDressUpCommands(Gui::CommandManager& rcCmdMgr);
void CreatePartDesignTransformedCommands(Gui::CommandManager& rcCmdMgr);
