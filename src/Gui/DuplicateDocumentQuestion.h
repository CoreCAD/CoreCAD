// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Cruth contributors

#pragma once

#include <App/Services.h>

namespace Gui
{

class DuplicateDocumentQuestion final: public App::DuplicateDocumentQuestion
{
public:
    App::DuplicateAnswer ask(const std::string& copyPath, const App::Document& open) const override;
};

}  // namespace Gui
