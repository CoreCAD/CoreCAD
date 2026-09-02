// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <string>
#endif

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include "Feature.h"

using namespace Import;

PROPERTY_SOURCE(Import::Feature, Part::Feature)

Feature::Feature()
{
    ADD_PROPERTY_TYPE(SourceFile, (""), "Import", App::Prop_None, "File this geometry was translated from");
    ADD_PROPERTY_TYPE(
        SourceHash,
        (""),
        "Import",
        App::Prop_ReadOnly,
        "Fingerprint of the source file's contents, as read"
    );
    ADD_PROPERTY_TYPE(
        TranslatorSettings,
        (),
        "Import",
        App::Prop_ReadOnly,
        "Translator options this geometry was read under"
    );
}

std::string Feature::hashFile(const char* path)
{
    if (!path || *path == '\0') {
        return {};
    }

    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha1);
    if (!hasher.addData(&file)) {
        return {};
    }

    const QByteArray digest = hasher.result().toHex();
    return {digest.constData(), static_cast<std::size_t>(digest.size())};
}

bool Feature::refreshSourceHash()
{
    const std::string digest = hashFile(SourceFile.getValue());
    if (digest == SourceHash.getStrValue()) {
        return false;
    }

    SourceHash.setValue(digest);
    return true;
}
