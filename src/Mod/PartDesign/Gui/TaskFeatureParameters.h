// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (C) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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


#include <type_traits>
#include <vector>
#include <Gui/TaskView/TaskDialog.h>
#include <Gui/TaskView/TaskView.h>
#include <Gui/DocumentObserver.h>

#include "ViewProvider.h"

class QCheckBox;
class QLabel;
class QPushButton;

namespace PartDesign
{
class Body;
}

namespace PartDesignGui
{

class Ui_TaskPreviewParameters;

class TaskPreviewParameters: public Gui::TaskView::TaskBox
{
    Q_OBJECT

public:
    explicit TaskPreviewParameters(ViewProvider* vp, QWidget* parent = nullptr);
    ~TaskPreviewParameters() override;

public Q_SLOTS:
    void onShowPreviewChanged(bool show);
    void onShowFinalChanged(bool show);

private:
    ViewProvider* vp;
    std::unique_ptr<Ui_TaskPreviewParameters> ui;

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/PartDesign/Preview"
    );
};

/// Cruth §8.5 (#27): the "Merge result" control. Makes the otherwise-silent
/// spawn-vs-extend choice at feature creation visible and reversible. Checked = the
/// feature extends the Body it was anchored to; unchecked = the feature is its own new
/// Body. Additive sketch features only — a subtractive cut must extend something, and a
/// feature that opened as its own new Body (bare global plane) has nothing to merge into,
/// so in both cases the checkbox is present but disabled.
///
/// A pattern asks a SECOND merge question, and this box carries both, because they are one
/// subject to the person answering them: does the result join the Body it was added to, and
/// do the COPIES join EACH OTHER (§5.5, #34). The two are independent — a pattern can extend
/// its Body while keeping its copies apart — and the second row appears only for a pattern,
/// which is the only feature that has copies to ask about.
class TaskMergeResultParameters: public Gui::TaskView::TaskBox
{
    Q_OBJECT

public:
    explicit TaskMergeResultParameters(PartDesignGui::ViewProvider* vp, QWidget* parent = nullptr);
    ~TaskMergeResultParameters() override;

private Q_SLOTS:
    void onMergeToggled(bool merge);
    void onPickBody();
    void onMergeCopiesToggled(bool merge);

private:
    void refreshBodyLabel();
    /// Restate what the last recompute found: how many copies were asked for and how many
    /// pieces they came back as. Says nothing when the question did not arise — when the
    /// copies were kept apart, or when there are too few to run into each other.
    void refreshOverlapNotice();
    /// Bodies this feature could legally be moved into: every Body in the document except
    /// the one it already sits in, and except any that depends on it (which would close a
    /// cycle). Recomputed on demand — a move can retire the Body it left (§4.7).
    std::vector<PartDesign::Body*> candidateBodies() const;

    PartDesignGui::ViewProvider* vp;
    QCheckBox* mergeCheckBox;
    QLabel* bodyLabel;
    QPushButton* pickBodyButton;
    /// Patterns only; null for every other feature. Checked = the copies are fused with each
    /// other, which is the feature's MultiBody property read the way a person thinks about it
    /// (MultiBody true means "keep them apart", so the checkbox is its inverse).
    QCheckBox* mergeCopiesCheckBox {nullptr};
    QLabel* overlapLabel {nullptr};
    /// The pre-existing Body the feature was anchored to when the dialog opened — the
    /// target we splice back onto when the user re-enables merge after toggling off.
    /// Null when the feature opened as its own new Body (nothing to merge into).
    PartDesign::Body* mergeTargetBody;
};

/// Convenience class to collect common methods for all SketchBased features
class TaskFeatureParameters: public Gui::TaskView::TaskBox, public Gui::DocumentObserver
{
    Q_OBJECT

public:
    TaskFeatureParameters(
        PartDesignGui::ViewProvider* vp,
        QWidget* parent,
        const std::string& pixmapname,
        const QString& parname
    );
    ~TaskFeatureParameters() override = default;

    /// save field history
    virtual void saveHistory()
    {}
    /// apply changes made in the parameters input to the model via commands
    virtual void apply()
    {}

    void recomputeFeature();

    bool isUpdateBlocked() const
    {
        return blockUpdate;
    }

protected Q_SLOTS:
    // TODO Add update view to all dialogs (2015-12-05, Fat-Zer)
    void onUpdateView(bool on);

private:
    /** Notifies when the object is about to be removed. */
    void slotDeletedObject(const Gui::ViewProviderDocumentObject& Obj) override;

protected:
    template<typename T = PartDesignGui::ViewProvider>
    T* getViewObject() const
    {
        static_assert(std::is_base_of<PartDesignGui::ViewProvider, T>::value, "Wrong template argument");
        return freecad_cast<T*>(vp);
    }

    template<typename T = App::DocumentObject>
    T* getObject() const
    {
        static_assert(std::is_base_of<App::DocumentObject, T>::value, "Wrong template argument");

        if (vp) {
            return vp->getObject<T>();
        }

        return nullptr;
    }

    Gui::Document* getGuiDocument() const
    {
        return vp ? vp->getDocument() : nullptr;
    }

    App::Document* getAppDocument() const
    {
        auto obj = getObject();
        return obj ? obj->getDocument() : nullptr;
    }

    bool& getUpdateBlockRef()
    {
        return blockUpdate;
    }

    void setUpdateBlocked(bool value)
    {
        blockUpdate = value;
    }

protected:
    PartDesignGui::ViewProvider* vp;

private:
    bool blockUpdate;
};

/// A common base for sketch based, dressup and other solid parameters dialogs
class TaskDlgFeatureParameters: public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskDlgFeatureParameters(PartDesignGui::ViewProvider* vp);
    ~TaskDlgFeatureParameters() override;

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    /// is called by the framework if the dialog is rejected (Cancel)
    bool reject() override;

    template<typename T = PartDesignGui::ViewProvider>
    T* getViewObject() const
    {
        static_assert(std::is_base_of<PartDesignGui::ViewProvider, T>::value, "Wrong template argument");
        return freecad_cast<T*>(vp);
    }

    template<typename T = App::DocumentObject>
    T* getObject() const
    {
        static_assert(std::is_base_of<App::DocumentObject, T>::value, "Wrong template argument");
        if (vp) {
            return vp->getObject<T>();
        }

        return nullptr;
    }

protected:
    PartDesignGui::TaskPreviewParameters* preview;

private:
    PartDesignGui::ViewProvider* vp;
};

}  // namespace PartDesignGui
