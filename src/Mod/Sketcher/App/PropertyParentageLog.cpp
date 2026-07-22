// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <sstream>
# include <string>
#endif

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <Base/Exception.h>
#include <Base/PyObjectBase.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "PropertyParentageLog.h"

using namespace Sketcher;

TYPESYSTEM_SOURCE(Sketcher::PropertyParentageLog, App::Property)

namespace
{

const char* opToString(ParentageOp op)
{
    switch (op) {
        case ParentageOp::Split:
            return "Split";
        case ParentageOp::TrimSever:
            return "TrimSever";
        case ParentageOp::Join:
            return "Join";
    }
    return "Split";
}

ParentageOp opFromString(const std::string& s)
{
    if (s == "TrimSever") {
        return ParentageOp::TrimSever;
    }
    if (s == "Join") {
        return ParentageOp::Join;
    }
    return ParentageOp::Split;
}

// The uuid lists are already sorted by the caller, so join preserves that order.
std::string joinUuids(const std::vector<boost::uuids::uuid>& ids)
{
    std::string out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += boost::uuids::to_string(ids[i]);
    }
    return out;
}

std::vector<boost::uuids::uuid> splitUuids(const std::string& s)
{
    std::vector<boost::uuids::uuid> out;
    boost::uuids::string_generator gen;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        out.push_back(gen(tok));
    }
    return out;
}

// A stable, content-derived ordering: op, then children, then parents. Meaningless
// on purpose — it carries no judgement about which child is the continuation.
bool entryLess(const ParentageEntry& a, const ParentageEntry& b)
{
    if (a.op != b.op) {
        return a.op < b.op;
    }
    if (a.children != b.children) {
        return a.children < b.children;
    }
    return a.parents < b.parents;
}

}  // namespace

void PropertyParentageLog::recordEvent(
    ParentageOp op,
    std::vector<boost::uuids::uuid> parents,
    std::vector<boost::uuids::uuid> children
)
{
    aboutToSetValue();
    ParentageEntry entry;
    entry.op = op;
    entry.parents = std::move(parents);
    entry.children = std::move(children);
    entries.push_back(std::move(entry));
    hasSetValue();
}

void PropertyParentageLog::Save(Base::Writer& writer) const
{
    // The log carries no order of its own; serialise in a stable, content-derived
    // order so a shuffled log writes byte-identically.
    std::vector<ParentageEntry> sorted = entries;
    for (auto& entry : sorted) {
        std::sort(entry.parents.begin(), entry.parents.end());
        std::sort(entry.children.begin(), entry.children.end());
    }
    std::sort(sorted.begin(), sorted.end(), entryLess);

    writer.Stream() << writer.ind() << "<ParentageLog count=\"" << sorted.size() << "\">\n";
    writer.incInd();
    for (const auto& entry : sorted) {
        writer.Stream() << writer.ind() << "<Entry op=\"" << opToString(entry.op) << "\" parents=\""
                        << joinUuids(entry.parents) << "\" children=\"" << joinUuids(entry.children)
                        << "\"/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</ParentageLog>\n";
}

void PropertyParentageLog::Restore(Base::XMLReader& reader)
{
    reader.readElement("ParentageLog");
    const int count = reader.getAttribute<long>("count");

    std::vector<ParentageEntry> restored;
    restored.reserve(count);
    for (int i = 0; i < count; ++i) {
        reader.readElement("Entry");
        ParentageEntry entry;
        entry.op = opFromString(reader.getAttribute<const char*>("op"));
        entry.parents = splitUuids(reader.getAttribute<const char*>("parents"));
        entry.children = splitUuids(reader.getAttribute<const char*>("children"));
        restored.push_back(std::move(entry));
    }
    reader.readEndElement("ParentageLog");

    aboutToSetValue();
    entries = std::move(restored);
    hasSetValue();
}

App::Property* PropertyParentageLog::Copy() const
{
    auto* copy = new PropertyParentageLog();
    copy->entries = entries;
    return copy;
}

void PropertyParentageLog::Paste(const App::Property& from)
{
    aboutToSetValue();
    entries = static_cast<const PropertyParentageLog&>(from).entries;
    hasSetValue();
}

unsigned int PropertyParentageLog::getMemSize() const
{
    unsigned int size = sizeof(*this);
    for (const auto& entry : entries) {
        size += static_cast<unsigned int>(
            sizeof(entry) + (entry.parents.size() + entry.children.size()) * sizeof(boost::uuids::uuid)
        );
    }
    return size;
}

void PropertyParentageLog::mintDurableIdentity()
{
    if (entries.empty()) {
        return;
    }
    aboutToSetValue();
    entries.clear();
    hasSetValue();
}

PyObject* PropertyParentageLog::getPyObject()
{
    // No consumer exists yet; the record is inspected from C++ (getEntries).
    Py_RETURN_NONE;
}

void PropertyParentageLog::setPyObject(PyObject*)
{
    throw Base::AttributeError("Sketcher parentage log is read-only");
}
