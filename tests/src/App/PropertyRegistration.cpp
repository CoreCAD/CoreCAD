// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <Base/Exception.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>

#include <src/App/InitApplication.h>

namespace Tests
{
/// Registers normally: every property is added before anything looks one up.
class RegistersInOrder: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Tests::RegistersInOrder);

public:
    RegistersInOrder()
    {
        ADD_PROPERTY(First, (0));
        ADD_PROPERTY(Second, (0));
    }

    App::PropertyInteger First;
    App::PropertyInteger Second;
};

/// Looks a property up by name partway through registering. The lookup seals the class's
/// property table, so the property added after it can no longer be registered.
class LooksUpMidRegistration: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Tests::LooksUpMidRegistration);

public:
    LooksUpMidRegistration()
    {
        ADD_PROPERTY(First, (0));
        (void)getPropertyByName("First");
        ADD_PROPERTY(Second, (0));
    }

    App::PropertyInteger First;
    App::PropertyInteger Second;
};
}  // namespace Tests

PROPERTY_SOURCE(Tests::RegistersInOrder, App::DocumentObject)        // NOLINT
PROPERTY_SOURCE(Tests::LooksUpMidRegistration, App::DocumentObject)  // NOLINT

/** A property registered after its class's table was sealed must fail loudly (Cruth #42).
 *
 *  It used to vanish in debug builds: the property object existed, but nothing could find it by
 *  name, and a dialog that asked for it crashed far from the cause. Pad lost four properties this
 *  way. Release builds already threw; now every build does.
 */
class PropertyRegistrationTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        Tests::RegistersInOrder::init();
        Tests::LooksUpMidRegistration::init();
    }
};

TEST_F(PropertyRegistrationTest, everyPropertyRegisteredInOrderIsFoundByName)
{
    Tests::RegistersInOrder obj;
    EXPECT_NE(obj.getPropertyByName("First"), nullptr);
    EXPECT_NE(obj.getPropertyByName("Second"), nullptr);
}

TEST_F(PropertyRegistrationTest, registeringAfterALookupSealedTheTableThrows)
{
    EXPECT_THROW(Tests::LooksUpMidRegistration obj, Base::RuntimeError);
}
