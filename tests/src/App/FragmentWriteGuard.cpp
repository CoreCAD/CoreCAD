// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/FileInfo.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Writer.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

/** A document that did not come back whole is a fragment, and the file it came from is the only
 *  remaining copy of what is missing from it.
 *
 *  A truncated file, or one still carrying the markers of an unfinished merge, opens as its own
 *  beginning. An ordinary save then publishes that beginning over the whole record. These tests
 *  hold the guard, and the way past it, in place (Amendment 19 Clause 19.3).
 */
class FragmentWriteGuardTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
    }

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static void writeAll(const std::string& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        out << text;
    }

    /// A saved three-object document, cut off part way through -- what a truncated write or an
    /// interrupted transfer leaves behind.
    App::Document* openTruncated(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* whole
            = app.newDocument(app.getUniqueDocumentName("fragment").c_str(), "testUser");
        for (const char* name : {"Alpha", "Beta", "Gamma"}) {
            whole->addObject("App::VarSet", name);
        }
        EXPECT_TRUE(whole->saveAs(path.c_str()));
        const std::string full = readAll(path);
        app.closeDocument(whole->getName());

        const std::string::size_type at = full.find("name=\"Gamma\"");
        EXPECT_NE(at, std::string::npos);
        writeAll(path, full.substr(0, at));

        _doc = app.openDocument(path.c_str());
        return _doc;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The measured loss: the document opens as the prefix that was read, and an ordinary save writes
// that prefix over the file that still holds everything.
TEST_F(FragmentWriteGuardTest, savingAFragmentOverItsOwnFileIsRefusedAndChangesNothing)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);
    ASSERT_TRUE(doc->testStatus(App::Document::RestoreError))
        << "a truncated file opened as a whole document";

    const std::string before = readAll(path);
    EXPECT_FALSE(doc->save()) << "a fragment was published over the record";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file on disk";
}

// The guard travels with the document's state, never with the destination: a rule attached to the
// path is walked around by writing elsewhere and writing back.
TEST_F(FragmentWriteGuardTest, savingAFragmentToAnyOtherPathIsRefusedToo)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    EXPECT_FALSE(doc->saveCopy(elsewhere.c_str()))
        << "a fragment was written to a new path, from where it can be copied back";
    EXPECT_TRUE(readAll(elsewhere).empty()) << "a refused copy left a fragment on disk";
}

// A refused save changes nothing -- not the document, not the name or the path it is saved under.
// A refusal that has already renamed the document performed half of the write it declined.
TEST_F(FragmentWriteGuardTest, aRefusedSaveAsLeavesTheDocumentWhereItWas)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string name = doc->FileName.getStrValue();
    const std::string label = doc->Label.getStrValue();

    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    EXPECT_FALSE(doc->saveAs(elsewhere.c_str()));
    EXPECT_EQ(doc->FileName.getStrValue(), name)
        << "a refused Save As left the document pointing at the file it declined to write";
    EXPECT_EQ(doc->Label.getStrValue(), label) << "a refused Save As renamed the document";
}

// The way past the guard, which the clause requires exist: a caller may accept the loss, by name,
// per write. What is forbidden is a write that proceeds on an assumed answer.
TEST_F(FragmentWriteGuardTest, aWriteThatNamesWhatItLosesIsAllowedThrough)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::vector<std::string> losing = doc->whatASaveWouldLose();
    ASSERT_FALSE(losing.empty()) << "a fragment would not say what a save would cost";

    EXPECT_FALSE(doc->saveAcceptingLoss({"something else"}, ""))
        << "a write was let through on an answer to a question nobody asked";

    const std::string before = readAll(path);
    EXPECT_TRUE(doc->saveAcceptingLoss(losing, ""))
        << "a caller that accepted the loss was refused";
    EXPECT_NE(readAll(path), before) << "an accepted write did not happen";
}

// The acceptance is given per write, and a write from here still costs what it costs: the file
// that holds what is missing is untouched by a write that went somewhere else.
TEST_F(FragmentWriteGuardTest, acceptingAWriteElsewhereDoesNotSettleTheAccountWithTheFile)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(doc->saveAcceptingLoss(doc->whatASaveWouldLose(), elsewhere));

    EXPECT_FALSE(doc->whatASaveWouldLose().empty())
        << "a write to another path was treated as having settled what the first file holds";
    EXPECT_FALSE(doc->save()) << "accepting one write turned the guard off";
}

// The other half of the same rule: a cost that is no longer real is not a cost. Once the accepted
// write has landed on the very file the loss was measured against, that file holds nothing more
// than this document does, and going on refusing would be refusing over content that no longer
// exists anywhere.
//
// Measured before this: the guard read a status bit that a read set and nothing ever cleared, so
// every later save of the document was refused for a loss that had already happened.
TEST_F(FragmentWriteGuardTest, anAcceptedWriteOverItsOwnFileSettlesTheAccount)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    ASSERT_TRUE(doc->saveAcceptingLoss(doc->whatASaveWouldLose(), ""));

    EXPECT_TRUE(doc->whatASaveWouldLose().empty())
        << "a document still named a cost its own file no longer holds";
    EXPECT_TRUE(doc->holdsUnreadContent() == false)
        << "a document that is now exactly its own file still called itself not whole";

    const std::string written = readAll(path);
    EXPECT_TRUE(doc->save()) << "an ordinary save was refused over a loss already taken";
    EXPECT_FALSE(readAll(path).empty()) << "the save that was allowed through wrote nothing";
}

// A fragment is the worst case the amendment describes, and asking whether the document is whole
// has to answer for it. Measured before this: `IsWhole` was True for a truncated document,
// because the question was answered from the statements the read KEPT and a fragment keeps
// nothing -- the content never arrived to be kept.
TEST_F(FragmentWriteGuardTest, aFragmentDoesNotCallItselfWhole)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    EXPECT_TRUE(doc->holdsUnreadContent()) << "a truncated document said it was whole";
}

// An ordinary document is not gated. The guard exists to stop a quiet loss, not to make a person
// argue with their own files.
TEST_F(FragmentWriteGuardTest, anOrdinaryDocumentSavesWithoutBeingAskedAnything)
{
    auto& app = App::GetApplication();
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    _doc = app.newDocument(app.getUniqueDocumentName("whole").c_str(), "testUser");
    _doc->addObject("App::VarSet", "Alpha");

    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    EXPECT_TRUE(_doc->whatASaveWouldLose().empty()) << "an ordinary document named a cost";
    EXPECT_TRUE(_doc->save()) << "an ordinary save was refused";
}

// Reading a file is a fresh account of that file. A document read once as a fragment and then read
// again from a whole file carries nothing of the first read into the second.
TEST_F(FragmentWriteGuardTest, readingAgainStartsTheAccountOver)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);
    ASSERT_FALSE(doc->whatASaveWouldLose().empty());

    // The whole file put back under the same document, and read again.
    App::Document* whole = App::GetApplication().newDocument(
        App::GetApplication().getUniqueDocumentName("rebuilt").c_str(),
        "testUser"
    );
    for (const char* name : {"Alpha", "Beta", "Gamma"}) {
        whole->addObject("App::VarSet", name);
    }
    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(whole->saveAs(elsewhere.c_str()));
    App::GetApplication().closeDocument(whole->getName());
    writeAll(path, readAll(elsewhere));

    doc->restore();
    EXPECT_TRUE(doc->whatASaveWouldLose().empty())
        << "a document read back whole was still charged for an earlier read";
    EXPECT_TRUE(doc->save()) << "a document read back whole was still refused";
}

// A refusal a script cannot see is a refusal it cannot act on (P8).
//
// Measured before this: saveAs and saveCopy discarded the refusal and handed the caller None back
// with no file written -- indistinguishable from success -- and save() named the wrong cause
// entirely ("Object attribute 'FileName' is not set"). A script that opens a directory of
// documents and saves them is the tool most able to cause damage at scale, and it has to be able
// to tell a write that happened from one that did not.
TEST_F(FragmentWriteGuardTest, aRefusedWriteReachesTheCallerThatAskedForIt)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string name = doc->getName();
    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";

    // Each of the three write paths raises, and says what the write would lose.
    for (const std::string& call :
         {std::string("save()"), "saveAs(u'" + elsewhere + "')", "saveCopy('" + elsewhere + "')"}) {
        std::string raised;
        try {
            Base::Interpreter().runString(
                ("import FreeCAD\nFreeCAD.getDocument('" + name + "')." + call + "\n").c_str()
            );
        }
        catch (const Base::Exception& e) {
            // Raised through the exception factory, which maps a Python RuntimeError onto the
            // matching C++ type -- so it is caught as what every refusal here is: an exception.
            raised = e.what();
        }
        EXPECT_FALSE(raised.empty()) << "a refused " << call
                                     << " was reported as a write that "
                                        "happened";
        EXPECT_NE(raised.find("did not come back whole"), std::string::npos)
            << "a refused " << call << " did not say why: " << raised;
        EXPECT_NE(raised.find("saveAcceptingLoss"), std::string::npos)
            << "a refused " << call << " did not name the way past the guard: " << raised;
    }

    EXPECT_TRUE(readAll(elsewhere).empty()) << "a refused write left something on disk";
}


/** A read that steps over what it cannot understand loses it just as completely as a read that
 *  stops, and it does so without raising anything at all.
 *
 *  These hold the other half of Clause 19.3: what a write would cost is measured from what the
 *  read could not bring back, not from a bit set when a read threw. A sealed archive -- what an
 *  older version or a records system hands over -- is where such a read still happens, and its
 *  reader has always carried on past a property it could not read and an object it could not
 *  create.
 */
class SilentLossGuardTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
    }

    /// A sealed archive stating exactly what the test wants a reader to trip over.
    static void writeArchive(const std::string& path, const std::string& documentXml)
    {
        Base::ZipWriter writer(path.c_str());
        writer.putNextEntry("Document.xml");
        writer.Stream() << documentXml;
        writer.writeFiles();
    }

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// A property whose value will not read back. The reader says so and carries on, the document opens
// looking ordinary, and an ordinary save writes the absence of that value over the file.
//
// Measured before this: the document reported nothing to lose, called itself whole, and saved.
TEST_F(SilentLossGuardTest, aPropertyThatWouldNotReadBackIsNamedAsACostOfSaving)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    writeArchive(
        path,
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<Document SchemaVersion=\"4\" ProgramVersion=\"0.21\" FileVersion=\"1\">\n"
        "  <Properties Count=\"0\">\n"
        "  </Properties>\n"
        "  <Objects Count=\"1\">\n"
        "    <Object type=\"App::VarSet\" name=\"Alpha\" />\n"
        "  </Objects>\n"
        "  <ObjectData Count=\"1\">\n"
        "    <Object name=\"Alpha\">\n"
        "      <Properties Count=\"1\">\n"
        "        <Property name=\"Label\" type=\"App::PropertyString\">\n"
        "          <String/>\n"
        "        </Property>\n"
        "      </Properties>\n"
        "    </Object>\n"
        "  </ObjectData>\n"
        "</Document>\n"
    );

    _doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    ASSERT_NE(_doc->getObject("Alpha"), nullptr) << "the read stopped instead of carrying on";
    ASSERT_FALSE(_doc->testStatus(App::Document::RestoreError))
        << "this loss is supposed to be the one that does NOT throw";

    const std::vector<std::string> losing = _doc->whatASaveWouldLose();
    ASSERT_FALSE(losing.empty()) << "a save that would drop a stated value said it would cost "
                                    "nothing";
    EXPECT_NE(losing.front().find("Label"), std::string::npos)
        << "the cost did not name what would be lost: " << losing.front();
    EXPECT_FALSE(_doc->holdsUnreadContent() == false) << "the document called itself whole";

    const std::string before = readAll(path);
    EXPECT_FALSE(_doc->save()) << "a value stated in the file was written away without a word";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file on disk";
}

// An object of a type this build cannot construct. The archive reader drops it and carries on;
// everything the file says about it -- and every value it held -- goes with it.
TEST_F(SilentLossGuardTest, anObjectThisBuildCannotConstructIsNamedAsACostOfSaving)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    writeArchive(
        path,
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<Document SchemaVersion=\"4\" ProgramVersion=\"0.21\" FileVersion=\"1\">\n"
        "  <Properties Count=\"0\">\n"
        "  </Properties>\n"
        "  <Objects Count=\"2\">\n"
        "    <Object type=\"App::VarSet\" name=\"Alpha\" />\n"
        "    <Object type=\"NoSuchModule::NoSuchThing\" name=\"Beta\" />\n"
        "  </Objects>\n"
        "  <ObjectData Count=\"1\">\n"
        "    <Object name=\"Alpha\">\n"
        "      <Properties Count=\"0\">\n"
        "      </Properties>\n"
        "    </Object>\n"
        "  </ObjectData>\n"
        "</Document>\n"
    );

    _doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    EXPECT_EQ(_doc->getObject("Beta"), nullptr) << "this build constructed the unconstructable";

    const std::vector<std::string> losing = _doc->whatASaveWouldLose();
    ASSERT_FALSE(losing.empty()) << "a save that would drop a whole object said it would cost "
                                    "nothing";
    EXPECT_NE(losing.front().find("Beta"), std::string::npos)
        << "the cost did not name the object that would be lost: " << losing.front();

    const std::string before = readAll(path);
    EXPECT_FALSE(_doc->save()) << "an object stated in the file was written away without a word";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file on disk";
}

// An archive that reads back entirely is not charged for anything. The guard is keyed to loss, and
// an ordinary file from an older version loses nothing by being opened and saved.
TEST_F(SilentLossGuardTest, anArchiveThatReadsBackWholeIsNotCharged)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    writeArchive(
        path,
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<Document SchemaVersion=\"4\" ProgramVersion=\"0.21\" FileVersion=\"1\">\n"
        "  <Properties Count=\"0\">\n"
        "  </Properties>\n"
        "  <Objects Count=\"1\">\n"
        "    <Object type=\"App::VarSet\" name=\"Alpha\" />\n"
        "  </Objects>\n"
        "  <ObjectData Count=\"1\">\n"
        "    <Object name=\"Alpha\">\n"
        "      <Properties Count=\"1\">\n"
        "        <Property name=\"Label\" type=\"App::PropertyString\">\n"
        "          <String value=\"Alpha\"/>\n"
        "        </Property>\n"
        "      </Properties>\n"
        "    </Object>\n"
        "  </ObjectData>\n"
        "</Document>\n"
    );

    _doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    EXPECT_TRUE(_doc->whatASaveWouldLose().empty())
        << "a file that read back whole was charged for saving";
    EXPECT_TRUE(_doc->save()) << "a file that read back whole was refused";
}
