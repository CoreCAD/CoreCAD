// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#ifndef PART_SPATIALINTERFERENCE_H
#define PART_SPATIALINTERFERENCE_H

#include <utility>
#include <vector>

#include <Mod/Part/PartGlobal.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Part
{

class TopoShape;

/**
 * True when two shapes share positive volume.
 *
 * Set intersection of the two solids. A boolean common yields an empty compound
 * for disjoint solids, and a zero-volume face or edge for two shapes merely
 * touching along a surface -- both correctly read as no overlap, because parts
 * are expected to touch. A boolean failure reads as no overlap rather than
 * propagating: this answers a question the UI asked, and cannot become the
 * user's problem.
 */
PartExport bool sharesVolume(const TopoShape& first, const TopoShape& second);

/**
 * The solids in @p doc that stand on their own.
 *
 * An object counts when it carries at least one solid and no other shape-carrying
 * object in the document consumes it. That second condition is what keeps the
 * answer to "what solid things are in this document" from double-counting: the
 * operands of a boolean overlap by design and are answered for by their result;
 * the features of a body are answered for by the body; a sketch carries no solid
 * at all. What is left is the set a user would point at and call the parts --
 * bodies, imported geometry, standalone shapes alike.
 *
 * Nothing here knows what kind of object it is looking at, which is the point:
 * an imported part is as much a solid in the document as a modelled body.
 */
PartExport std::vector<App::DocumentObject*> independentSolids(App::Document* doc);

/**
 * Every unordered pair of independent solids in @p doc that share volume.
 *
 * Two solids may occupy the same space without being merged (ARCHITECTURE §4.8):
 * valid geometry, and almost always unintended. Pure geometry -- no state read,
 * no recompute touched -- because per §8.6 this is a non-blocking notice the UI
 * raises on demand, never a recompute failure. Bounding boxes pre-reject
 * far-apart candidates; the pairwise boolean is too costly to run on everything.
 *
 * §8.6 speaks of Bodies, and asking every independent solid asks the same thing:
 * §4.6 spawns a Body for every connected solid a feature produces, and §7.8 rules
 * that imported geometry belongs to one rather than sitting loose at the top of the
 * document. A solid no Body claims is not something the model admits -- it exists
 * only because auto-spawn for imports is unbuilt -- so this reads the geometry
 * rather than the bookkeeping, and gives the same answer once that is built.
 */
PartExport std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> overlappingPairs(
    App::Document* doc
);

}  // namespace Part

#endif  // PART_SPATIALINTERFERENCE_H
