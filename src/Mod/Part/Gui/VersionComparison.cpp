// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <map>
# include <string>

#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Color.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/HistoryPanel.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/ShapeComparison.h>

#include "VersionComparison.h"

using namespace PartGui;

namespace
{

// The create and remove colours of the icon palette (UI_GUIDE §2.2.1).
const Base::Color addedColour(0x0F / 255.0F, 0x6C / 255.0F, 0xBD / 255.0F);
const Base::Color removedColour(0xC5 / 255.0F, 0x0F / 255.0F, 0x1F / 255.0F);
const Base::Color sharedColour(0.6F, 0.6F, 0.6F);
constexpr long sharedTransparency = 75;

/// The compare tab already open for each title, by document name, so a second compare of the same
/// entry replaces it rather than piling up tabs.
std::map<std::string, std::string>& openComparisons()
{
    static std::map<std::string, std::string> open;
    return open;
}

void addLayer(
    App::Document& doc,
    const char* name,
    const Part::TopoShape& shape,
    const Base::Color& colour,
    long transparency
)
{
    if (shape.isNull()) {
        return;
    }
    auto* layer = doc.addObject<Part::Feature>(name);
    layer->Shape.setValue(shape);
    auto* view = dynamic_cast<Gui::ViewProviderGeometryObject*>(
        Gui::Application::Instance->getViewProvider(layer)
    );
    if (view) {
        view->ShapeAppearance.setDiffuseColor(colour);
        view->Transparency.setValue(transparency);
    }
}

std::string cameraOfTheModelView()
{
    if (auto* view = dynamic_cast<Gui::View3DInventor*>(Gui::getMainWindow()->activeWindow())) {
        return view->getCamera();
    }
    return {};
}

}  // namespace

App::Document* PartGui::showVersionComparison(
    App::Document& before,
    App::Document& after,
    const std::string& title
)
{
    const Part::ShapeComparison comparison
        = Part::compareShapes(Part::documentSolids(before), Part::documentSolids(after));
    const std::string camera = cameraOfTheModelView();

    auto& app = App::GetApplication();
    auto open = openComparisons().find(title);
    if (open != openComparisons().end()) {
        // The tab may have been closed and its name given to another comparison since.
        const App::Document* earlier = app.getDocument(open->second.c_str());
        if (earlier != nullptr && title == earlier->Label.getValue()) {
            app.closeDocument(open->second.c_str());
        }
        openComparisons().erase(open);
    }

    App::DocumentInitFlags flags;
    flags.temporary = true;
    flags.createView = true;
    App::Document* doc
        = app.newDocument(app.getUniqueDocumentName("Compare").c_str(), title.c_str(), flags);
    openComparisons()[title] = doc->getName();

    addLayer(*doc, "Shared", comparison.shared, sharedColour, sharedTransparency);
    addLayer(*doc, "Added", comparison.added, addedColour, 0);
    addLayer(*doc, "Removed", comparison.removed, removedColour, 0);
    doc->purgeTouched();

    Gui::Document* shown = Gui::Application::Instance->getDocument(doc);
    shown->setModified(false);
    if (auto* view = dynamic_cast<Gui::View3DInventor*>(shown->getActiveView())) {
        if (camera.empty() || !view->setCamera(camera.c_str())) {
            view->getViewer()->viewAll();
        }
    }
    // Creating a hidden document hands the active document back to the model, which brings the
    // model's tab forward; making the comparison the active document brings its tab forward.
    app.setActiveDocument(doc);

    if (comparison.added.isNull() && comparison.removed.isNull()) {
        Gui::getMainWindow()->showMessage(QObject::tr("No material was added or removed."));
    }
    return doc;
}

void PartGui::registerVersionComparison()
{
    Gui::registerVersionComparer(
        [](App::Document& before, App::Document& after, const std::string& title) {
            showVersionComparison(before, after, title);
        }
    );
}
