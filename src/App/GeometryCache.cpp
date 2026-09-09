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
#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <sstream>
#include <vector>
#endif

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "GeometryCache.h"
#include "Property.h"
#include "StoredRecipe.h"

using namespace App;

namespace
{

namespace fs = std::filesystem;

/// The shape of a cache entry. Bumped when this file changes how an entry is written, which is
/// the one thing an entry cannot describe about itself.
constexpr const char* entryFormat = "1";

/// What the recipe text cannot say: which program built the result.
///
/// A feature's geometry comes from its recipe AND from the code that reads it, so a release that
/// builds a fillet differently would otherwise be handed back the old fillet by a cache that
/// still saw matching text. The release version is the honest coarse answer -- it changes when
/// the program people are running changes -- and it deliberately does not change per commit,
/// which would empty the cache on every build during development for no gain.
///
/// It is NOT a complete answer, and this is the known limit of the whole mechanism: two builds of
/// the same release that produce different geometry are indistinguishable here. That is a bug in
/// the build rather than a case to be tolerated, and `entryFormat` above is the manual lever for
/// the day one is shipped anyway.
std::string buildStamp()
{
    const auto& config = App::Application::Config();
    const auto major = config.find("BuildVersionMajor");
    const auto minor = config.find("BuildVersionMinor");
    std::string stamp = "cruth-";
    stamp += major != config.end() ? major->second : "0";
    stamp += ".";
    stamp += minor != config.end() ? minor->second : "0";
    stamp += "/entry-";
    stamp += entryFormat;
    return stamp;
}

std::string digestOf(const std::string& text)
{
    const QByteArray digest =
        QCryptographicHash::hash(QByteArray(text.data(), static_cast<int>(text.size())),
                                 QCryptographicHash::Sha1);
    return QString::fromLatin1(digest.toHex()).toStdString();
}

/// The digest of one object, and of everything it stands on.
///
/// An object's own recipe block says what it was asked to be; the keys of the objects it is built
/// on say what it was asked to be built ON. Both are needed: a pad whose own text is untouched is
/// still a different pad once the sketch beneath it moves. Recursion is what carries a change all
/// the way up a chain of features, and memoising it is what keeps a hundred features a hundred
/// digests rather than a hundred walks of the whole graph.
std::string keyFor(const DocumentObject& obj,
                   const std::string& assetDirectory,
                   std::map<const DocumentObject*, std::string>& known,
                   std::vector<const DocumentObject*>& onPath)
{
    const auto seen = known.find(&obj);
    if (seen != known.end()) {
        return seen->second;
    }
    // A cycle has no bottom to start from, so there is no honest digest to give. The object is
    // reported unidentifiable and simply gets rebuilt, which is what happened before this
    // existed.
    if (std::find(onPath.begin(), onPath.end(), &obj) != onPath.end()) {
        return {};
    }
    onPath.push_back(&obj);

    std::string material = buildStamp();
    material += "\n";
    try {
        material += formatStoredRecipeObject(obj, assetDirectory);
    }
    catch (const Base::Exception&) {
        onPath.pop_back();
        known[&obj] = {};
        return {};
    }

    // In durable-id order, so that the order the links happen to be stored in cannot rename an
    // entry that describes the same thing.
    std::map<std::string, std::string> inputs;
    for (const DocumentObject* input : obj.getOutList()) {
        if (input == nullptr || input == &obj) {
            continue;
        }
        const std::string inputKey = keyFor(*input, assetDirectory, known, onPath);
        if (inputKey.empty()) {
            // Built on something that cannot be identified, so neither can this.
            onPath.pop_back();
            known[&obj] = {};
            return {};
        }
        inputs[input->Uid.getValueStr()] = inputKey;
    }
    for (const auto& [uuid, inputKey] : inputs) {
        material += "\nbuilt-on " + uuid + " " + inputKey;
    }

    onPath.pop_back();
    const std::string key = digestOf(material);
    known[&obj] = key;
    return key;
}

fs::path entryPath(const std::string& cacheDirectory, const std::string& key)
{
    return fs::path(cacheDirectory) / "geometry" / key;
}

const char* contentFile = "content.xml";

}  // namespace

std::string App::builtGeometryKey(const DocumentObject& obj, const std::string& assetDirectory)
{
    std::map<const DocumentObject*, std::string> known;
    std::vector<const DocumentObject*> onPath;
    return keyFor(obj, assetDirectory, known, onPath);
}

void App::storeBuiltGeometry(const Document& doc,
                             const std::string& cacheDirectory,
                             const std::string& assetDirectory)
{
    if (cacheDirectory.empty()) {
        return;
    }

    std::map<const DocumentObject*, std::string> known;
    std::vector<const DocumentObject*> onPath;

    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        const std::vector<Property*> rebuilt = rebuiltProperties(*obj);
        if (rebuilt.empty()) {
            continue;
        }
        const std::string key = keyFor(*obj, assetDirectory, known, onPath);
        if (key.empty()) {
            continue;
        }

        const fs::path entry = entryPath(cacheDirectory, key);
        std::error_code failed;
        if (fs::exists(entry / contentFile, failed)) {
            // The name is the content: an entry that is there is already this result.
            continue;
        }

        try {
            fs::create_directories(entry);
            Base::FileWriter writer(entry.string().c_str());
            writer.putNextEntry(contentFile);
            writer.Stream().precision(std::numeric_limits<double>::max_digits10);
            writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                            << "<Built Count=\"" << rebuilt.size() << "\">\n";
            writer.incInd();
            for (const Property* prop : rebuilt) {
                writer.Stream() << writer.ind() << "<Property name=\"" << prop->getName()
                                << "\" type=\"" << prop->getTypeId().getName() << "\">\n";
                prop->Save(writer);
                writer.Stream() << writer.ind() << "</Property>\n";
            }
            writer.decInd();
            writer.Stream() << "</Built>\n";
            // Closes the entry and writes out the values large enough to be kept beside it --
            // the solids, which is most of what this is for.
            writer.writeFiles();
        }
        catch (const std::exception& e) {
            // A cache that cannot be written costs a rebuild, never a design. The document
            // itself has already been saved and must not be undone by this.
            Base::Console().warning("Could not keep the built geometry for %s: %s\n",
                                    obj->getNameInDocument() != nullptr ? obj->getNameInDocument()
                                                                        : "",
                                    e.what());
        }
    }
}

std::set<DocumentObject*> App::restoreBuiltGeometry(Document& doc,
                                                    const std::string& cacheDirectory,
                                                    const std::string& assetDirectory)
{
    std::set<DocumentObject*> toBuild;
    std::vector<DocumentObject*> restored;

    std::map<const DocumentObject*, std::string> known;
    std::vector<const DocumentObject*> onPath;

    for (DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        const std::vector<Property*> rebuilt = rebuiltProperties(*obj);
        if (rebuilt.empty()) {
            // Nothing a rebuild would leave behind, so there is nothing to keep and nothing to
            // do -- the same answer the document archive gave, which never recomputed on open.
            continue;
        }

        const std::string key =
            cacheDirectory.empty() ? std::string {} : keyFor(*obj, assetDirectory, known, onPath);
        const fs::path entry = entryPath(cacheDirectory, key);
        std::error_code failed;
        if (key.empty() || !fs::exists(entry / contentFile, failed)) {
            toBuild.insert(obj);
            continue;
        }

        try {
            Base::FileInfo file((entry / contentFile).string());
            Base::ifstream stream(file, std::ios::in | std::ios::binary);
            Base::XMLReader reader(contentFile, stream);
            if (!reader.isValid()) {
                toBuild.insert(obj);
                continue;
            }

            // Handed its own result, an object must not react by producing it again: some
            // features rebuild themselves whenever a property changes, and one of them is the
            // reason this cache exists.
            obj->setStatus(ObjectStatus::Restore, true);
            reader.readElement("Built");
            const int count = reader.getAttribute<long>("Count");
            for (int i = 0; i < count; ++i) {
                reader.readElement("Property");
                const std::string name = reader.getAttribute<const char*>("name");
                const std::string type = reader.getAttribute<const char*>("type");
                Property* prop = obj->getPropertyByName(name.c_str());
                if (prop != nullptr && prop->getTypeId().getName() == type) {
                    prop->Restore(reader);
                }
                reader.readEndElement("Property");
            }
            reader.readEndElement("Built");
            // The values kept beside the entry -- the solids -- are asked for by name, and one
            // that is not there is simply absent.
            reader.readFiles(entry.string());
            obj->setStatus(ObjectStatus::Restore, false);
            restored.push_back(obj);
        }
        catch (const std::exception& e) {
            obj->setStatus(ObjectStatus::Restore, false);
            // A half-read entry is not a half-built object: what follows rebuilds it outright,
            // so the only cost is the one that was going to be paid anyway.
            Base::Console().warning("Could not reuse the built geometry for %s: %s\n",
                                    obj->getNameInDocument() != nullptr ? obj->getNameInDocument()
                                                                        : "",
                                    e.what());
            toBuild.insert(obj);
        }
    }

    // Purged only once every entry has been read. Giving an object its shape back marks what
    // stands on it as needing attention, so an object settled the moment it was restored would
    // be unsettled again by the next one read.
    for (DocumentObject* obj : restored) {
        if (toBuild.find(obj) == toBuild.end()) {
            obj->purgeTouched();
        }
    }

    return toBuild;
}
