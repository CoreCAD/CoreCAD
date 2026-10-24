// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <App/Link.h>
#include <Base/Exception.h>
#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/JointGroup.h>
#include <Mod/Assembly/App/Motion.h>
#include <Mod/Assembly/App/Simulation.h>
#include <src/App/InitApplication.h>

// Cruth #130: an object sits in at most one folder, but an assembly records membership, not
// filing. Filing a component (or a joint) in a folder must not take it out of the assembly,
// and adding a filed object to an assembly must not take it out of its folder.

class FolderFilingTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("folders");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _assembly = _doc->addObject<Assembly::AssemblyObject>("Assembly");
        _joints = _assembly->addObject<Assembly::JointGroup>("Joints");
        _folder = _doc->addObject<App::DocumentObjectGroup>("Folder");
        _part = _doc->addObject<App::Link>("Component");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    std::string _docName;
    App::Document* _doc = nullptr;
    Assembly::AssemblyObject* _assembly = nullptr;
    Assembly::JointGroup* _joints = nullptr;
    App::DocumentObjectGroup* _folder = nullptr;
    App::Link* _part = nullptr;
};

TEST_F(FolderFilingTest, FilingAComponentKeepsItInTheAssembly)
{
    _assembly->addObject(_part);
    _folder->addObject(_part);

    EXPECT_TRUE(_assembly->hasObject(_part));
    EXPECT_TRUE(_folder->hasObject(_part));
}

TEST_F(FolderFilingTest, AddingAFiledObjectToTheAssemblyKeepsItInItsFolder)
{
    _folder->addObject(_part);
    _assembly->addObject(_part);

    EXPECT_TRUE(_folder->hasObject(_part));
    EXPECT_TRUE(_assembly->hasObject(_part));
}

TEST_F(FolderFilingTest, FilingAJointKeepsItInTheJointGroup)
{
    auto* joint = _doc->addObject<App::Link>("Joint");
    _joints->addObject(joint);
    _folder->addObject(joint);

    EXPECT_TRUE(_joints->hasObject(joint));
    EXPECT_TRUE(_folder->hasObject(joint));
}

TEST_F(FolderFilingTest, SettingAFolderDirectlyAcceptsAnAssemblyMember)
{
    _assembly->addObject(_part);

    EXPECT_NO_THROW(_folder->Group.setValues({_part}));
    EXPECT_TRUE(_assembly->hasObject(_part));
}

// The filing rule itself stays: moving an object to another folder takes it out of the first.
TEST_F(FolderFilingTest, AnObjectSitsInOneFolderAtATime)
{
    auto* other = _doc->addObject<App::DocumentObjectGroup>("Other");
    _folder->addObject(_part);
    other->addObject(_part);

    EXPECT_FALSE(_folder->hasObject(_part));
    EXPECT_TRUE(other->hasObject(_part));
}

TEST_F(FolderFilingTest, SettingAFolderDirectlyStillRejectsAnotherFoldersMember)
{
    auto* other = _doc->addObject<App::DocumentObjectGroup>("Other");
    _folder->addObject(_part);

    EXPECT_THROW(other->Group.setValues({_part}), Base::RuntimeError);
    EXPECT_TRUE(_folder->hasObject(_part));
    EXPECT_FALSE(other->hasObject(_part));
}

// A simulation holds its motions the same way: filing one must not take it out.
TEST_F(FolderFilingTest, FilingAMotionKeepsItInItsSimulation)
{
    auto* simulation = _doc->addObject<Assembly::Simulation>("Simulation");
    auto* motion = _doc->addObject<Assembly::Motion>("Motion");
    simulation->addObject(motion);
    _folder->addObject(motion);

    EXPECT_TRUE(simulation->hasObject(motion));
    EXPECT_TRUE(_folder->hasObject(motion));
}
