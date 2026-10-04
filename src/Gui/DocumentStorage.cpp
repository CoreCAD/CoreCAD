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


#include <memory>
#include <list>
#include <string>
#include <map>
#include <vector>
#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/Transactions.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Tools.h>

#include "Document.h"
#include "Application.h"
#include "Command.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "Thumbnail.h"
#include "Tree.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"
#include "private/DocumentP.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

static bool checkCanonicalPath(const std::map<App::Document*, bool>& docs)
{
    std::map<QString, std::vector<App::Document*>> paths;
    bool warn = false;
    for (auto doc : App::GetApplication().getDocuments()) {
        QFileInfo info(QString::fromUtf8(doc->FileName.getValue()));
        auto& d = paths[info.canonicalFilePath()];
        d.push_back(doc);
        if (!warn && d.size() > 1) {
            if (docs.contains(d.front()) || docs.contains(d.back())) {
                warn = true;
            }
        }
    }
    if (!warn) {
        return true;
    }
    QString msg;
    QTextStream ts(&msg);
    ts << QObject::tr("Identical physical path detected. It may cause unwanted overwrite of existing document!\n\n")
       << QObject::tr("Are you sure you want to continue?");

    auto docName = [](App::Document* doc) -> QString {
        if (doc->Label.getStrValue() == doc->getName()) {
            return QString::fromUtf8(doc->getName());
        }
        return QStringLiteral("%1 (%2)").arg(
            QString::fromUtf8(doc->Label.getValue()),
            QString::fromUtf8(doc->getName())
        );
    };
    int count = 0;
    for (auto& v : paths) {
        if (v.second.size() <= 1) {
            continue;
        }
        for (auto doc : v.second) {
            if (docs.contains(doc)) {
                FC_WARN("Physical path: " << v.first.toUtf8().constData());
                for (auto d : v.second) {
                    FC_WARN(
                        "  Document: " << docName(d).toUtf8().constData() << ": "
                                       << d->FileName.getValue()
                    );
                }
                if (count == 3) {
                    ts << "\n\n" << QObject::tr("Check report view for more…");
                }
                else if (count < 3) {
                    ts << "\n\n"
                       << QObject::tr("Physical path:") << ' ' << v.first << "\n"
                       << QObject::tr("Document:") << ' ' << docName(doc) << "\n  "
                       << QObject::tr("Path:") << ' ' << QString::fromUtf8(doc->FileName.getValue());
                    for (auto d : v.second) {
                        if (d == doc) {
                            continue;
                        }
                        ts << "\n"
                           << QObject::tr("Document:") << ' ' << docName(d) << "\n  "
                           << QObject::tr("Path:") << ' '
                           << QString::fromUtf8(d->FileName.getValue());
                    }
                }
                ++count;
                break;
            }
        }
    }
    int ret = QMessageBox::warning(
        getMainWindow(),
        QObject::tr("Identical physical path"),
        msg,
        QMessageBox::Yes,
        QMessageBox::No
    );
    return ret == QMessageBox::Yes;
}

bool Document::askIfSavingFailed(const QString& error)
{
    int ret = QMessageBox::question(
        getMainWindow(),
        QObject::tr("Could not save document"),
        QObject::tr(
            "There was an issue trying to save the file. "
            "This may be because some of the parent folders do not exist, "
            "or you do not have sufficient permissions, "
            "or for other reasons. Error details:\n\n\"%1\"\n\n"
            "Would you like to save the file with a different name?"
        )
            .arg(error),
        QMessageBox::Yes,
        QMessageBox::No
    );

    if (ret == QMessageBox::No) {
        // TODO: Understand what exactly is supposed to be returned here
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
    else if (ret == QMessageBox::Yes) {
        return saveAs();
    }

    return false;
}

bool Document::warnIfOlderVersion()
{
    // Skip warning if no GUI (headless/scripted mode)
    if (!getMainWindow()) {
        return true;
    }

    // Check if version checking is disabled in preferences
    if (App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
            ->GetBool("DisableVersionCheckOnSave", false)) {
        return true;
    }

    // Get document version info
    const char* docVersion = d->_pcDocument->getProgramVersion();
    const bool hasVersionString = !Base::Tools::isNullOrEmpty(docVersion);

    // Parse document version string like "1.0R39319 (Git)" or "0.21R33694 (Git)"
    // hasVersion is true only if the string is present AND parses as major.minor.
    // Unrecognised strings like "pre-0.14" still display in the dialog but cannot
    // be compared numerically, so they are treated as older versions.
    int docMajor = 0, docMinor = 0;
    const bool hasVersion = hasVersionString
        && std::sscanf(docVersion, "%d.%d", &docMajor, &docMinor) == 2;

    // Get current FreeCAD version
    auto config = App::Application::Config();
    int currentMajor = 0, currentMinor = 0;
    if (config.count("BuildVersionMajor") && config.count("BuildVersionMinor")) {
        currentMajor = std::stoi(config["BuildVersionMajor"]);
        currentMinor = std::stoi(config["BuildVersionMinor"]);
    }
    else {
        return true;
    }

    // Warn if the document was created with an older version or has no version info
    if (!hasVersion || (docMajor < currentMajor)
        || (docMajor == currentMajor && docMinor < currentMinor)) {
        QMessageBox msgBox(getMainWindow());
        msgBox.setWindowTitle(QObject::tr("File Created with Older FreeCAD Version"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(
            QObject::tr(
                "This file was created with %1, but you are using v%2.%3.\n\n"
                "Saving will upgrade the file format. The file may not be readable "
                "by older versions of FreeCAD after saving.\n\n"
                "Use 'Save As…' to preserve the original file."
                "\n"
            )
                .arg(
                    !hasVersionString
                        ? QObject::tr("an unknown older version of FreeCAD")
                        : QObject::tr("FreeCAD version %1").arg(QString::fromUtf8(docVersion))
                )
                .arg(currentMajor)
                .arg(currentMinor)
        );
        QPushButton* saveButton = msgBox.addButton(QObject::tr("Save"), QMessageBox::AcceptRole);
        QPushButton* saveAsButton = msgBox.addButton(QObject::tr("Save As…"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);

        QCheckBox dontShowCheckBox(QObject::tr("Do not show this warning again"), &msgBox);
        msgBox.setCheckBox(&dontShowCheckBox);

        int ret = msgBox.exec();

        if (msgBox.clickedButton() == saveButton) {
            if (dontShowCheckBox.isChecked()) {
                App::GetApplication()
                    .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                    ->SetBool("DisableVersionCheckOnSave", true);
            }
        }
        else if (msgBox.clickedButton() == saveAsButton) {
            saveAs();
            return false;
        }

        if (ret == QMessageBox::Cancel) {
            return false;
        }
    }
    return true;
}

/// Save the document
bool Document::save()
{
    if (d->_pcDocument->isSaved()) {
        // Warn if this document was created with an older FreeCAD version
        if (!warnIfOlderVersion()) {
            return false;
        }

        try {
            std::vector<App::Document*> docs;
            std::map<App::Document*, bool> dmap;
            try {
                docs = getDocument()->getDependentDocuments();
                for (auto it = docs.begin(); it != docs.end();) {
                    App::Document* doc = *it;
                    if (doc == getDocument()) {
                        dmap[doc] = doc->mustExecute();
                        ++it;
                        continue;
                    }
                    auto gdoc = Application::Instance->getDocument(doc);
                    if ((gdoc && !gdoc->isModified()) || doc->testStatus(App::Document::PartialDoc)
                        || doc->testStatus(App::Document::TempDoc)) {
                        it = docs.erase(it);
                        continue;
                    }
                    dmap[doc] = doc->mustExecute();
                    ++it;
                }
            }
            catch (const Base::RuntimeError& e) {
                FC_ERR(e.what());
                docs = {getDocument()};
                dmap.clear();
                dmap[getDocument()] = getDocument()->mustExecute();
            }

            if (docs.size() > 1) {
                int ret = QMessageBox::question(
                    getMainWindow(),
                    QObject::tr("Save dependent files"),
                    QObject::tr(
                        "The file contains external dependencies. "
                        "Do you want to save the dependent files, too?"
                    ),
                    QMessageBox::Yes,
                    QMessageBox::No
                );

                if (ret != QMessageBox::Yes) {
                    docs = {getDocument()};
                    dmap.clear();
                    dmap[getDocument()] = getDocument()->mustExecute();
                }
            }

            if (!checkCanonicalPath(dmap)) {
                return false;
            }

            Gui::WaitCursor wc;
            // save all documents
            for (auto doc : docs) {
                // Changed 'mustExecute' status may be triggered by saving external document
                if (!dmap[doc] && doc->mustExecute()) {
                    App::AutoTransaction trans(doc, "Recompute");
                    Command::doCommand(
                        Command::Doc,
                        "App.getDocument(\"%s\").recompute()",
                        doc->getName()
                    );
                }

                Command::doCommand(Command::Doc, "App.getDocument(\"%s\").save()", doc->getName());
                auto gdoc = Application::Instance->getDocument(doc);
                if (gdoc) {
                    gdoc->setModified(false);
                }
            }
        }
        catch (const Base::FileException& e) {
            e.reportException();
            return askIfSavingFailed(QString::fromUtf8(e.what()));
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(
                getMainWindow(),
                QObject::tr("Saving document failed"),
                QString::fromLatin1(e.what())
            );
            return false;
        }
        return true;
    }
    else {
        return saveAs();
    }
}

namespace
{
// Cruth: the save dialog offers the document's own type-derived extension first
// (a Part document defaults to .cpart), with .FCStd always available as a fallback.
FileDialog::FilterList documentSaveFilters(App::Document* doc, const QString& exe)
{
    FileDialog::FilterList filters;
    const std::string ext = doc->documentFileExtension();
    if (ext == "cpart") {
        filters.append({QObject::tr("Part document"), {QStringLiteral("*.cpart")}});
    }
    else if (ext == "cassembly") {
        filters.append({QObject::tr("Assembly document"), {QStringLiteral("*.cassembly")}});
    }
    filters.append({QObject::tr("%1 document").arg(exe), {QStringLiteral("*.FCStd")}});
    return filters;
}
}  // namespace

/// Save the document under a new file name
bool Document::saveAs()
{
    getMainWindow()->showMessage(QObject::tr("Save document under new filename…"));

    QString exe = qApp->applicationName();
    QString name = QString::fromUtf8(getDocument()->FileName.getValue());
    if (name.isEmpty()) {
        name = QString::fromUtf8(getDocument()->Label.getValue());
    }
    if (name.endsWith(QStringLiteral(".FCBak"), Qt::CaseInsensitive)) {
        name.chop(QStringLiteral(".FCBak").size());
        const QString docExt = QStringLiteral(".")
            + QString::fromStdString(getDocument()->documentFileExtension());
        if (!name.endsWith(docExt, Qt::CaseInsensitive)) {
            name += docExt;
        }
    }
    QString fn = FileDialog::getSaveFileName(
        getMainWindow(),
        QObject::tr("Save %1 Document").arg(exe),
        name,
        documentSaveFilters(getDocument(), exe)
    );

    if (!fn.isEmpty()) {
        QFileInfo fi;
        fi.setFile(fn);

        const char* DocName = App::GetApplication().getDocumentName(getDocument());

        // save as new file name
        try {
            Gui::WaitCursor wc;
            std::string escapedstr = Base::Tools::escapedUnicodeFromUtf8(fn.toUtf8());
            escapedstr = Base::Tools::escapeEncodeFilename(escapedstr);
            Command::doCommand(
                Command::Doc,
                "App.getDocument(\"%s\").saveAs(u\"%s\")",
                DocName,
                escapedstr.c_str()
            );
            // App::Document::saveAs() may modify the passed file name
            fi.setFile(QString::fromUtf8(d->_pcDocument->FileName.getValue()));
            setModified(false);
            getMainWindow()->appendRecentFile(fi.filePath());
        }
        catch (const Base::FileException& e) {
            e.reportException();
            return askIfSavingFailed(QString::fromUtf8(e.what()));
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(
                getMainWindow(),
                QObject::tr("Saving document failed"),
                QString::fromLatin1(e.what())
            );
            // Cruth (Amendment 19 Clause 19.3): a write that did not happen is never reported as
            // one that did. Measured, this returned true after the write was refused, so the
            // document was marked unmodified, added to the recent files, and the close prompt
            // accepted that as saved and threw the session's work away.
            return false;
        }
        return true;
    }
    else {
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
}

void Document::saveAll()
{
    std::vector<App::Document*> docs;
    try {
        docs = App::Document::getDependentDocuments(App::GetApplication().getDocuments(), true);
    }
    catch (Base::Exception& e) {
        e.reportException();
        int ret = QMessageBox::critical(
            getMainWindow(),
            QObject::tr("Failed to save document"),
            QObject::tr("Documents contains cyclic dependencies. Do you still want to save them?"),
            QMessageBox::Yes,
            QMessageBox::No
        );
        if (ret != QMessageBox::Yes) {
            return;
        }
        docs = App::GetApplication().getDocuments();
    }

    std::map<App::Document*, bool> dmap;
    for (auto doc : docs) {
        if (doc->testStatus(App::Document::PartialDoc) || doc->testStatus(App::Document::TempDoc)) {
            continue;
        }
        dmap[doc] = doc->mustExecute();
    }

    if (!checkCanonicalPath(dmap)) {
        return;
    }

    for (auto doc : docs) {
        if (doc->testStatus(App::Document::PartialDoc) || doc->testStatus(App::Document::TempDoc)) {
            continue;
        }
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc) {
            continue;
        }
        if (!doc->isSaved()) {
            if (!gdoc->saveAs()) {
                break;
            }
        }
        Gui::WaitCursor wc;

        try {
            // Changed 'mustExecute' status may be triggered by saving external document
            if (!dmap[doc] && doc->mustExecute()) {
                App::AutoTransaction trans(doc, "Recompute");
                Command::doCommand(Command::Doc, "App.getDocument('%s').recompute()", doc->getName());
            }
            Command::doCommand(Command::Doc, "App.getDocument('%s').save()", doc->getName());
            gdoc->setModified(false);
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(
                getMainWindow(),
                QObject::tr("Failed to save document")
                    + QStringLiteral(": %1").arg(QString::fromUtf8(doc->getName())),
                QString::fromLatin1(e.what())
            );
            break;
        }
    }
}

/// Save a copy of the document under a new file name
bool Document::saveCopy()
{
    getMainWindow()->showMessage(QObject::tr("Save a copy of the document under new filename…"));

    QString exe = qApp->applicationName();
    QString name = QString::fromUtf8(getDocument()->FileName.getValue());
    if (name.endsWith(QStringLiteral(".FCBak"), Qt::CaseInsensitive)) {
        name.chop(QStringLiteral(".FCBak").size());
        const QString docExt = QStringLiteral(".")
            + QString::fromStdString(getDocument()->documentFileExtension());
        if (!name.endsWith(docExt, Qt::CaseInsensitive)) {
            name += docExt;
        }
    }
    QString fn = FileDialog::getSaveFileName(
        getMainWindow(),
        QObject::tr("Save %1 Document").arg(exe),
        name,
        documentSaveFilters(getDocument(), exe)
    );
    if (!fn.isEmpty()) {
        const char* DocName = App::GetApplication().getDocumentName(getDocument());

        // save as new file name
        Gui::WaitCursor wc;
        std::string pyfn = Base::Tools::escapeEncodeFilename(fn.toUtf8().constData());
        try {
            Command::doCommand(
                Command::Doc,
                "App.getDocument(\"%s\").saveCopy(\"%s\")",
                DocName,
                pyfn.c_str()
            );
        }
        catch (const Base::Exception& e) {
            // A copy of a fragment is a copy of the beginning of a file, and this path reaches
            // the same guard as a save (Amendment 19 Clause 19.3). Said here rather than thrown
            // past the caller, and never reported as a copy that was written.
            QMessageBox::critical(
                getMainWindow(),
                QObject::tr("Saving document failed"),
                QString::fromLatin1(e.what())
            );
            return false;
        }

        return true;
    }
    else {
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
}

unsigned int Document::getMemSize() const
{
    unsigned int size = 0;

    // size of the view providers in the document

    for (const auto& vp : d->_ViewProviderMap) {
        size += vp.second->getMemSize();
    }
    return size;
}

/**
 * Adds a separate XML file to the projects file that contains information about the view providers.
 */
void Document::Save(Base::Writer& writer) const
{
    // It's only possible to add extra information if force of XML is disabled
    if (!writer.isForceXML()) {
        writer.addFile("GuiDocument.xml", this);

        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Document"
        );
        if (hGrp->GetBool("SaveThumbnail", true)) {
            int size = hGrp->GetInt("ThumbnailSize", 256);
            size = Base::clamp<int>(size, 64, 512);
            std::list<MDIView*> mdi = getMDIViews();

            View3DInventorViewer* view = nullptr;
            for (const auto& it : mdi) {
                if (it->isDerivedFrom<View3DInventor>()) {
                    view = static_cast<View3DInventor*>(it)->getViewer();
                    break;
                }
            }

            d->thumb.setFileName(d->_pcDocument->FileName.getValue());
            d->thumb.setSize(size);
            d->thumb.setViewer(view);
            d->thumb.Save(writer);
        }
    }
}

/**
 * Loads a separate XML file from the projects file with information about the view providers.
 */
void Document::Restore(Base::XMLReader& reader)
{
    reader.addFile("GuiDocument.xml", this);

    // hide all elements to avoid to update the 3d view when loading data files
    // RestoreDocFile then restores the visibility status again
    std::map<const App::DocumentObject*, ViewProviderDocumentObject*>::iterator it;
    for (const auto& vp : d->_ViewProviderMap) {
        vp.second->startRestoring();
        vp.second->setStatus(Gui::isRestoring, true);
    }
}

/**
 * Restores the properties of the view providers.
 */
void Document::RestoreDocFile(Base::Reader& reader)
{
    // We must create an XML parser to read from the input stream
    std::shared_ptr<Base::XMLReader> localreader
        = std::make_shared<Base::XMLReader>("GuiDocument.xml", reader);
    localreader->FileVersion = reader.getFileVersion();

    localreader->readElement("Document");
    long scheme = localreader->getAttribute<long>("SchemaVersion");
    localreader->DocumentSchema = scheme;
    localreader->ProgramVersion = d->_pcDocument->getProgramVersion();

    // Whether this file carries the appearance a person chose, or only view state. A sealed
    // archive carries it and says nothing, because every one ever written predates the question.
    // The project cache says so outright, and its appearance comes from the recipe instead --
    // which matters, because the cache is read AFTER the recipe and a copy here would win.
    const bool hasAppearance = localreader->getAttribute<long>("Appearance", 1) == 1;

    bool hasExpansion = localreader->hasAttribute("HasExpansion");
    if (hasExpansion) {
        auto tree = TreeWidget::instance();
        if (tree) {
            auto docItem = tree->getDocumentItem(this);
            if (docItem) {
                docItem->Restore(*localreader);
            }
        }
    }

    // At this stage all the document objects and their associated view providers exist.
    // Now we must restore the properties of the view providers only.
    //
    // SchemeVersion "1"
    if (scheme == 1) {
        // read the viewproviders itself
        localreader->readElement("ViewProviderData");
        int Cnt = localreader->getAttribute<long>("Count");
        for (int i = 0; i < Cnt; i++) {
            localreader->readElement("ViewProvider");
            std::string name = localreader->getAttribute<const char*>("name");

            bool expanded = false;
            if (!hasExpansion && localreader->hasAttribute("expanded")) {
                const char* attr = localreader->getAttribute<const char*>("expanded");
                if (strcmp(attr, "1") == 0) {
                    expanded = true;
                }
            }

            int treeRank = -1;
            if (localreader->hasAttribute("treeRank")) {
                treeRank = localreader->getAttribute<int>("treeRank");
            }

            auto pObj = freecad_cast<ViewProviderDocumentObject*>(getViewProviderByName(name.c_str()));
            // check if this feature has been registered
            if (pObj) {
                if (hasAppearance) {
                    pObj->Restore(*localreader);
                }
                else {
                    pObj->restoreExtensions(*localreader);
                }
            }

            if (pObj && treeRank >= 0) {
                pObj->setTreeRank(treeRank);
            }

            if (pObj && expanded) {
                this->signalExpandObject(*pObj, TreeItemMode::ExpandItem, 0, 0);
            }
            localreader->readEndElement("ViewProvider");
        }
        localreader->readEndElement("ViewProviderData");

        // read camera settings
        localreader->readElement("Camera");
        const char* ppReturn = localreader->getAttribute<const char*>("settings");
        cameraSettings.clear();
        if (!Base::Tools::isNullOrEmpty(ppReturn)) {
            saveCameraSettings(ppReturn);
            try {
                for (const auto& it : getMDIViews()) {
                    if (auto* viewCamera = freecad_cast<MDIViewWithCamera*>(it)) {
                        viewCamera->setCamera(cameraSettings.c_str());
                    }
                }
            }
            catch (const Base::Exception& e) {
                Base::Console().error("%s\n", e.what());
            }
        }
    }

    reader.initLocalReader(localreader);

    // reset modified flag
    setModified(false);
}

/** The document on screen agrees with the document on disk.
 *
 * Each of the GUI's own save commands cleared this flag for itself, so a save a person started
 * from a menu was accounted for and a save a script started was not: the file was written, and
 * the window went on saying the document had unsaved changes until closing it asked whether to
 * save work that was already saved.
 *
 * Only a save to the document's own file counts. Saving a copy writes the same content to a
 * different path and deliberately leaves the document where it was, so the name written is
 * compared with the name the document answers to rather than assumed.
 */
void Document::slotFinishSaveDocument(const App::Document& doc, const std::string& fileName)
{
    if (d->_pcDocument != &doc) {
        return;
    }
    if (fileName == doc.FileName.getStrValue()) {
        setModified(false);
    }
}

void Document::slotStartRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }
    // disable this signal while loading a document
    d->connectActObjectBlocker.block();
}

void Document::slotFinishRestoreObject(const App::DocumentObject& obj)
{
    auto vpd = freecad_cast<ViewProviderDocumentObject*>(getViewProvider(&obj));
    if (vpd) {
        vpd->setStatus(Gui::isRestoring, false);
        vpd->finishRestoring();
        if (!vpd->canAddToSceneGraph()) {
            toggleInSceneGraph(vpd);
        }
    }
}

void Document::slotFinishRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }
    d->connectActObjectBlocker.unblock();
    App::DocumentObject* act = doc.getActiveObject();
    if (act) {
        ViewProvider* viewProvider = getViewProvider(act);
        if (viewProvider && viewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
            signalActivatedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
        }
    }

    // reset modified flag
    setModified(
        doc.testStatus(App::Document::LinkStampChanged)
        || doc.testStatus(App::Document::GivenNewIdentity)
    );
}

void Document::slotShowHidden(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }

    Application::Instance->signalShowHidden(*this);
}

/**
 * Saves the properties of the view providers.
 */
void Document::SaveDocFile(Base::Writer& writer) const
{
    // The project cache, which is deletable by design and therefore may not hold anything a
    // person authored. The chosen appearance goes to the file of record instead; what is left
    // here is view state -- how this session happened to be looking at the part.
    saveDocFile(writer, /*withAppearance=*/false);
}

/// The stored form of the view layer, with or without the appearance a person chose.
///
/// The sealed archive -- what a release or a records system hands over -- is one self-contained
/// file and wants the full form, so the choice is a parameter rather than a deletion.
void Document::saveDocFile(Base::Writer& writer, bool withAppearance) const
{
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>" << std::endl
                    << "<!--" << std::endl
                    << " FreeCAD Document, see https://www.freecad.org for more information…"
                    << std::endl
                    << "-->" << std::endl;

    writer.Stream() << "<Document SchemaVersion=\"1\"";
    if (!withAppearance) {
        // Said on the file, so a reader knows which form it is holding. Absent means the full
        // form, which is what every sealed archive ever written says.
        writer.Stream() << " Appearance=\"0\"";
    }

    writer.incInd();

    auto tree = TreeWidget::instance();
    bool hasExpansion = false;
    if (tree) {
        auto docItem = tree->getDocumentItem(this);
        if (docItem) {
            hasExpansion = true;
            writer.Stream() << " HasExpansion=\"1\">" << std::endl;
            docItem->Save(writer);
        }
    }
    if (!hasExpansion) {
        writer.Stream() << ">" << std::endl;
    }

    // writing the view provider names itself
    writer.Stream() << writer.ind() << "<ViewProviderData Count=\"" << d->_ViewProviderMap.size()
                    << "\">" << std::endl;

    bool xml = writer.isForceXML();
    // writer.setForceXML(true);
    writer.incInd();  // indentation for 'ViewProvider name'
    for (const auto& it : d->_ViewProviderMap) {
        const App::DocumentObject* doc = it.first;
        ViewProviderDocumentObject* obj = it.second;
        writer.Stream() << writer.ind() << "<ViewProvider name=\"" << doc->getNameInDocument() << "\""
                        << " expanded=\"" << (doc->testStatus(App::Expand) ? 1 : 0) << "\""
                        << " treeRank=\"" << obj->getTreeRank() << "\"";
        if (obj->hasExtensions()) {
            writer.Stream() << " Extensions=\"True\"";
        }

        writer.Stream() << ">" << std::endl;
        if (withAppearance) {
            obj->Save(writer);
        }
        else {
            // The extensions only. They are a declaration of what this view provider IS rather
            // than of how it looks, and re-creating a dynamic one is not something the recipe
            // can do for us.
            obj->saveExtensions(writer);
        }
        writer.Stream() << writer.ind() << "</ViewProvider>" << std::endl;
    }
    writer.setForceXML(xml);

    writer.decInd();  // indentation for 'ViewProvider name'
    writer.Stream() << writer.ind() << "</ViewProviderData>" << std::endl;
    writer.decInd();  // indentation for 'ViewProviderData Count'

    // save camera settings
    for (const auto& it : getMDIViews()) {
        if (auto* viewCamera = freecad_cast<MDIViewWithCamera*>(it)) {
            const std::string& camera = viewCamera->getCamera();
            if (saveCameraSettings(camera.c_str())) {
                break;
            }
        }
    }

    writer.incInd();  // indentation for camera settings
    writer.Stream() << writer.ind() << "<Camera settings=\"" << encodeAttribute(getCameraSettings())
                    << "\"/>\n";
    writer.decInd();  // indentation for camera settings

    writer.Stream() << "</Document>" << std::endl;
}

void Document::exportObjects(const std::vector<App::DocumentObject*>& obj, Base::Writer& writer)
{
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>" << std::endl;
    writer.Stream() << "<Document SchemaVersion=\"1\">" << std::endl;

    std::map<const App::DocumentObject*, ViewProvider*> views;
    for (const auto& it : obj) {
        Document* doc = Application::Instance->getDocument(it->getDocument());
        if (doc) {
            ViewProvider* vp = doc->getViewProvider(it);
            if (vp) {
                views[it] = vp;
            }
        }
    }

    // writing the view provider names itself
    writer.incInd();  // indentation for 'ViewProviderData Count'
    writer.Stream() << writer.ind() << "<ViewProviderData Count=\"" << views.size() << "\">"
                    << std::endl;

    bool xml = writer.isForceXML();
    // writer.setForceXML(true);
    writer.incInd();  // indentation for 'ViewProvider name'
    std::map<const App::DocumentObject*, ViewProvider*>::const_iterator jt;
    for (jt = views.begin(); jt != views.end(); ++jt) {
        const App::DocumentObject* doc = jt->first;
        ViewProvider* vp = jt->second;
        writer.Stream() << writer.ind() << "<ViewProvider name=\"" << doc->getExportName() << "\" "
                        << "expanded=\"" << (doc->testStatus(App::Expand) ? 1 : 0) << "\"";
        if (vp->hasExtensions()) {
            writer.Stream() << " Extensions=\"True\"";
        }

        writer.Stream() << ">" << std::endl;
        vp->Save(writer);
        writer.Stream() << writer.ind() << "</ViewProvider>" << std::endl;
    }
    writer.setForceXML(xml);

    writer.decInd();  // indentation for 'ViewProvider name'
    writer.Stream() << writer.ind() << "</ViewProviderData>" << std::endl;
    writer.decInd();  // indentation for 'ViewProviderData Count'
    writer.incInd();  // indentation for camera settings
    writer.Stream() << writer.ind() << "<Camera settings=\"\"/>" << std::endl;
    writer.decInd();  // indentation for camera settings
    writer.Stream() << "</Document>" << std::endl;
}

void Document::importObjects(
    const std::vector<App::DocumentObject*>& obj,
    Base::Reader& reader,
    const std::map<std::string, std::string>& nameMapping
)
{
    // We must create an XML parser to read from the input stream
    std::shared_ptr<Base::XMLReader> localreader
        = std::make_shared<Base::XMLReader>("GuiDocument.xml", reader);
    localreader->readElement("Document");
    long scheme = localreader->getAttribute<long>("SchemaVersion");

    // At this stage all the document objects and their associated view providers exist.
    // Now we must restore the properties of the view providers only.
    //
    // SchemeVersion "1"
    if (scheme == 1) {
        // read the viewproviders itself
        localreader->readElement("ViewProviderData");
        int Cnt = localreader->getAttribute<long>("Count");
        auto it = obj.begin();
        for (int i = 0; i < Cnt && it != obj.end(); ++i, ++it) {
            // The stored name usually doesn't match with the current name anymore
            // thus we try to match by type. This should work because the order of
            // objects should not have changed
            localreader->readElement("ViewProvider");
            std::string name = localreader->getAttribute<const char*>("name");
            auto jt = nameMapping.find(name);
            if (jt != nameMapping.end()) {
                name = jt->second;
            }
            bool expanded = false;
            if (localreader->hasAttribute("expanded")) {
                const char* attr = localreader->getAttribute<const char*>("expanded");
                if (strcmp(attr, "1") == 0) {
                    expanded = true;
                }
            }
            Gui::ViewProvider* pObj = this->getViewProviderByName(name.c_str());
            if (pObj) {
                pObj->setStatus(Gui::isRestoring, true);
                auto vpd = freecad_cast<ViewProviderDocumentObject*>(pObj);
                if (vpd) {
                    vpd->startRestoring();
                }
                pObj->Restore(*localreader);
                if (expanded && vpd) {
                    this->signalExpandObject(*vpd, TreeItemMode::ExpandItem, 0, 0);
                }
            }
            localreader->readEndElement("ViewProvider");
            if (it == obj.end()) {
                break;
            }
        }
        localreader->readEndElement("ViewProviderData");
    }

    localreader->readEndElement("Document");

    // In the file GuiDocument.xml new data files might be added
    if (localreader->hasFilenames()) {
        reader.initLocalReader(localreader);
    }
}

void Document::slotFinishImportObjects(const std::vector<App::DocumentObject*>& objs)
{
    (void)objs;
    // finishRestoring() is now triggered by signalFinishRestoreObject
    //
    // for(auto obj : objs) {
    //     auto vp = getViewProvider(obj);
    //     if(!vp) continue;
    //     vp->setStatus(Gui::isRestoring,false);
    //     auto vpd = freecad_cast<ViewProviderDocumentObject*>(vp);
    //     if(vpd) vpd->finishRestoring();
    // }
}

void Document::addRootObjectsToGroup(
    const std::vector<App::DocumentObject*>& obj,
    App::DocumentObjectGroup* grp
)
{
    std::map<App::DocumentObject*, bool> rootMap;
    for (const auto it : obj) {
        rootMap[it] = true;
    }
    // get the view providers and check which objects are children
    for (const auto it : obj) {
        Gui::ViewProvider* vp = getViewProvider(it);
        if (vp) {
            std::vector<App::DocumentObject*> child = vp->claimChildren();
            for (const auto& jt : child) {
                auto kt = rootMap.find(jt);
                if (kt != rootMap.end()) {
                    kt->second = false;
                }
            }
        }
    }

    // all objects that are not children of other objects can be added to the group
    for (const auto& it : rootMap) {
        if (it.second) {
            grp->addObject(it.first);
        }
    }
}
