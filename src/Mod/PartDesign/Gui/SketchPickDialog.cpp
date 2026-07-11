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
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Part2DObject.h>

#include "SketchPickDialog.h"

using namespace PartDesignGui;

namespace
{

Part::Part2DObject* sketchOf(const QListWidgetItem* item)
{
    if (!item) {
        return nullptr;
    }
    return reinterpret_cast<Part::Part2DObject*>(item->data(Qt::UserRole).value<void*>());
}

// Highlight the given sketch in the 3D view (and tree) so the user sees what they are
// about to pick. Clears any prior highlight first.
void highlightInView(Part::Part2DObject* sketch)
{
    Gui::Selection().clearSelection();
    if (sketch) {
        Gui::Selection().addSelection(sketch->getDocument()->getName(), sketch->getNameInDocument());
    }
}

}  // namespace

Part::Part2DObject* PartDesignGui::pickSketch(const std::vector<Part::Part2DObject*>& candidates)
{
    if (candidates.empty()) {
        return nullptr;
    }

    QDialog dialog(Gui::getMainWindow());
    dialog.setWindowTitle(
        QCoreApplication::translate("PartDesignGui::SketchPickDialog", "Select a sketch")
    );

    auto* layout = new QVBoxLayout(&dialog);

    auto* prompt = new QLabel(
        QCoreApplication::translate(
            "PartDesignGui::SketchPickDialog",
            "Several sketches are available. Choose the one to use:"
        ),
        &dialog
    );
    layout->addWidget(prompt);

    auto* list = new QListWidget(&dialog);
    for (auto* sketch : candidates) {
        auto* item = new QListWidgetItem(QString::fromUtf8(sketch->Label.getValue()), list);
        item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(sketch)));
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
        highlightInView(sketchOf(list->currentItem()));
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        highlightInView(nullptr);  // cancelled: drop the transient highlight
        return nullptr;
    }

    return sketchOf(list->currentItem());
}
