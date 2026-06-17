// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#include <Mod/Part/Gui/ViewProvider.h>
#include <Mod/PartDesign/PartDesignGlobal.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Gui/ViewProviderPart.h>
#include <Gui/ViewProviderOriginGroupExtension.h>
#include <QCoreApplication>
#include <fastsignals/signal.h>

class SoGroup;
class SoSeparator;
class SbBox3f;
class SoGetBoundingBoxAction;
namespace PartDesignGui
{

/** ViewProvider of the Body feature
 *  This class manages the visual appearance of the features in the
 *  Body feature. That means while editing all visible features are shown.
 *  If the Body is not active it shows only the result shape (tip).
 * \author jriegel
 */
class PartDesignGuiExport ViewProviderBody: public PartGui::ViewProviderPart,
                                            public Gui::ViewProviderOriginGroupExtension
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderBody)
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesignGui::ViewProviderBody);

public:
    /// constructor
    ViewProviderBody();
    /// destructor
    ~ViewProviderBody() override;

    App::PropertyEnumeration DisplayModeBody;

    void attach(App::DocumentObject*) override;

    bool doubleClicked() override;
    void setupContextMenu(QMenu* menu, QObject* receiver, const char* member) override;
    bool isActiveBody();
    void toggleActiveBody();

    std::vector<std::string> getDisplayModes() const override;
    void setDisplayMode(const char* ModeName) override;
    void setOverrideMode(const std::string& mode) override;

    bool onDelete(const std::vector<std::string>&) override;

    /// Update the children's highlighting when triggered
    void updateData(const App::Property* prop) override;
    /// unify children visuals
    void onChanged(const App::Property* prop) override;

    /**
     * Return the bounding box of visible features
     * @note datums are counted as their base point only
     */
    SbBox3f getBoundBox();

    PartDesign::Feature* getShownFeature() const;
    ViewProvider* getShownViewProvider() const;

    /** Check whether objects can be added to the view provider by drag and drop */
    bool canDropObjects() const override;
    /** Check whether the object can be dropped to the view provider by drag and drop */
    bool canDropObject(App::DocumentObject*) const override;
    /** Add an object to the view provider by drag and drop */
    void dropObject(App::DocumentObject*) override;
    bool canDragObjectToTarget(App::DocumentObject* obj, App::DocumentObject* target) const override;
    /* Check whether the object accept reordering of its children during drop.*/
    bool acceptReorderingObjects() const override
    {
        return true;
    };

    /// Override to return the color of the tip instead of the body, which doesn't really have color
    std::map<std::string, Base::Color> getElementColors(const char* element) const override;

    /**
     * Derive the body's tree children from the BaseFeature chain (walked backward
     * from the Tip) rather than from exclusive Group membership. This makes the
     * pipeline the source of truth for the tree (ARCHITECTURE.md §3.2/§3.3): a
     * feature that is on the chain still appears even if it is no longer a Group
     * member, while non-pipeline objects (Origin, datums, unconsumed sketches)
     * are still surfaced so nothing disappears during the ownership migration.
     */
    std::vector<App::DocumentObject*> claimChildren() const override;

    /**
     * Derive the body's 3D scene-graph children from the BaseFeature chain too,
     * mirroring claimChildren(). The base OriginGroup extension parents only
     * Group members under the body's coordinate node, so a de-owned feature (on
     * the chain but not in Group) would never inherit the body frame. This flat
     * variant returns Origin + every chain feature + their claimed sub-objects
     * (sketches/datums) so all pipeline objects are parented. For a normal body
     * (every feature both on the chain and in Group) the set is identical to the
     * old Group-based one, so non-de-owned bodies are unaffected.
     */
    std::vector<App::DocumentObject*> claimChildren3D() const override;

    void show() override;

protected:
    /// Copy over all visual properties to the child features
    void unifyVisualProperty(const App::Property* prop);
    /// Set Feature viewprovider into visual body mode
    void setVisualBodyMode(bool bodymode);

private:
    static const char* BodyModeEnum[];

    /// Ordered pipeline (base -> tip) derived by walking BaseFeature back from
    /// the Tip, with a cycle guard. Shared by claimChildren() and
    /// claimChildren3D() so the chain walk has a single source of truth.
    std::vector<App::DocumentObject*> pipelineChain() const;

    void afterRecompute(const App::Document&, const std::vector<App::DocumentObject*>& recomputedObjs);
    fastsignals::scoped_connection m_RecomputedConn;
    void onChangedObject(const Gui::ViewProvider& vp, const App::Property& prop);
    fastsignals::scoped_connection m_ChangedConn;
    void refreshOverlays();
};

}  // namespace PartDesignGui
