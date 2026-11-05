# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# A script can ask what a step directly uses and what directly uses it, with a Body read
# through to the step it shows (Amendment 20, Clause 20.2 rule 2).

import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square


def _uids(objects):
    return {obj.Uid for obj in objects}


class TestStepDependencies(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestStepDependencies", type="Part")
        self.padA1 = PartDesign.makeFeature(_square(self.Doc, "A1"), "Pad")
        self.bodyA = PartDesign.findBodyOf(self.padA1)
        self.sketchA2 = _square(self.Doc, "A2", side=5)
        self.padA2 = PartDesign.makeFeature(self.sketchA2, "Pad", body=self.bodyA)
        self.padA2.Length = 20

        self.padB = PartDesign.makeFeature(_square(self.Doc, "B", x0=5), "Pad")
        self.bodyB = PartDesign.findBodyOf(self.padB)
        self.boolean = self.Doc.addObject("PartDesign::Boolean", "Boolean")
        self.bodyB.addFeature(self.boolean)
        self.boolean.Tools = [self.bodyA]
        self.Doc.recompute()

    def testStepUsesItsBaseAndProfile(self):
        used = _uids(PartDesign.stepsUsedBy(self.padA2))
        self.assertIn(self.padA1.Uid, used)
        self.assertIn(self.sketchA2.Uid, used)

    def testBodyBaseDoesNotReachThatBodysUsers(self):
        base = self.Doc.addObject("Part::Box", "Base")
        self.bodyA.BaseFeature = base
        self.Doc.recompute()
        self.assertIn(self.bodyA.Uid, _uids(base.InList))
        self.assertNotIn(self.boolean.Uid, _uids(PartDesign.stepsUsing(base)))

    def testUsedBodyIsReadAsItsShownStep(self):
        used = _uids(PartDesign.stepsUsedBy(self.boolean))
        self.assertIn(self.padA2.Uid, used)
        self.assertIn(self.padB.Uid, used)
        self.assertNotIn(self.bodyA.Uid, used)

    def testStepShownByABodyReachesThatBodysUsers(self):
        users = _uids(PartDesign.stepsUsing(self.padA2))
        self.assertIn(self.boolean.Uid, users)
        self.assertNotIn(self.bodyA.Uid, users)

    def testEarlierStepOfABodyDoesNotReachThatBodysUsers(self):
        users = _uids(PartDesign.stepsUsing(self.padA1))
        self.assertIn(self.padA2.Uid, users)
        self.assertNotIn(self.boolean.Uid, users)

    def testUsesAndUsedByAgree(self):
        steps = [o for o in self.Doc.Objects if not o.isDerivedFrom("PartDesign::Body")]
        for step in steps:
            for used in PartDesign.stepsUsedBy(step):
                self.assertIn(step.Uid, _uids(PartDesign.stepsUsing(used)), (step.Name, used.Name))
            for user in PartDesign.stepsUsing(step):
                self.assertIn(step.Uid, _uids(PartDesign.stepsUsedBy(user)), (step.Name, user.Name))

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)
