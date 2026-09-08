// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <FCConfig.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/ExplodedView.h>
#include <Mod/Assembly/App/ExplodedViewStep.h>
#include <Mod/Assembly/App/ViewGroup.h>
#include <src/App/InitApplication.h>

class ExplodedViewTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        auto* doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");

        _assembly = doc->addObject<Assembly::AssemblyObject>();
        // An exploded view is never held by the assembly directly: it lives inside the
        // assembly's ViewGroup, which is how the workbench creates it.
        _viewGroup = _assembly->addObject<Assembly::ViewGroup>("views");
        _view = _viewGroup->addObject<Assembly::ExplodedView>("explodedView");
        _step = doc->addObject<Assembly::ExplodedViewStep>();
        _view->addObject(_step);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    Assembly::AssemblyObject* _assembly {nullptr};
    Assembly::ViewGroup* _viewGroup {nullptr};
    Assembly::ExplodedView* _view {nullptr};
    Assembly::ExplodedViewStep* _step {nullptr};

private:
    std::string _docName;
};

// An exploded view sits one container below the assembly. Looking only at its direct
// parents finds the ViewGroup and stops, which is what the former Python version did:
// it never found the assembly, so exploding did nothing.
TEST_F(ExplodedViewTest, findsTheAssemblyThroughItsContainer)  // NOLINT
{
    ASSERT_NE(_view->getAssembly(), nullptr);
    EXPECT_EQ(_view->getAssembly(), _assembly);
}

TEST_F(ExplodedViewTest, holdsItsMovesInOrder)  // NOLINT
{
    const std::vector<Assembly::ExplodedViewStep*> steps = _view->getSteps();

    ASSERT_EQ(steps.size(), 1U);
    EXPECT_EQ(steps.front(), _step);
}

TEST_F(ExplodedViewTest, aMoveIsANormalDisplacementUnlessSaidOtherwise)  // NOLINT
{
    EXPECT_STREQ(_step->MoveType.getValueAsString(), "Normal");

    _step->MoveType.setValue("Radial");
    EXPECT_STREQ(_step->MoveType.getValueAsString(), "Radial");
}

// A move whose reference names nothing acts on nothing, rather than guessing at some
// component (P7: never heal by changing meaning).
TEST_F(ExplodedViewTest, aMoveWithNoReferenceMovesNothing)  // NOLINT
{
    EXPECT_TRUE(_step->movedComponents().empty());
    EXPECT_TRUE(_step->applyStep(Base::Vector3d(), 100.0).empty());
}

// Asking an exploded view for its shape must never disturb the document, and an
// assembly with no geometry has nothing to show rather than a shape of nothing.
TEST_F(ExplodedViewTest, anEmptyAssemblyExplodesToNothing)  // NOLINT
{
    EXPECT_TRUE(_view->getExplodedShape().isNull());
}
