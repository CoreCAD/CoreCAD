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


#pragma once

#include <list>
#include <map>
#include <set>
#include <string>
#include <fastsignals/signal.h>

#include <Base/Matrix.h>

#include "Thumbnail.h"

class SoSeparator;

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Gui
{
class Application;
class BaseView;
class View3DInventor;
class View3DInventorViewer;
class ViewProvider;
class ViewProviderDocumentObject;

// Pimpl class
struct DocumentP
{
    Thumbnail thumb;
    int _iWinCount;
    int _iDocId;
    bool _isClosing;
    bool _isModified;
    bool _isTransacting;
    bool _isActive;
    bool _changeViewTouchDocument;
    bool _editWantsRestore;
    bool _editWantsRestorePrevious;
    int _editMode;
    int _editModePrevious;
    ViewProvider* _editViewProvider;
    ViewProvider* _editViewProviderPrevious;
    App::DocumentObject* _editingObject;
    ViewProviderDocumentObject* _editViewProviderParent;
    std::string _editSubname;
    std::string _editSubElement;
    std::string _workbenchName;  // Name of the workbench acting on this document
    Base::Matrix4D _editingTransform;
    View3DInventorViewer* _editingViewer;
    std::set<const App::DocumentObject*> _editObjs;

    Application* _pcAppWnd;
    // the doc/Document
    App::Document* _pcDocument;
    /// List of all registered views
    std::list<Gui::BaseView*> baseViews;
    /// List of all registered views
    std::list<Gui::BaseView*> passiveViews;
    std::map<const App::DocumentObject*, ViewProviderDocumentObject*> _ViewProviderMap;
    std::map<SoSeparator*, ViewProviderDocumentObject*> _CoinMap;
    std::map<std::string, ViewProvider*> _ViewProviderMapAnnotation;
    std::list<ViewProviderDocumentObject*> _redoViewProviders;

    /// Objects contested by several providers as of the last 3D parenting pass, so a change
    /// in that status can be noticed and the affected providers refreshed.
    std::set<const App::DocumentObject*> _contested3D;
    /// Guards the refresh sweep against re-entering itself.
    bool _reconciling3D = false;
    bool _rebuildScheduled = false;

    using Connection = fastsignals::connection;
    using AdvancedConnection = fastsignals::advanced_connection;
    Connection connectNewObject;
    Connection connectDelObject;
    Connection connectCngObject;
    Connection connectRenObject;
    AdvancedConnection connectActObject;
    Connection connectSaveDocument;
    Connection connectFinishSaveDocument;
    Connection connectRestDocument;
    Connection connectStartLoadDocument;
    Connection connectFinishLoadDocument;
    Connection connectShowHidden;
    Connection connectFinishRestoreDocument;
    Connection connectFinishRestoreObject;
    Connection connectExportObjects;
    Connection connectImportObjects;
    Connection connectFinishImportObjects;
    Connection connectUndoDocument;
    Connection connectRedoDocument;
    Connection connectRecomputed;
    Connection connectSkipRecompute;
    Connection connectTransactionAppend;
    Connection connectTransactionRemove;
    Connection connectTouchedObject;
    Connection connectCommitTransaction;
    Connection connectChangePropertyEditor;
    AdvancedConnection connectChangeDocument;

    using ConnectionBlock = fastsignals::shared_connection_block;
    ConnectionBlock connectActObjectBlocker;
    ConnectionBlock connectChangeDocumentBlocker;

    static ViewProviderDocumentObject* throwIfCastFails(ViewProvider* p);

    static App::DocumentObject* tryGetObject(ViewProviderDocumentObject* vp);

    void throwIfNotInMap(App::DocumentObject* obj, App::Document* doc) const;

    App::DocumentObject* tryGetSubObject(App::DocumentObject* obj, const char* subname);

    ViewProviderDocumentObject* tryGetSubViewProvider(
        ViewProviderDocumentObject* vp,
        App::DocumentObject* obj,
        App::DocumentObject* sobj
    ) const;

    void setParentViewProvider(ViewProviderDocumentObject* vp);

    void clearSubElement();

    void findElementName(const char* subname);

    void findSubObjectList(App::DocumentObject* obj, const char* subname);

    bool tryStartEditing(
        ViewProviderDocumentObject* vp,
        App::DocumentObject* obj,
        const char* subname,
        int ModNum
    );

    bool tryStartEditing(ViewProviderDocumentObject* svp, App::DocumentObject* sobj, int ModNum);

    void setEditingViewerIfPossible(View3DInventor* view3d, int ModNum);

    void signalEditMode();

    void setDocumentNameOfTaskDialog(App::Document* doc);
};

}  // namespace Gui
