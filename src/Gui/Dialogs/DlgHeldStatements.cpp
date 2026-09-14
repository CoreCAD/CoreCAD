// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   Cruth is free software: you can redistribute it and/or modify it       *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Cruth is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with Cruth. If not, see                                  *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QHeaderView>
# include <QMessageBox>
# include <QPushButton>
# include <QTreeWidgetItem>
# include <map>
# include <string>
# include <utility>
# include <vector>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyContainer.h>
#include <Base/Exception.h>

#include "Dialogs/DlgHeldStatements.h"
#include "ui_DlgHeldStatements.h"
#include "Command.h"


using namespace Gui::Dialog;

namespace
{
/// What the act needs, carried on the row a person picks: who holds it, the name it is held
/// under, and whether the name is a whole object block rather than one of an object's values.
constexpr int HolderRole = Qt::UserRole;
constexpr int NameRole = Qt::UserRole + 1;
constexpr int WholeObjectRole = Qt::UserRole + 2;

/// One row: a name something is held under, and every reason it is held under that name.
struct HeldRow
{
    std::string holder;
    std::string name;
    std::string why;
    bool wholeObject = false;
    QString statedName;  ///< for a whole object block, the name its own words give it
};

/// How an object says which one it is, to a person rather than to the program.
QString nameFor(const App::DocumentObject* obj)
{
    const QString internal = QString::fromUtf8(obj->getNameInDocument());
    const QString label = QString::fromUtf8(obj->Label.getValue());
    return label == internal ? internal : QStringLiteral("%1 (%2)").arg(label, internal);
}

/** The name a kept object block states for itself, read out of the block's own opening tag.
 *
 * A durable id identifies an object to the program and to nothing else. A person about to drop
 * a whole object needs to know which one, and the only thing in the block that answers that is
 * the name the file states. The words are in this program's own form, so they are read rather
 * than guessed at; where the block does not state one, the id stands alone.
 */
QString statedNameIn(const std::string& block)
{
    const std::string::size_type object = block.find("<Object ");
    if (object == std::string::npos) {
        return {};
    }
    const std::string::size_type close = block.find('>', object);
    const std::string::size_type at = block.find(" name=\"", object);
    if (at == std::string::npos || (close != std::string::npos && at > close)) {
        return {};
    }
    const std::string::size_type from = at + std::string(" name=\"").size();
    const std::string::size_type to = block.find('"', from);
    if (to == std::string::npos) {
        return {};
    }
    return QString::fromUtf8(block.substr(from, to - from).c_str());
}
}  // namespace


DlgHeldStatements::DlgHeldStatements(App::Document* doc, QWidget* parent)
    : QDialog(parent)
    , _doc(doc)
    , ui(new Ui_DlgHeldStatements)
{
    ui->setupUi(this);

    // Not a default button anywhere near a destructive act: a person reaches it deliberately.
    _discard = ui->buttonBox->addButton(tr("Discard..."), QDialogButtonBox::DestructiveRole);
    _discard->setAutoDefault(false);
    _discard->setEnabled(false);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_discard, &QPushButton::clicked, this, &DlgHeldStatements::onDiscard);
    connect(
        ui->statements,
        &QTreeWidget::itemSelectionChanged,
        this,
        &DlgHeldStatements::onSelectionChanged
    );

    reload();
}

DlgHeldStatements::~DlgHeldStatements()
{
    delete ui;
}

void DlgHeldStatements::reload()
{
    ui->statements->clear();
    ui->words->clear();
    _discard->setEnabled(false);
    if (_doc == nullptr) {
        return;
    }

    std::map<std::string, QString> blocks;
    for (const auto& kept : _doc->unreadObjects()) {
        blocks.emplace(kept[0], statedNameIn(kept[2]));
    }

    // Grouped by the name it is held under, because that is what the act takes. One name can be
    // held for more than one reason -- a reference that did not resolve and source material that
    // was not found are two statements under one name -- and a single discard drops all of them.
    // A row per reason would offer a person a choice the act cannot honour.
    std::vector<HeldRow> rows;
    std::map<std::pair<std::string, std::string>, std::size_t> at;
    for (const auto& [holder, name, why] : _doc->heldStatements()) {
        const auto key = std::make_pair(holder, name);
        if (const auto found = at.find(key); found != at.end()) {
            rows[found->second].why += "; " + why;
            continue;
        }
        at.emplace(key, rows.size());
        HeldRow row;
        row.holder = holder;
        row.name = name;
        row.why = why;
        if (const auto block = blocks.find(name); holder.empty() && block != blocks.end()) {
            row.wholeObject = true;
            row.statedName = block->second;
        }
        rows.push_back(std::move(row));
    }

    const bool holdsNothing = rows.empty();
    if (holdsNothing) {
        ui->explanation->setText(
            tr("This document holds nothing it cannot honour. Everything its file states, this "
               "build could read.")
        );
    }
    ui->consequence->setVisible(!holdsNothing);
    ui->statements->setVisible(!holdsNothing);
    ui->wordsLabel->setVisible(!holdsNothing);
    ui->words->setVisible(!holdsNothing);
    if (holdsNothing) {
        return;
    }

    for (const HeldRow& row : rows) {
        QString heldBy = tr("This document");
        if (!row.holder.empty()) {
            const App::DocumentObject* obj = _doc->getObject(row.holder.c_str());
            heldBy = obj != nullptr ? nameFor(obj) : QString::fromUtf8(row.holder.c_str());
        }
        auto* item = new QTreeWidgetItem(ui->statements);
        item->setText(0, heldBy);
        const QString said = QString::fromUtf8(row.name.c_str());
        item->setText(
            1,
            row.statedName.isEmpty() ? said : QStringLiteral("%1 (%2)").arg(row.statedName, said)
        );
        item->setText(2, QString::fromUtf8(row.why.c_str()));
        item->setData(0, HolderRole, QString::fromUtf8(row.holder.c_str()));
        item->setData(0, NameRole, QString::fromUtf8(row.name.c_str()));
        item->setData(0, WholeObjectRole, row.wholeObject);
    }
    ui->statements->resizeColumnToContents(0);
    ui->statements->resizeColumnToContents(1);
}

void DlgHeldStatements::onSelectionChanged()
{
    ui->words->clear();
    const QTreeWidgetItem* item = ui->statements->currentItem();
    _discard->setEnabled(item != nullptr && item->isSelected());
    if (item == nullptr || !item->isSelected() || _doc == nullptr) {
        return;
    }

    const std::string holder = item->data(0, HolderRole).toString().toStdString();
    const std::string name = item->data(0, NameRole).toString().toStdString();

    // The file's own words, not this session's reading of them -- the point of keeping a
    // statement is that nobody here understood it, so nobody here may paraphrase it either.
    if (item->data(0, WholeObjectRole).toBool()) {
        for (const auto& kept : _doc->unreadObjects()) {
            if (kept[0] == name) {
                ui->words->setPlainText(QString::fromUtf8(kept[2].c_str()));
                return;
            }
        }
        return;
    }

    App::PropertyContainer* held = _doc->holderOfStatements(holder);
    if (held == nullptr) {
        return;
    }
    const App::PropertyContainer::KeptStatement kept = held->keptStatementFor(name);
    QStringList said;
    if (!kept.statedWords.empty()) {
        said << QString::fromUtf8(kept.statedWords.c_str());
    }
    if (!kept.missingSource.empty()) {
        said << tr("Source material named: %1").arg(QString::fromUtf8(kept.missingSource.c_str()));
    }
    for (const auto& target : kept.unresolvedTargets) {
        const QString uuid = QString::fromUtf8(target.uuid.c_str());
        said
            << (target.sub.empty() ? tr("Points at the object %1").arg(uuid)
                                   : tr("Points at %1 of the object %2")
                                         .arg(QString::fromUtf8(target.sub.c_str()), uuid));
    }
    ui->words->setPlainText(said.join(QLatin1String("\n")));
}

void DlgHeldStatements::onDiscard()
{
    const QTreeWidgetItem* item = ui->statements->currentItem();
    if (item == nullptr || !item->isSelected() || _doc == nullptr) {
        return;
    }
    const QString holder = item->data(0, HolderRole).toString();
    const QString name = item->data(0, NameRole).toString();
    const bool wholeObject = item->data(0, WholeObjectRole).toBool();

    QMessageBox ask(this);
    ask.setIcon(QMessageBox::Warning);
    ask.setWindowTitle(tr("Discard Statement"));
    ask.setText(
        holder.isEmpty() ? tr("Discard '%1'?").arg(item->text(1))
                         : tr("Discard '%1' on %2?").arg(item->text(1), item->text(0))
    );
    ask.setInformativeText(
        tr("%1.\n\nThe words the file states for it are dropped from this document, and the next "
           "save writes the document without them. The file on disk is the only other copy.\n\n"
           "The act is recorded in this document's history as your own, and can be undone.")
            .arg(item->text(2))
    );
    QPushButton* discard = ask.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton* cancel = ask.addButton(QMessageBox::Cancel);
    ask.setDefaultButton(cancel);
    ask.exec();
    if (ask.clickedButton() != discard) {
        return;
    }

    // Through the same call a script would make, so a recorded macro says what was done here and
    // the one act has one meaning (P8). The refusals the act may raise are a person's to read.
    const QString doc = QString::fromUtf8(_doc->getName());
    try {
        if (wholeObject) {
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.getDocument('%s').discardUnreadObject('%s')",
                doc.toUtf8().constData(),
                name.toUtf8().constData()
            );
        }
        else if (holder.isEmpty()) {
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.getDocument('%s').discardStatement(None, '%s')",
                doc.toUtf8().constData(),
                name.toUtf8().constData()
            );
        }
        else {
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.getDocument('%s').discardStatement("
                "App.getDocument('%s').getObject('%s'), '%s')",
                doc.toUtf8().constData(),
                doc.toUtf8().constData(),
                holder.toUtf8().constData(),
                name.toUtf8().constData()
            );
        }
    }
    catch (const Base::Exception& e) {
        QMessageBox::warning(this, tr("Discard Statement"), QString::fromUtf8(e.what()));
    }
    reload();
}

#include "moc_DlgHeldStatements.cpp"
