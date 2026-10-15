// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <ranges>
#include <string_view>

#include <QDockWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <TopAbs_ShapeEnum.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Dialogs/DlgMaterialPropertiesImp.h>
#include <Gui/DockWindowManager.h>
#include <Gui/MainWindow.h>
#include <Gui/OverlayManager.h>
#include <Gui/ViewProvider.h>
#include <Mod/Material/App/MaterialFilter.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/Materials.h>
#include <Mod/Material/App/ModelUuids.h>
#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Material/Gui/MaterialTreeWidget.h>
#include <Mod/Part/App/MaterialExtension.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Part/App/TopoShape.h>

#include "AppearancePanel.h"

using namespace PartGui;

QPointer<AppearancePanel> AppearancePanel::instance;

namespace
{

/// Compared by value only: an appearance keeps the id of the material it came from even after a
/// person recolours it, so the id says nothing about whether it was chosen.
bool sameLook(const App::Material& a, const App::Material& b)
{
    return a.diffuseColor == b.diffuseColor && a.ambientColor == b.ambientColor
        && a.specularColor == b.specularColor && a.emissiveColor == b.emissiveColor
        && a.shininess == b.shininess && a.transparency == b.transparency && a.image == b.image
        && a.imagePath == b.imagePath;
}

bool sameLooks(const std::vector<App::Material>& a, const std::vector<App::Material>& b)
{
    return std::ranges::equal(a, b, sameLook);
}

App::PropertyMaterialList* appearanceOf(const App::DocumentObject* obj)
{
    auto* view = Gui::Application::Instance->getViewProvider(obj);
    return view
        ? dynamic_cast<App::PropertyMaterialList*>(view->getPropertyByName("ShapeAppearance"))
        : nullptr;
}

/// A face whose look is still the one the part's previous material gave it, or the standard look,
/// was never chosen by a person and follows the new material. Any other face keeps its colour.
std::vector<App::Material> followMaterial(
    std::vector<App::Material> faces,
    const App::Material& oldLook,
    const App::Material& newLook
)
{
    const App::Material standard = App::Material::getDefaultAppearance();
    for (auto& face : faces) {
        if (sameLook(face, oldLook) || sameLook(face, standard)) {
            face = newLook;
        }
    }
    return faces;
}

/// One face look per face, so single faces can be set. A list with one entry colours every face.
std::vector<App::Material> lookPerFace(const App::DocumentObject* obj)
{
    std::vector<App::Material> looks = appearanceOf(obj)->getValues();
    const auto count = static_cast<std::size_t>(Part::getShape(obj).countSubShapes(TopAbs_FACE));
    if (looks.size() < count) {
        looks.resize(count, looks.empty() ? App::Material::getDefaultAppearance() : looks.front());
    }
    return looks;
}

int faceIndex(std::string_view element)
{
    constexpr std::string_view prefix = "Face";
    if (!element.starts_with(prefix)) {
        return -1;
    }
    const int number = std::atoi(std::string(element.substr(prefix.size())).c_str());
    return number > 0 ? number - 1 : -1;
}

/// The part's look is set on the part itself when it carries one; an object with no part above
/// it is painted directly.
App::DocumentObject* paintedObjectOf(App::DocumentObject* obj)
{
    App::DocumentObject* part = partOf(obj);
    return part && appearanceOf(part) ? part : obj;
}

App::Document* documentOf(const AppearanceTargets& targets)
{
    if (!targets.parts.empty()) {
        return targets.parts.front()->getDocument();
    }
    return targets.faces.empty() ? nullptr : targets.faces.begin()->first->getDocument();
}

/// Every change is one undo step.
template<typename Change>
void asOneStep(const AppearanceTargets& targets, const char* name, Change change)
{
    App::Document* doc = documentOf(targets);
    if (!doc) {
        return;
    }
    doc->openTransaction(name);
    change();
    doc->commitTransaction();
}

void paintLook(const AppearanceTargets& targets, const App::Material& look)
{
    for (auto* part : targets.parts) {
        appearanceOf(part)->setValue(look);
    }
    for (const auto& [obj, faces] : targets.faces) {
        std::vector<App::Material> looks = lookPerFace(obj);
        for (int face : faces) {
            if (face < static_cast<int>(looks.size())) {
                looks[face] = look;
            }
        }
        appearanceOf(obj)->setValues(looks);
    }
}

void paintDefaultLook(const AppearanceTargets& targets)
{
    for (auto* part : targets.parts) {
        appearanceOf(part)->setValue(defaultLookOf(part));
    }
    for (const auto& [obj, faces] : targets.faces) {
        const App::Material standard = defaultLookOf(obj);
        std::vector<App::Material> looks = lookPerFace(obj);
        for (int face : faces) {
            if (face < static_cast<int>(looks.size())) {
                looks[face] = standard;
            }
        }
        appearanceOf(obj)->setValues(looks);
    }
}

void paintMaterial(const std::vector<App::DocumentObject*>& parts, const Materials::Material& material)
{
    for (auto* part : parts) {
        auto* prop = Part::materialPropertyOf(part);
        if (!prop) {
            continue;
        }
        const App::Material oldLook = prop->getValue().getMaterialAppearance();
        const App::Material newLook = material.getMaterialAppearance();
        prop->setValue(material);

        // The part is painted, not whatever was selected. A Body copies its appearance onto its
        // features wholesale, so any shape the part is built from that the paint reached is
        // settled again from its own faces as they were before.
        std::vector<std::pair<App::PropertyMaterialList*, std::vector<App::Material>>> builtFrom;
        for (auto* source : part->getOutListRecursive()) {
            if (auto* appearance = appearanceOf(source)) {
                builtFrom.emplace_back(appearance, appearance->getValues());
            }
        }
        if (auto* appearance = appearanceOf(part)) {
            auto faces = followMaterial(appearance->getValues(), oldLook, newLook);
            if (!sameLooks(faces, appearance->getValues())) {
                appearance->setValues(faces);
            }
        }
        for (auto& [appearance, before] : builtFrom) {
            if (sameLooks(appearance->getValues(), before)) {
                continue;
            }
            auto faces = followMaterial(before, oldLook, newLook);
            if (!sameLooks(faces, appearance->getValues())) {
                appearance->setValues(faces);
            }
        }
    }
}

}  // namespace

App::DocumentObject* PartGui::partOf(App::DocumentObject* obj)
{
    std::set<App::DocumentObject*> seen;
    std::deque<App::DocumentObject*> queue;
    if (obj) {
        seen.insert(obj);
        queue.push_back(obj);
    }
    while (!queue.empty()) {
        App::DocumentObject* current = queue.front();
        queue.pop_front();
        if (Part::hasMaterial(current)) {
            return current;
        }
        for (App::DocumentObject* dependant : current->getInList()) {
            if (dependant && seen.insert(dependant).second) {
                queue.push_back(dependant);
            }
        }
    }
    return nullptr;
}

App::Material PartGui::defaultLookOf(const App::DocumentObject* obj)
{
    if (const Materials::Material* material = Part::materialOfPart(obj)) {
        return material->getMaterialAppearance();
    }
    return App::Material::getDefaultAppearance();
}

AppearanceTargets PartGui::appearanceTargetsFromSelection()
{
    AppearanceTargets targets;
    std::set<App::DocumentObject*> parts;
    auto selection = Gui::Selection().getSelectionEx();
    for (auto& selected : selection) {
        App::DocumentObject* obj = selected.getObject();
        if (!obj || !appearanceOf(obj)) {
            continue;
        }
        bool wholeObject = true;
        for (const auto& element : selected.getSubNames()) {
            if (element.empty()) {
                continue;
            }
            wholeObject = false;
            const int face = faceIndex(element);
            if (face >= 0 && Part::hasShape(obj)) {
                targets.faces[obj].insert(face);
            }
        }
        if (wholeObject) {
            App::DocumentObject* painted = paintedObjectOf(obj);
            if (parts.insert(painted).second) {
                targets.parts.push_back(painted);
            }
        }
    }
    return targets;
}

void PartGui::applyLook(const AppearanceTargets& targets, const App::Material& look)
{
    asOneStep(targets, QT_TRANSLATE_NOOP("Command", "Set appearance"), [&]() {
        paintLook(targets, look);
    });
}

void PartGui::resetLook(const AppearanceTargets& targets)
{
    asOneStep(targets, QT_TRANSLATE_NOOP("Command", "Reset appearance"), [&]() {
        paintDefaultLook(targets);
    });
}

void PartGui::applyMaterial(
    const std::vector<App::DocumentObject*>& parts,
    const Materials::Material& material
)
{
    AppearanceTargets targets;
    targets.parts = parts;
    asOneStep(targets, QT_TRANSLATE_NOOP("Command", "Set material"), [&]() {
        paintMaterial(parts, material);
    });
}

AppearancePanel::AppearancePanel(QWidget* parent)
    : QWidget(parent)
    , SelectionObserver(true)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);

    subject = new QLabel(content);
    subject->setWordWrap(true);
    layout->addWidget(subject);

    emptyState = new QLabel(tr("Select a part or faces to change how they look."), content);
    emptyState->setWordWrap(true);
    layout->addWidget(emptyState);

    madeOf = new QGroupBox(tr("Made of"), content);
    auto* madeOfLayout = new QVBoxLayout(madeOf);
    materials = new MatGui::MaterialTreeWidget(madeOf);
    Materials::MaterialFilter physical;
    physical.requirePhysical(true);
    materials->setFilter(physical);
    madeOfLayout->addWidget(materials);
    layout->addWidget(madeOf);

    looksLike = new QGroupBox(tr("Looks like"), content);
    auto* looksLayout = new QVBoxLayout(looksLike);
    looks = new MatGui::MaterialTreeWidget(looksLike);
    auto filters = std::make_shared<std::list<std::shared_ptr<Materials::MaterialFilter>>>();
    auto basic = std::make_shared<Materials::MaterialFilter>();
    basic->setName(tr("Basic appearance"));
    basic->addRequiredComplete(Materials::ModelUUIDs::ModelUUID_Rendering_Basic);
    filters->push_back(basic);
    auto texture = std::make_shared<Materials::MaterialFilter>();
    texture->setName(tr("Texture appearance"));
    texture->addRequiredComplete(Materials::ModelUUIDs::ModelUUID_Rendering_Texture);
    filters->push_back(texture);
    auto all = std::make_shared<Materials::MaterialFilter>();
    all->setName(tr("All materials"));
    filters->push_back(all);
    looks->setIncludeEmptyFolders(false);
    looks->setIncludeLegacy(false);
    looks->setFilter(filters);
    looksLayout->addWidget(looks);

    auto* buttons = new QHBoxLayout();
    customize = new QPushButton(tr("Customize…"), looksLike);
    customize->setToolTip(tr("Sets the colours, shininess and transparency by hand"));
    reset = new QPushButton(tr("Reset"), looksLike);
    reset->setToolTip(tr("Returns the selection to the look of its material, or the standard look"));
    buttons->addWidget(customize);
    buttons->addStretch();
    buttons->addWidget(reset);
    looksLayout->addLayout(buttons);
    layout->addWidget(looksLike);
    layout->addStretch();

    connect(
        materials,
        &MatGui::MaterialTreeWidget::materialSelected,
        this,
        &AppearancePanel::onMaterialSelected
    );
    connect(looks, &MatGui::MaterialTreeWidget::materialSelected, this, &AppearancePanel::onLookSelected);
    connect(customize, &QPushButton::clicked, this, &AppearancePanel::onCustomize);
    connect(reset, &QPushButton::clicked, this, &AppearancePanel::onReset);

    auto& app = App::GetApplication();
    undone = app.signalUndo.connect([this]() { scheduleRefresh(); });
    redone = app.signalRedo.connect([this]() { scheduleRefresh(); });
    deleted = app.signalDeletedObject.connect([this](const App::DocumentObject&) {
        targets = {};
        scheduleRefresh();
    });
    documentClosed = app.signalDeleteDocument.connect([this](const App::Document&) {
        targets = {};
        scheduleRefresh();
    });
}

void AppearancePanel::showPanel()
{
    if (!instance) {
        instance = new AppearancePanel();
        instance->dock = Gui::DockWindowManager::instance()
                             ->addDockWindow("Appearance", instance, Qt::RightDockWidgetArea);
        instance->dock->setWindowTitle(tr("Appearance and material"));
        instance->dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable
        );
        Gui::OverlayManager::instance()->setupTitleBar(instance->dock);
    }
    instance->refresh();
    instance->dock->show();
    instance->dock->raise();
}

void AppearancePanel::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    using Type = Gui::SelectionChanges::MsgType;
    if (!isVisible()) {
        return;
    }
    if (msg.Type == Type::AddSelection || msg.Type == Type::RmvSelection
        || msg.Type == Type::SetSelection || msg.Type == Type::ClrSelection) {
        scheduleRefresh();
    }
}

void AppearancePanel::scheduleRefresh()
{
    if (refreshPending) {
        return;
    }
    refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        refreshPending = false;
        if (isVisible()) {
            refresh();
        }
    });
}

void AppearancePanel::refresh()
{
    targets = appearanceTargetsFromSelection();
    const bool nothing = targets.empty();
    const bool partsOnly = !targets.parts.empty() && targets.faces.empty();
    const bool madeOfApplies = partsOnly
        && std::ranges::all_of(targets.parts, [](auto* part) { return Part::hasMaterial(part); });

    emptyState->setVisible(nothing);
    subject->setVisible(!nothing);
    looksLike->setVisible(!nothing);
    madeOf->setVisible(madeOfApplies);
    if (nothing) {
        return;
    }

    std::size_t faceCount = 0;
    for (const auto& entry : targets.faces) {
        faceCount += entry.second.size();
    }
    const auto count = [](std::size_t n, const QString& one, const QString& many) {
        return n == 1 ? one : many.arg(n);
    };
    const QString faceText = count(faceCount, tr("1 face"), tr("%1 faces"));
    const QString partText = count(targets.parts.size(), tr("1 part"), tr("%1 parts"));
    if (targets.faces.empty()) {
        subject->setText(
            targets.parts.size() == 1 ? QString::fromUtf8(targets.parts.front()->Label.getValue())
                                      : partText
        );
    }
    else if (targets.parts.empty() && targets.faces.size() == 1) {
        subject->setText(
            tr("%1 of %2")
                .arg(faceText, QString::fromUtf8(targets.faces.begin()->first->Label.getValue()))
        );
    }
    else if (targets.parts.empty()) {
        subject->setText(faceText);
    }
    else {
        subject->setText(tr("%1 and %2").arg(faceText, partText));
    }

    App::Material shown;
    if (!targets.parts.empty()) {
        shown = appearanceOf(targets.parts.front())->getValues().front();
    }
    else {
        const auto& [obj, faces] = *targets.faces.begin();
        const auto looksNow = lookPerFace(obj);
        shown = looksNow.at(std::min<std::size_t>(*faces.begin(), looksNow.size() - 1));
    }
    {
        QSignalBlocker block(looks);
        looks->setMaterial(QString::fromStdString(shown.uuid));
    }
    if (madeOfApplies) {
        QSignalBlocker block(materials);
        auto* prop = Part::materialPropertyOf(targets.parts.front());
        materials->setMaterial(prop->getValue().getUUID());
    }
}

void AppearancePanel::onMaterialSelected(const std::shared_ptr<Materials::Material>& material)
{
    if (!material || targets.parts.empty() || !targets.faces.empty()) {
        return;
    }
    applyMaterial(targets.parts, *material);
}

void AppearancePanel::onLookSelected(const std::shared_ptr<Materials::Material>& material)
{
    if (!material || targets.empty()) {
        return;
    }
    applyLook(targets, material->getMaterialAppearance());
}

void AppearancePanel::onCustomize()
{
    if (targets.empty()) {
        return;
    }
    App::Material start;
    if (!targets.parts.empty()) {
        start = appearanceOf(targets.parts.front())->getValues().front();
    }
    else {
        const auto& [obj, faces] = *targets.faces.begin();
        const auto looksNow = lookPerFace(obj);
        start = looksNow.at(std::min<std::size_t>(*faces.begin(), looksNow.size() - 1));
    }
    Gui::Dialog::DlgMaterialPropertiesImp dialog(Gui::getMainWindow());
    dialog.setCustomMaterial(start);
    dialog.setDefaultMaterial(start);
    dialog.exec();
    const App::Material chosen = dialog.getCustomMaterial();
    if (sameLook(chosen, start)) {
        return;
    }
    applyLook(targets, chosen);
}

void AppearancePanel::onReset()
{
    resetLook(targets);
    refresh();
}

#include "moc_AppearancePanel.cpp"
