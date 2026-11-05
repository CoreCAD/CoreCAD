// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <QApplication>

#include <App/Application.h>
#include <App/Document.h>

#include <Gui/Application.h>
#include <Gui/MainWindow.h>

#include <src/App/InitApplication.h>

namespace tests
{

/** Stand up enough of the running program for a test to exercise Gui/ logic.
 *
 * The dialog tests that came before this one reach only the leaves of the Gui tree -- a widget
 * built by hand, asked a question, thrown away. Everything above the leaves (selection, the Gui
 * document, commands, the tree) calls Gui::Application::Instance and Gui::getMainWindow()
 * without asking whether they exist, so none of it could be tested at all.
 *
 * This is the same order the real program uses in Application::runApplication(): the App layer,
 * then Qt, then the Gui application, then the main window. What it leaves out is the startup
 * process, the workbenches, the splash screen, the 3D mouse and the event loop -- none of which
 * a test of Gui logic needs.
 *
 * Both objects are deliberately never destroyed. They are singletons that outlive the test
 * binary's useful life, and tearing them down at exit only invites order-of-destruction crashes
 * in code that has no reason to run.
 */
static void initGuiApplication()
{
    if (Gui::Application::Instance) {
        return;
    }

    initApplication();

    // A QApplication must already exist; QTEST_MAIN creates one before initTestCase() runs.
    Q_ASSERT(qApp != nullptr);

    // Registers the Gui types, and nothing works without it: an object's view provider is made
    // by looking its class up by name, so before this call every object in a document silently
    // arrives without one.
    Gui::Application::initApplication();

    // Every view provider owns a piece of scene graph, built in its constructor, so the scene
    // library has to be running before the first object appears in a document. It needs no
    // graphics card for that -- only the drawing does.
    Gui::Application::initOpenInventor();

    new Gui::Application(true);  // sets Gui::Application::Instance
    new Gui::MainWindow();       // sets the window Gui::getMainWindow() returns
}

/** A document for a test that is about Gui logic rather than about drawing.
 *
 * A document created the ordinary way opens a 3D view with it, and a 3D view wants an OpenGL
 * context that an offscreen test run has not got -- Coin walks into its cache-context lock and
 * the process dies before the first assertion. The view is the only part that needs the
 * graphics card: the Gui document, its view providers and the selection all exist without it.
 */
static App::Document* newViewlessDocument(const char* name)
{
    auto& app = App::GetApplication();
    App::DocumentInitFlags flags;
    flags.createView = false;
    return app.newDocument(app.getUniqueDocumentName(name).c_str(), "testUser", flags);
}

/// Reopen a saved document without the 3D view, for the same reason as above.
static App::Document* openViewlessDocument(const char* fileName)
{
    App::DocumentInitFlags flags;
    flags.createView = false;
    return App::GetApplication().openDocument(fileName, flags);
}

}  // namespace tests
