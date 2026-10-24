// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Color.h>
#include <Gui/Application.h>
#include <Gui/HistoryPanel.h>
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
constexpr long removedTransparency = 60;

Part::Feature* addLayer(App::Document& doc, const char* name, const Part::TopoShape& shape)
{
    if (shape.isNull()) {
        return nullptr;
    }
    auto* layer = doc.addObject<Part::Feature>(name);
    layer->Shape.setValue(shape);
    return layer;
}

void colourLayer(Part::Feature* layer, const Base::Color& colour, long transparency)
{
    auto* view = layer ? dynamic_cast<Gui::ViewProviderGeometryObject*>(
                             Gui::Application::Instance->getViewProvider(layer)
                         )
                       : nullptr;
    if (view) {
        view->ShapeAppearance.setDiffuseColor(colour);
        view->Transparency.setValue(transparency);
    }
}

}  // namespace

App::Document* PartGui::versionLayers(App::Document& before, App::Document& after, bool showChanges)
{
    App::DocumentInitFlags flags;
    flags.temporary = true;
    flags.createView = false;
    auto& app = App::GetApplication();
    App::Document* doc = app.newDocument(app.getUniqueDocumentName("Version").c_str(), "", flags);

    if (showChanges) {
        const Part::ShapeComparison comparison
            = Part::compareShapes(Part::documentSolids(before), Part::documentSolids(after));
        addLayer(*doc, "Kept", comparison.shared);
        colourLayer(addLayer(*doc, "Added", comparison.added), addedColour, 0);
        colourLayer(addLayer(*doc, "Removed", comparison.removed), removedColour, removedTransparency);
    }
    else {
        addLayer(*doc, "Version", Part::documentSolids(after));
    }
    doc->purgeTouched();
    return doc;
}

void PartGui::registerVersionLayers()
{
    Gui::registerVersionLayers(&versionLayers);
}
