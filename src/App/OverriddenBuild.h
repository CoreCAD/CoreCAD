// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <set>
#include <vector>

#include <FCGlobal.h>

#include "ObjectIdentifier.h"

namespace App
{

class DocumentObject;

/// Builds an object again as it would be with some values replaced (ARCHITECTURE §7.7: the
/// values are read as overrides; the steps themselves are untouched). Only the object and the
/// steps it is built from that read a replaced value are rebuilt. When this goes away, every
/// value is put back and those steps are rebuilt as they were.
class AppExport OverriddenBuild
{
public:
    /// Throws Base::ValueError when a target is not something @p result is built from.
    OverriddenBuild(DocumentObject* result, std::vector<ObjectIdentifier> targets);
    ~OverriddenBuild();

    OverriddenBuild(const OverriddenBuild&) = delete;
    OverriddenBuild& operator=(const OverriddenBuild&) = delete;
    OverriddenBuild(OverriddenBuild&&) = delete;
    OverriddenBuild& operator=(OverriddenBuild&&) = delete;

    /// Rebuilds with each target set to the value at the same position. Throws
    /// Base::RuntimeError naming the step that failed.
    void build(const std::vector<App::any>& values);

private:
    void rebuild(const std::vector<App::any>& values);

    DocumentObject* result;
    std::vector<ObjectIdentifier> targets;
    std::vector<App::any> asStated;
    std::vector<DocumentObject*> steps;
    std::set<DocumentObject*> alreadyTouched;
    bool overridden {false};
};

}  // namespace App
