/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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
// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Which rows a document's tree had expanded: saved with the document, and restored when it opens.

#include <cstring>

#include <Base/Reader.h>
#include <Base/Writer.h>

#include <App/DocumentObject.h>

#include "Tree.h"
#include "TreeShared.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;

static unsigned int countExpandedItem(const QTreeWidgetItem* item)
{
    unsigned int size = 0;
    for (int i = 0, count = item->childCount(); i < count; ++i) {
        auto citem = item->child(i);
        if (citem->type() != TreeWidget::ObjectType || !citem->isExpanded()) {
            continue;
        }
        auto obj = static_cast<const DocumentObjectItem*>(citem)->object()->getObject();
        if (obj->isAttachedToDocument()) {
            size += strlen(obj->getNameInDocument()) + countExpandedItem(citem);
        }
    }
    return size;
}

unsigned int DocumentItem::getMemSize() const
{
    return countExpandedItem(this);
}

static void saveExpandedItem(Base::Writer& writer, const QTreeWidgetItem* item)
{
    int itemCount = 0;
    for (int i = 0, count = item->childCount(); i < count; ++i) {
        auto citem = item->child(i);
        if (citem->type() != TreeWidget::ObjectType || !citem->isExpanded()) {
            continue;
        }
        auto obj = static_cast<const DocumentObjectItem*>(citem)->object()->getObject();
        if (obj->isAttachedToDocument()) {
            ++itemCount;
        }
    }

    if (!itemCount) {
        writer.Stream() << "/>" << std::endl;
        return;
    }

    writer.Stream() << " count=\"" << itemCount << "\">" << std::endl;
    writer.incInd();
    for (int i = 0, count = item->childCount(); i < count; ++i) {
        auto citem = item->child(i);
        if (citem->type() != TreeWidget::ObjectType || !citem->isExpanded()) {
            continue;
        }
        auto obj = static_cast<const DocumentObjectItem*>(citem)->object()->getObject();
        if (obj->isAttachedToDocument()) {
            writer.Stream() << writer.ind() << "<Expand name=\"" << obj->getNameInDocument() << "\"";
            saveExpandedItem(writer, static_cast<const DocumentObjectItem*>(citem));
        }
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Expand>" << std::endl;
}

void DocumentItem::Save(Base::Writer& writer) const
{
    writer.Stream() << writer.ind() << "<Expand ";
    saveExpandedItem(writer, this);
}

void DocumentItem::Restore(Base::XMLReader& reader)
{
    reader.readElement("Expand");
    if (!reader.hasAttribute("count")) {
        return;
    }
    _ExpandInfo.reset(new ExpandInfo);
    _ExpandInfo->restore(reader);
    for (auto inst : TreeWidget::Instances) {
        if (inst != getTree()) {
            auto docItem = inst->getDocumentItem(document());
            if (docItem) {
                docItem->_ExpandInfo = _ExpandInfo;
            }
        }
    }
}

void DocumentItem::restoreItemExpansion(const ExpandInfoPtr& info, DocumentObjectItem* item)
{
    item->setExpanded(true);
    if (!info) {
        return;
    }
    for (int i = 0, count = item->childCount(); i < count; ++i) {
        auto citem = item->child(i);
        if (citem->type() != TreeWidget::ObjectType) {
            continue;
        }
        auto obj = static_cast<DocumentObjectItem*>(citem)->object()->getObject();
        if (!obj->isAttachedToDocument()) {
            continue;
        }
        auto it = info->find(obj->getNameInDocument());
        if (it != info->end()) {
            restoreItemExpansion(it->second, static_cast<DocumentObjectItem*>(citem));
        }
    }
}
