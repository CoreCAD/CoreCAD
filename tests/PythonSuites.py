# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors
# Cruth

"""Say whether the suites the program registers are the suites ctest runs.

The Python test modules are registered at run time -- each module adds its own names to
``FreeCAD.__unit_test__`` as it loads -- and ctest is told about them at configure time, from a
list written out by hand. Two lists that no one compares drift apart silently, and the way they
drift is the one that hides work: a module adds a suite, nothing runs it, and nobody learns that
until something in it breaks badly enough to be noticed another way.

So the two lists are compared, here, on every run. This test fails when a suite is registered and
not run, and when a suite is run and no longer registered.
"""

import os
import sys

import FreeCAD


def say(words):
    """Say it where a test runner will show it, and now rather than at exit.

    Printing alone was not enough: the embedded interpreter is torn down by the exit that follows,
    and a buffer that has not been flushed by then is a message nobody ever sees -- which is the
    exact failure this check exists to prevent.
    """
    print(words, flush=True)
    sys.stdout.flush()


def main():
    listed = [name for name in os.environ.get("CRUTH_PYTHON_TEST_SUITES", "").split(";") if name]
    registered = list(FreeCAD.__unit_test__)

    if not listed:
        say("PythonSuites: nothing was handed to this check, so it can prove nothing.")
        return 1

    unrun = [name for name in registered if name not in listed]
    gone = [name for name in listed if name not in registered]

    for name in unrun:
        say(
            "PythonSuites: '%s' is registered as a test suite and nothing runs it. "
            "Add it to PYTHON_TEST_SUITES in tests/CMakeLists.txt." % name
        )
    for name in gone:
        say(
            "PythonSuites: ctest runs '%s' and this build registers no such suite. "
            "Remove it from PYTHON_TEST_SUITES in tests/CMakeLists.txt." % name
        )

    if unrun or gone:
        return 1

    say("PythonSuites: %d registered suites, all of them run." % len(registered))
    return 0


sys.exit(main())
