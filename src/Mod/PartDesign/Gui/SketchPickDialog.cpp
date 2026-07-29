// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Cruth (CoreCAD fork of FreeCAD)                     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QDialog>
# include <QDialogButtonBox>
# include <QLabel>
# include <QListWidget>
# include <QPushButton>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/Body.h>

#include "SketchPickDialog.h"

using namespace PartDesignGui;

namespace
{

App::DocumentObject* objectOf(const QListWidgetItem* item)
{
    if (!item) {
        return nullptr;
    }
    return reinterpret_cast<App::DocumentObject*>(item->data(Qt::UserRole).value<void*>());
}

// Highlight the given object in the 3D view (and tree) so the user sees what they are
// about to pick. Clears any prior highlight first.
void highlightInView(App::DocumentObject* obj)
{
    Gui::Selection().clearSelection();
    if (obj) {
        Gui::Selection().addSelection(obj->getDocument()->getName(), obj->getNameInDocument());
    }
}

// The one modal chooser shared by pickSketch and pickBody: list the candidates by label,
// highlight the current one in the 3D view, gate OK until a row is picked, and return the
// chosen object (or nullptr on cancel). The typed wrappers cast the result.
App::DocumentObject* pickFromCandidates(
    const std::vector<App::DocumentObject*>& candidates,
    const QString& title,
    const QString& prompt
)
{
    if (candidates.empty()) {
        return nullptr;
    }

    QDialog dialog(Gui::getMainWindow());
    dialog.setWindowTitle(title);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(prompt, &dialog));

    auto* list = new QListWidget(&dialog);
    for (auto* obj : candidates) {
        auto* item = new QListWidgetItem(QString::fromUtf8(obj->Label.getValue()), list);
        item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(obj)));
    }
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);

    // Nothing is chosen yet: keep OK inert until the user selects a row, and highlight
    // the current candidate in the 3D view as the selection moves.
    okButton->setEnabled(false);
    QObject::connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, okButton]() {
        okButton->setEnabled(!list->selectedItems().isEmpty());
        highlightInView(objectOf(list->currentItem()));
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        highlightInView(nullptr);  // cancelled: drop the transient highlight
        return nullptr;
    }

    return objectOf(list->currentItem());
}

}  // namespace

Part::Part2DObject* PartDesignGui::pickSketch(const std::vector<Part::Part2DObject*>& candidates)
{
    std::vector<App::DocumentObject*> objects(candidates.begin(), candidates.end());
    auto* picked = pickFromCandidates(
        objects,
        QCoreApplication::translate("PartDesignGui::SketchPickDialog", "Select a sketch"),
        QCoreApplication::translate(
            "PartDesignGui::SketchPickDialog",
            "Several sketches are available. Choose the one to use:"
        )
    );
    return static_cast<Part::Part2DObject*>(picked);
}

PartDesign::Body* PartDesignGui::pickBody(const std::vector<PartDesign::Body*>& candidates)
{
    std::vector<App::DocumentObject*> objects(candidates.begin(), candidates.end());
    auto* picked = pickFromCandidates(
        objects,
        QCoreApplication::translate("PartDesignGui::SketchPickDialog", "Select a body"),
        QCoreApplication::translate(
            "PartDesignGui::SketchPickDialog",
            "Several bodies are available. Choose the one to operate on:"
        )
    );
    return static_cast<PartDesign::Body*>(picked);
}
