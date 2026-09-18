// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include "Base/Quantity.h"

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/Expression.h"
#include "App/ObjectIdentifier.h"
#include "App/PropertyExpressionEngine.h"

#include "src/App/InitApplication.h"

#include <Base/FileInfo.h>

#include <filesystem>
#include <string>
#include <vector>

// clang-format off

class PropertyExpressionEngineTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc_name = App::GetApplication().getUniqueDocumentName("test");
        _this_doc = App::GetApplication().newDocument(_doc_name.c_str(), "testUser");
        _this_obj = _this_doc -> addObject("Sketcher::SketchObject");
        _source_name = std::string("this_origin");
        _source_prop = _this_obj -> addDynamicProperty("App::PropertyString", _source_name.c_str()); // property with length as string
        _target_name = std::string("this_length");
        _target_prop = _this_obj -> addDynamicProperty("App::PropertyLength", _target_name.c_str()); // property with reference to source
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc_name.c_str());
    }

    std::string doc_name() { return _doc_name; }
    App::Document* this_doc() { return _this_doc; }
    App::DocumentObject* this_obj() { return _this_obj; }
    std::string source_name() { return _source_name; }
    App::Property* source_prop() { return _source_prop; }
    std::string target_name() { return _target_name; }
    App::Property* target_prop() { return _target_prop; }

private:
    std::string _doc_name;
    App::Document* _this_doc {};
    App::DocumentObject* _this_obj {};
    std::string _source_name;
    App::Property* _source_prop {};
    std::string _target_name;
    App::Property* _target_prop {};
};

// https://github.com/FreeCAD/FreeCAD/issues/11965
TEST_F(PropertyExpressionEngineTest, executeCrossPropertyReference)
{
    auto source_text = std::string("1.5 m"); // provided in source
    auto target_text = std::string("1500 mm"); // expected in target

    auto source_path = App::ObjectIdentifier::parse(this_obj(), source_name());
    source_prop()->setPathValue(source_path, source_text);

    auto target_expr = "parsequant(" + source_name() + ")"; // Solution B: parsequant() function

    auto target_path = App::ObjectIdentifier::parse(this_obj(), target_name());
    std::shared_ptr<App::Expression> target_rule(App::Expression::parse(this_obj(), target_expr));
    this_obj()->setExpression(target_path, target_rule);

    this_obj() -> ExpressionEngine.execute();

    auto source_entry = source_prop() -> getPathValue(source_path);
    ASSERT_TRUE(source_entry.type() == typeid(std::string));
    auto source_value = App::any_cast<std::string>(source_entry);

    auto target_entry = target_prop() -> getPathValue(target_path);
    ASSERT_TRUE(target_entry.type() == typeid(Base::Quantity));
    auto target_quant = App::any_cast<Base::Quantity>(target_entry);
    auto target_value = target_quant.getValue();
    auto target_unit = target_quant.getUnit().getString();

    auto verify_quant = Base::Quantity::parse(target_text);

    EXPECT_EQ(target_quant, verify_quant) << ""
        "expecting equal: source_text='" + source_text + "' target_text='" + target_text + "'"
        "instead produced: target_value='" + std::to_string(target_value) + "' target_unit='" + target_unit + "'"
    ;
}

// clang-format on

namespace fs = std::filesystem;

/*  A formula that binds nothing sits next to one that does.
 *
 *  A formula naming another object is written with its durable binding inside it, so it is an
 *  opened element. A formula made of numbers alone binds nothing, so it is written self-closing
 *  -- and a self-closing element has already ended by the time the reader has it. A reader that
 *  asks such a formula for its bindings is handed the NEXT formula instead, and refuses the file
 *  for stating something it never stated. Two formulas of the plain kind are enough to do it, so
 *  this is the ordinary case, not a corner of one.
 */
class FormulasSideBySideTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _folder = fs::path(Base::FileInfo::getTempFileName()) / "project";
        fs::create_directories(_folder);
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        std::error_code ignored;
        fs::remove_all(_folder.parent_path(), ignored);
    }

    /// One part: a source value, and three formulas -- plain, bound, plain -- in that order, so
    /// that a plain one is followed by another formula on either side of a bound one.
    std::string aPartWhoseFormulasSitSideBySide()
    {
        auto& app = App::GetApplication();
        _doc = app.newDocument(app.getUniqueDocumentName("formulas").c_str(), "testUser");

        auto* source = _doc->addObject("App::VarSet", "Source");
        source->addDynamicProperty("App::PropertyFloat", "val");
        App::ObjectIdentifier val = App::ObjectIdentifier::parse(source, "val");
        source->getPropertyByName("val")->setPathValue(val, 5.0);

        auto* holder = _doc->addObject("App::VarSet", "Holder");
        for (const char* name : {"a", "b", "c"}) {
            holder->addDynamicProperty("App::PropertyFloat", name);
        }
        setFormula(holder, "a", "1");
        setFormula(holder, "b", "Source.val * 2");
        setFormula(holder, "c", "3");

        _doc->recompute();
        const std::string path = (_folder / "formulas.cpart").string();
        EXPECT_TRUE(_doc->saveAs(path.c_str())) << "the part did not save";
        app.closeDocument(_doc->getName());
        _doc = nullptr;
        return path;
    }

    static void setFormula(App::DocumentObject* obj, const char* prop, const std::string& text)
    {
        App::ObjectIdentifier path = App::ObjectIdentifier::parse(obj, prop);
        std::shared_ptr<App::Expression> rule(App::Expression::parse(obj, text));
        obj->setExpression(path, rule);
    }

    /// What one object's formulas say, as the reopened file states them.
    std::vector<std::string> formulasOf(const char* name) const
    {
        std::vector<std::string> stated;
        auto* obj = _doc->getObject(name);
        for (const auto& entry : obj->ExpressionEngine.getExpressions()) {
            stated.push_back(entry.first.toString() + " = " + entry.second->toString());
        }
        return stated;
    }

    App::Document* _doc {};
    fs::path _folder;
};

TEST_F(FormulasSideBySideTest, aPlainFormulaDoesNotSwallowTheNextOne)
{
    const std::string path = aPartWhoseFormulasSitSideBySide();

    ASSERT_NO_THROW(_doc = App::GetApplication().openDocument(path.c_str()))
        << "the part was refused for stating something it does not state";
    ASSERT_NE(_doc, nullptr) << "the part did not come back at all";

    EXPECT_EQ(formulasOf("Holder"), (std::vector<std::string> {"a = 1", "b = Source.val * 2", "c = 3"}))
        << "the formulas did not come back as the file states them";
}

TEST_F(FormulasSideBySideTest, aBoundFormulaStillFindsWhatItNames)
{
    const std::string path = aPartWhoseFormulasSitSideBySide();

    ASSERT_NO_THROW(_doc = App::GetApplication().openDocument(path.c_str()));
    ASSERT_NE(_doc, nullptr);

    _doc->recompute();
    auto* holder = _doc->getObject("Holder");
    App::ObjectIdentifier bound = App::ObjectIdentifier::parse(holder, "b");
    EXPECT_DOUBLE_EQ(App::any_cast<double>(holder->getPropertyByName("b")->getPathValue(bound)), 10.0)
        << "the formula that names another object no longer computes from it";
}
