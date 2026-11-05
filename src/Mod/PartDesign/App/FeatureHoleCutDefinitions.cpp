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

#include <iostream>

#include <App/Application.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>

#include "FeatureHole.h"
#include "json.hpp"

namespace PartDesign
{

void Hole::addCutType(const CutDimensionSet& dimensions)
{
    const CutDimensionSet::ThreadType thread = dimensions.thread_type;
    const std::string& name = dimensions.name;

    std::vector<std::string>* list;
    switch (thread) {
        case CutDimensionSet::Metric:
            HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricProfile", name), dimensions);
            list = &HoleCutType_ISOmetric_Enums;
            break;
        case CutDimensionSet::MetricFine:
            HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricFineProfile", name), dimensions);
            list = &HoleCutType_ISOmetricfine_Enums;
            break;
        default:
            return;
    }
    // add the collected lists of JSON definitions to the lists
    // if a name doesn't already exist in the list
    if (std::all_of(list->begin(), list->end(), [name](const std::string& x) { return x != name; })) {
        list->push_back(name);
    }
}

bool Hole::isDynamicCounterbore(const std::string& thread, const std::string& holeCutType)
{
    CutDimensionKey key {thread, holeCutType};
    return HoleCutTypeMap.count(key)
        && HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Counterbore;
}

bool Hole::isDynamicCountersink(const std::string& thread, const std::string& holeCutType)
{
    CutDimensionKey key {thread, holeCutType};
    return HoleCutTypeMap.count(key)
        && HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Countersink;
}

/*
 * Counter Dimensions
 */

const Hole::CounterBoreDimension Hole::CounterBoreDimension::nothing {"None", 0.0, 0.0};
const Hole::CounterSinkDimension Hole::CounterSinkDimension::nothing {"None", 0.0};

void Hole::calculateAndSetCounterbore()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = (1.5 * threadDiameter) + 1.0;
    double k = threadDiameter;

    HoleCutDiameter.setValue(dk);
    HoleCutDepth.setValue(k);
}

void Hole::calculateAndSetCountersink()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = 2.24 * threadDiameter;

    HoleCutDiameter.setValue(dk);
    ProfileBased::onChanged(&HoleCutDiameter);
}


Hole::CutDimensionKey::CutDimensionKey(const std::string& t, const std::string& c)
    : thread_type {t}
    , cut_name {c}
{}

bool Hole::CutDimensionKey::operator<(const CutDimensionKey& b) const
{
    return thread_type < b.thread_type || (thread_type == b.thread_type && cut_name < b.cut_name);
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const std::string& t, const std::string& c)
{
    return HoleCutTypeMap.find(CutDimensionKey(t, c))->second;
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const CutDimensionKey& k)
{
    return HoleCutTypeMap.find(k)->second;
}

Hole::CutDimensionSet::CutDimensionSet(
    const std::string& nme,
    std::vector<CounterBoreDimension>&& d,
    CutType cut,
    ThreadType thread,
    double a
)
    : bore_data {std::move(d)}
    , cut_type {cut}
    , thread_type {thread}
    , name {nme}
    , angle {a}
{}

Hole::CutDimensionSet::CutDimensionSet(
    const std::string& nme,
    std::vector<CounterSinkDimension>&& d,
    CutType cut,
    ThreadType thread,
    double a
)
    : sink_data {std::move(d)}
    , cut_type {cut}
    , thread_type {thread}
    , name {nme}
    , angle {a}
{}

const Hole::CounterBoreDimension& Hole::CutDimensionSet::get_bore(const std::string& t) const
{
    auto i = std::find_if(bore_data.begin(), bore_data.end(), [t](const Hole::CounterBoreDimension& x) {
        return x.thread == t;
    });
    if (i == bore_data.end()) {
        return CounterBoreDimension::nothing;
    }
    else {
        return *i;
    }
}

const Hole::CounterSinkDimension& Hole::CutDimensionSet::get_sink(const std::string& t) const
{
    auto i = std::find_if(sink_data.begin(), sink_data.end(), [t](const Hole::CounterSinkDimension& x) {
        return x.thread == t;
    });
    if (i == sink_data.end()) {
        return CounterSinkDimension::nothing;
    }
    else {
        return *i;
    }
}

void from_json(const nlohmann::json& j, Hole::CounterBoreDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
    t.depth = j["depth"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CounterSinkDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CutDimensionSet& t)
{
    t.name = j["name"].get<std::string>();

    std::string thread_type_string = j["thread_type"].get<std::string>();
    if (thread_type_string == "metric") {
        t.thread_type = Hole::CutDimensionSet::Metric;
    }
    else if (thread_type_string == "metricfine") {
        t.thread_type = Hole::CutDimensionSet::MetricFine;
    }
    else {
        throw Base::IndexError(std::string("Thread type '") + thread_type_string + "' unsupported");
    }

    std::string cut_type_string = j["cut_type"].get<std::string>();
    if (cut_type_string == "counterbore") {
        t.cut_type = Hole::CutDimensionSet::Counterbore;
        t.bore_data = j["data"].get<std::vector<Hole::CounterBoreDimension>>();
        t.angle = 0.0;
    }
    else if (cut_type_string == "countersink") {
        t.cut_type = Hole::CutDimensionSet::Countersink;
        t.sink_data = j["data"].get<std::vector<Hole::CounterSinkDimension>>();
        t.angle = j["angle"].get<double>();
    }
    else {
        throw Base::IndexError(std::string("Cut type '") + cut_type_string + "' unsupported");
    }

    t.name = j["name"].get<std::string>();
}

void Hole::readCutDefinitions()
{
    std::vector<std::string> dirs {
        ::App::Application::getResourceDir() + "Mod/PartDesign/Resources/Hole",
        ::App::Application::getUserAppDataDir() + "PartDesign/Hole"
    };

    std::clog << "Looking for thread definitions in: ";
    for (auto& i : dirs) {
        std::clog << i << " ";
    }
    std::clog << "\n";
    for (auto& dir : dirs) {
        std::vector<::Base::FileInfo> files {::Base::FileInfo(dir).getDirectoryContent()};
        for (const auto& f : files) {
            if (f.extension() == "json") {
                try {
                    Base::ifstream input(f);
                    nlohmann::json j;
                    input >> j;
                    CutDimensionSet screwtype = j.get<CutDimensionSet>();
                    addCutType(screwtype);
                }
                catch (std::exception& e) {
                    std::cerr << "Failed reading '" << f.filePath() << "' with: " << e.what() << "\n";
                }
            }
        }
    }
}

}  // namespace PartDesign
