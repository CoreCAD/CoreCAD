// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <QPointer>
#include <QWidget>

#include <fastsignals/signal.h>

#include <App/Material.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/PartGlobal.h>

class QDockWidget;
class QGroupBox;
class QLabel;
class QPushButton;

namespace App
{
class DocumentObject;
}

namespace Materials
{
class Material;
}

namespace MatGui
{
class MaterialTreeWidget;
}

namespace PartGui
{

/// What the appearance panel edits, read from the selection: whole parts, and faces grouped by
/// the object that shows them. A selected feature stands for the part built from it.
struct AppearanceTargets
{
    std::vector<App::DocumentObject*> parts;
    std::map<App::DocumentObject*, std::set<int>> faces;  // zero-based face indices

    bool empty() const
    {
        return parts.empty() && faces.empty();
    }
};

PartGuiExport AppearanceTargets appearanceTargetsFromSelection();

/// The part an object belongs to: itself when it can be made of something, otherwise the part
/// built from it. Membership is derived, so the walk is over dependants.
PartGuiExport App::DocumentObject* partOf(App::DocumentObject* obj);

/// The look an object has when no one has chosen one: its part's material's look, or the
/// standard look when the part is made of nothing.
PartGuiExport App::Material defaultLookOf(const App::DocumentObject* obj);

PartGuiExport void applyLook(const AppearanceTargets& targets, const App::Material& look);
PartGuiExport void resetLook(const AppearanceTargets& targets);
/// Sets what each part is made of. Faces nobody coloured by hand follow the new material's look.
PartGuiExport void applyMaterial(
    const std::vector<App::DocumentObject*>& parts,
    const Materials::Material& material
);

/// The appearance and material editing panel (UI_GUIDE §2.5): docked on the right, follows the
/// selection, applies each change at once as one undo step.
class PartGuiExport AppearancePanel: public QWidget, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    static void showPanel();

private:
    explicit AppearancePanel(QWidget* parent = nullptr);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    void scheduleRefresh();
    void refresh();
    void onMaterialSelected(const std::shared_ptr<Materials::Material>& material);
    void onLookSelected(const std::shared_ptr<Materials::Material>& material);
    void onCustomize();
    void onReset();

    QLabel* subject;
    QLabel* emptyState;
    QGroupBox* madeOf;
    QGroupBox* looksLike;
    MatGui::MaterialTreeWidget* materials;
    MatGui::MaterialTreeWidget* looks;
    QPushButton* customize;
    QPushButton* reset;
    QDockWidget* dock = nullptr;
    AppearanceTargets targets;
    bool refreshPending = false;
    fastsignals::scoped_connection undone, redone, deleted, documentClosed;

    static QPointer<AppearancePanel> instance;
};

}  // namespace PartGui
