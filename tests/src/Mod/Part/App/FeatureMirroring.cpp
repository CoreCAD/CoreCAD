// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>
#include <Mod/Part/App/FeatureMirroring.h>

#include "PartTestHelpers.h"

using namespace PartTestHelpers;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
class FeatureMirroringTest: public ::testing::Test, public PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
        _mirror = _doc->addObject<Part::Mirroring>();
        _mirror->Source.setValue(_boxes[0]);
        _mirror->Base.setValue(1, 0, 0);
        _mirror->execute();
    }

    void TearDown() override
    {}

    Part::Mirroring* _mirror = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(FeatureMirroringTest, testXMirror)
{
    // Arrange
    Base::BoundBox3d bb = _mirror->Shape.getShape().getBoundBox();
    // Assert size and position
    EXPECT_EQ(getVolume(_mirror->Shape.getShape().getShape()), 6);
    // Mirrored it around X from 0,0,0 -> 1,2,3  to  0,0,-3 -> 1,2,0
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, 0, -3, 1, 2, 0)));
    // Assert correct element Map
    EXPECT_TRUE(allElementsMatch(
        _mirror->Shape.getShape(),
        {
            "+X;:M;MIR;:H7024e4eea46fe3f5:7,F",      "+Y;:M;MIR;:H7024e4eea46fe3f5:7,F",
            "+Z;:M;MIR;:H7024e4eea46fe3f5:7,F",      "-X;:M;MIR;:H7024e4eea46fe3f5:7,F",
            "-Y;:M;MIR;:H7024e4eea46fe3f5:7,F",      "-Z;:M;MIR;:H7024e4eea46fe3f5:7,F",
            "+Z^-Y;:M;MIR;:H7024e4eea46fe3f5:7,E",   "+Y^-Z;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "+Y^+Z;:M;MIR;:H7024e4eea46fe3f5:7,E",   "-X^-Y;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "+Z^-X;:M;MIR;:H7024e4eea46fe3f5:7,E",   "+Y^-X;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "-X^-Z;:M;MIR;:H7024e4eea46fe3f5:7,E",   "+X^-Y;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "+X^+Z;:M;MIR;:H7024e4eea46fe3f5:7,E",   "+X^+Y;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "+X^-Z;:M;MIR;:H7024e4eea46fe3f5:7,E",   "-Y^-Z;:M;MIR;:H7024e4eea46fe3f5:7,E",
            "Vertex1;:M;MIR;:H7024e4eea46fe3f5:7,V", "Vertex2;:M;MIR;:H7024e4eea46fe3f5:7,V",
            "Vertex3;:M;MIR;:H7024e4eea46fe3f5:7,V", "Vertex4;:M;MIR;:H7024e4eea46fe3f5:7,V",
            "Vertex5;:M;MIR;:H7024e4eea46fe3f5:7,V", "Vertex6;:M;MIR;:H7024e4eea46fe3f5:7,V",
            "Vertex7;:M;MIR;:H7024e4eea46fe3f5:7,V", "Vertex8;:M;MIR;:H7024e4eea46fe3f5:7,V",
        }
    ));
}

TEST_F(FeatureMirroringTest, testYMirrorWithExistingElementMap)
{
    // Arrange
    Part::Fuse* _fuse = nullptr;  // NOLINT Can't be private in a test framework
    _fuse = _doc->addObject<Part::Fuse>();
    _fuse->Base.setValue(_boxes[0]);
    _fuse->Tool.setValue(_boxes[1]);
    _fuse->Refine.setValue(false);
    // Act
    _fuse->execute();
    _mirror->Source.setValue(_fuse);
    _mirror->Base.setValue(0, 1, 0);  // Y Axis
    Part::TopoShape ts = _fuse->Shape.getValue();
    double volume = getVolume(ts.getShape());
    Base::BoundBox3d bb = _mirror->Shape.getShape().getBoundBox();
    // Assert size and position
    EXPECT_EQ(getVolume(_mirror->Shape.getShape().getShape()), volume);
    // Mirrored it around X from 0,0,0 -> 1,2,3  to  0,0,-3 -> 1,2,0
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, 0, -3, 1, 3, 0)));
    // Assert correct element Map
    EXPECT_TRUE(elementsMatch(
        _mirror->Shape.getShape(),
        {
            "+X;:M(+X;K2;:H54e9ed54fc7242db:3,F);FUS;:H446a206b63def637:25,F;:M;MIR;:"
            "H10cd4b539912d5b5:7,F",
            "+X;:M2;FUS;:H446a206b63def637:8,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "+X;:M;FUS;:H54e9ed54fc7242db:7,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "+Y;:H446a206b63def637,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "+Z;:M(+Z;K2;:H54e9ed54fc7242db:3,F);FUS;:H446a206b63def637:25,F;:M;MIR;:"
            "H10cd4b539912d5b5:7,F",
            "+Z;:M2;FUS;:H446a206b63def637:8,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "+Z;:M;FUS;:H54e9ed54fc7242db:7,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "-X;:M(-X;K2;:H54e9ed54fc7242db:3,F);FUS;:H446a206b63def637:25,F;:M;MIR;:"
            "H10cd4b539912d5b5:7,F",
            "-X;:M2;FUS;:H446a206b63def637:8,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "-X;:M;FUS;:H54e9ed54fc7242db:7,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "-Y;:H54e9ed54fc7242db,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "-Z;:M(-Z;K2;:H54e9ed54fc7242db:3,F);FUS;:H446a206b63def637:25,F;:M;MIR;:"
            "H10cd4b539912d5b5:7,F",
            "-Z;:M2;FUS;:H446a206b63def637:8,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "-Z;:M;FUS;:H54e9ed54fc7242db:7,F;:M;MIR;:H10cd4b539912d5b5:7,F",
            "+Z^-Y;:H54e9ed54fc7242db,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Z^-Y;:M;FUS;:H446a206b63def637:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^-Z;:H446a206b63def637,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^-Z;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^+Z;:H446a206b63def637,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^+Z;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-X^-Y;:H54e9ed54fc7242db,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-X^-Y;:M;FUS;:H446a206b63def637:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Z^-X;:M(+Z^-X;K2;:H54e9ed54fc7242db:3,E);FUS;:H446a206b63def637:28,E;:M;MIR;:"
            "H10cd4b539912d5b5:7,E",
            "+Z^-X;:M2;FUS;:H446a206b63def637:8,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Z^-X;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^-X;:H446a206b63def637,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+Y^-X;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-X^-Z;:M(-X^-Z;K2;:H54e9ed54fc7242db:3,E);FUS;:H446a206b63def637:28,E;:M;MIR;:"
            "H10cd4b539912d5b5:7,E",
            "-X^-Z;:M2;FUS;:H446a206b63def637:8,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-X^-Z;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^-Y;:H54e9ed54fc7242db,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^-Y;:M;FUS;:H446a206b63def637:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^+Z;:M(+X^+Z;K2;:H54e9ed54fc7242db:3,E);FUS;:H446a206b63def637:28,E;:M;MIR;:"
            "H10cd4b539912d5b5:7,E",
            "+X^+Z;:M2;FUS;:H446a206b63def637:8,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^+Z;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^+Y;:H446a206b63def637,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^+Y;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^-Z;:M(+X^-Z;K2;:H54e9ed54fc7242db:3,E);FUS;:H446a206b63def637:28,E;:M;MIR;:"
            "H10cd4b539912d5b5:7,E",
            "+X^-Z;:M2;FUS;:H446a206b63def637:8,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "+X^-Z;:M;FUS;:H54e9ed54fc7242db:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-Y^-Z;:H54e9ed54fc7242db,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "-Y^-Z;:M;FUS;:H446a206b63def637:7,E;:M;MIR;:H10cd4b539912d5b5:7,E",
            "Vertex1;:H54e9ed54fc7242db,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex1;:M;FUS;:H446a206b63def637:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex2;:H54e9ed54fc7242db,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex2;:M;FUS;:H446a206b63def637:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex3;:H446a206b63def637,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex3;:M;FUS;:H54e9ed54fc7242db:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex4;:H446a206b63def637,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex4;:M;FUS;:H54e9ed54fc7242db:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex5;:H54e9ed54fc7242db,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex5;:M;FUS;:H446a206b63def637:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex6;:H54e9ed54fc7242db,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex6;:M;FUS;:H446a206b63def637:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex7;:H446a206b63def637,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex7;:M;FUS;:H54e9ed54fc7242db:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex8;:H446a206b63def637,V;:M;MIR;:H10cd4b539912d5b5:7,V",
            "Vertex8;:M;FUS;:H54e9ed54fc7242db:7,V;:M;MIR;:H10cd4b539912d5b5:7,V",
        }
    ));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
