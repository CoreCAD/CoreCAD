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


#include <string>
#include <map>
#include <set>
#include <vector>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "Document.h"
#include "Application.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

using namespace Gui;

std::set<const App::DocumentObject*> Document::contestedChildren3D() const
{
    std::map<const App::DocumentObject*, int> wanted;
    auto tally = [&wanted](ViewProvider* vp) {
        if (!vp) {
            return;
        }
        // Per provider, count an object once: a provider naming the same child twice is
        // still only one parent, and must not make the object look contested.
        std::set<const App::DocumentObject*> named;
        for (const App::DocumentObject* child : vp->claimChildren3D()) {
            if (child && named.insert(child).second) {
                ++wanted[child];
            }
        }
    };

    for (const auto& vp : d->_ViewProviderMap) {
        tally(vp.second);
    }
    for (const auto& va : d->_ViewProviderMapAnnotation) {
        tally(va.second);
    }

    std::set<const App::DocumentObject*> contested;
    for (const auto& [obj, count] : wanted) {
        if (count > 1) {
            contested.insert(obj);
        }
    }
    return contested;
}

void Document::reconcileContested3D(ViewProvider* alreadyHandled)
{
    // Whether an object is contested depends on ALL the providers, so it can change without
    // the affected provider's own object changing at all: adding a second body off a shared
    // sketch contests that sketch, but only the new object gets a change notification, and
    // the FIRST body goes on parenting the sketch it no longer may. Nothing else recomputes
    // it, so the stale parent survives until something unrelated touches that body.
    if (d->_reconciling3D) {
        return;
    }

    std::set<const App::DocumentObject*> contested = contestedChildren3D();
    if (contested == d->_contested3D) {
        return;
    }

    // Only the objects whose status actually flipped need their providers rebuilt.
    std::set<const App::DocumentObject*> flipped;
    std::set_symmetric_difference(
        contested.begin(),
        contested.end(),
        d->_contested3D.begin(),
        d->_contested3D.end(),
        std::inserter(flipped, flipped.begin())
    );
    d->_contested3D = std::move(contested);

    Base::FlagToggler<bool> guard(d->_reconciling3D, true);
    for (const auto& entry : d->_ViewProviderMap) {
        ViewProvider* vp = entry.second;
        if (vp == alreadyHandled || !vp->getChildRoot()) {
            continue;
        }
        for (const App::DocumentObject* child : vp->claimChildren3D()) {
            if (flipped.contains(child)) {
                handleChildren3D(vp);
                break;
            }
        }
    }
}

void Document::handleChildren3D(ViewProvider* viewProvider, bool deleting)
{
    // check for children
    if (viewProvider && viewProvider->getChildRoot()) {
        std::vector<App::DocumentObject*> children = viewProvider->claimChildren3D();

        // Drop the objects another provider also wants (see contestedChildren3D): they stay
        // at the scene root rather than hanging under two parents at once.
        const std::set<const App::DocumentObject*> contested = contestedChildren3D();
        if (!contested.empty()) {
            children.erase(
                std::remove_if(
                    children.begin(),
                    children.end(),
                    [&contested](const App::DocumentObject* child) {
                        return contested.contains(child);
                    }
                ),
                children.end()
            );
        }
        SoGroup* childGroup = viewProvider->getChildRoot();
        SoGroup* frontGroup = viewProvider->getFrontRoot();
        SoGroup* backGroup = viewProvider->getFrontRoot();

        // size not the same -> build up the list new
        if (deleting || childGroup->getNumChildren() != static_cast<int>(children.size())) {

            std::set<ViewProviderDocumentObject*> oldChildren;
            for (int i = 0, count = childGroup->getNumChildren(); i < count; ++i) {
                auto it = d->_CoinMap.find(static_cast<SoSeparator*>(childGroup->getChild(i)));
                if (it == d->_CoinMap.end()) {
                    continue;
                }
                oldChildren.insert(it->second);
            }

            Gui::coinRemoveAllChildren(childGroup);
            Gui::coinRemoveAllChildren(frontGroup);
            Gui::coinRemoveAllChildren(backGroup);

            if (!deleting) {
                for (const auto& it : children) {
                    if (auto ChildViewProvider
                        = dynamic_cast<ViewProviderDocumentObject*>(getViewProvider(it))) {
                        auto itOld = oldChildren.find(ChildViewProvider);
                        if (itOld != oldChildren.end()) {
                            oldChildren.erase(itOld);
                        }

                        if (SoSeparator* childRootNode = ChildViewProvider->getRoot()) {
                            if (childRootNode == childGroup) {
                                Base::Console().warning(
                                    "Document::handleChildren3D: Do not add "
                                    "group of '%s' to itself\n",
                                    it->getNameInDocument()
                                );
                            }
                            else if (childGroup) {
                                childGroup->addChild(childRootNode);
                            }
                        }

                        if (SoSeparator* childFrontNode = ChildViewProvider->getFrontRoot()) {
                            if (childFrontNode == frontGroup) {
                                Base::Console().warning(
                                    "Document::handleChildren3D: Do not add "
                                    "foreground group of '%s' to itself\n",
                                    it->getNameInDocument()
                                );
                            }
                            else if (frontGroup) {
                                frontGroup->addChild(childFrontNode);
                            }
                        }

                        if (SoSeparator* childBackNode = ChildViewProvider->getBackRoot()) {
                            if (childBackNode == backGroup) {
                                Base::Console().warning(
                                    "Document::handleChildren3D: Do not add "
                                    "background group of '%s' to itself\n",
                                    it->getNameInDocument()
                                );
                            }
                            else if (backGroup) {
                                backGroup->addChild(childBackNode);
                            }
                        }

                        // cycling to all views of the document to remove the viewprovider from the
                        // viewer itself
                        for (Gui::BaseView* vIt : d->baseViews) {
                            auto activeView = dynamic_cast<View3DInventor*>(vIt);
                            if (activeView
                                && activeView->getViewer()->hasViewProvider(ChildViewProvider)) {
                                // @Note hasViewProvider()
                                // remove the viewprovider serves the purpose of detaching the
                                // inventor nodes from the top level root in the viewer. However, if
                                // some of the children were grouped beneath the object earlier they
                                // are not anymore part of the toplevel inventor node. we need to
                                // check for that.
                                activeView->getViewer()->removeViewProvider(ChildViewProvider);
                            }
                        }
                    }
                }
            }

            // add the remaining old children back to toplevel invertor node
            for (auto vpd : oldChildren) {
                auto obj = vpd->getObject();
                if (!obj || !obj->isAttachedToDocument()) {
                    continue;
                }

                for (BaseView* view : d->baseViews) {
                    auto activeView = dynamic_cast<View3DInventor*>(view);
                    if (activeView && !activeView->getViewer()->hasViewProvider(vpd)) {
                        activeView->getViewer()->addViewProvider(vpd);
                    }
                }
            }
        }
    }
}

void Document::toggleInSceneGraph(ViewProvider* vp)
{
    // FIXME: What's the point of having this function?
    //
    for (auto view : d->baseViews) {
        auto activeView = dynamic_cast<View3DInventor*>(view);
        if (!activeView) {
            continue;
        }

        auto root = vp->getRoot();
        if (!root) {
            continue;
        }

        auto scenegraph = dynamic_cast<SoGroup*>(activeView->getViewer()->getSceneGraph());
        if (!scenegraph) {
            continue;
        }

        // If it cannot be added then only check the top-level nodes
        if (!vp->canAddToSceneGraph()) {
            int idx = scenegraph->findChild(root);
            if (idx >= 0) {
                scenegraph->removeChild(idx);
            }
        }
        else {
            // Do a deep search of the scene because the root node
            // isn't necessarily a top-level node when claimed by
            // another view provider.
            // This is to avoid to add a node twice to the scene.
            SoSearchAction sa;
            sa.setNode(root);
            sa.setSearchingAll(false);
            sa.apply(scenegraph);

            SoPath* path = sa.getPath();
            if (!path) {
                scenegraph->addChild(root);
            }
        }
    }
}
