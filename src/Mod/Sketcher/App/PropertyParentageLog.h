// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Trait 1, brick three: a persisted record of authored sketch-entity parentage.
// When a topology event retires one durable entity identity and mints others
// (an open-curve split, a trim that severs, a join), the succession is recorded
// here so a future three-way merge can go fine-grained across the split instead
// of degrading to modify/delete. The record is ancestry only, never identity and
// never a resolution key (architecture Amendment 10; the durable handles it names
// are Amendment 11's).

#pragma once

#include <vector>

#include <boost/uuid/uuid.hpp>

#include <App/Property.h>
#include <Mod/Sketcher/SketcherGlobal.h>

namespace Base
{
class Writer;
class XMLReader;
}  // namespace Base

namespace Sketcher
{

/// The kind of topology event that retired one identity and minted others.
enum class ParentageOp
{
    Split,      ///< open-curve split: one entity becomes N
    TrimSever,  ///< a trim that severs an entity in two
    Join,       ///< join / fuse: N entities become one
};

/// One recorded succession: the retired parents, the children minted from them,
/// and the operation that did it. Ancestry only.
struct ParentageEntry
{
    std::vector<boost::uuids::uuid> parents;
    std::vector<boost::uuids::uuid> children;
    ParentageOp op = ParentageOp::Split;
};

/// A persisted, transactional log of authored sketch-entity parentage.
///
/// Written at the retire-and-mint topology events of architecture §4.7; records
/// nothing at identity-preserving edits. It is data the document carries (not a
/// document object), so no object-admission rule reaches it. It carries no order
/// and is serialised in a stable, content-derived order, so a shuffled log writes
/// byte-identically. It empties when the owning sketch is duplicated, because a
/// copy's entities descend from nothing in their new home. It has no Python
/// binding yet: no consumer exists (Amendment 10's resolution and merge thirds are
/// deferred to their own amendments).
class SketcherExport PropertyParentageLog: public App::Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyParentageLog() = default;
    ~PropertyParentageLog() override = default;

    /// Append one succession. Transactional: written inside the current user
    /// action's transaction and reverted with it on undo.
    void recordEvent(
        ParentageOp op,
        std::vector<boost::uuids::uuid> parents,
        std::vector<boost::uuids::uuid> children
    );

    const std::vector<ParentageEntry>& getEntries() const
    {
        return entries;
    }
    bool isEmpty() const
    {
        return entries.empty();
    }

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;

    Property* Copy() const override;
    void Paste(const App::Property& from) override;
    unsigned int getMemSize() const override;

    /// Empty the log on duplication of the owning object (Amendment 10, Clause 10.2).
    void mintDurableIdentity() override;

    PyObject* getPyObject() override;
    void setPyObject(PyObject*) override;

private:
    std::vector<ParentageEntry> entries;
};

}  // namespace Sketcher
