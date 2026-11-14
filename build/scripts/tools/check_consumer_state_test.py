#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_consumer_state.py."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import check_consumer_state as state

ENTRY = '''state {path} {symbol}
    scope            -
    category         {category}
    owner            startup
    lifetime         the whole process
    synchronization  {synchronization}
    reason           a reason
'''


def write(root: pathlib.Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))


class ConsumerStateTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        write(self.root, 'src/AfterNightControls/app/Registry.cpp',
              '﻿#include "Registry.h"\n#include <mutex>\n\nstd::mutex gRegistryLock;\n'
              'static const QString kName = QStringLiteral("x");\n'
              'const char* kPath = "p";\n'
              'Registry& Registry::instance()\n{\n    static Registry registry;\n    return registry;\n}\n'
              'const char* css = R"(body { color: red; } .a b { x: 1; })";\n')
        self.entries = [
            ('src/AfterNightControls/app/Registry.cpp', 'gRegistryLock', 'ui-registration', 'it is the lock'),
            ('src/AfterNightControls/app/Registry.cpp', 'registry', 'ui-registration', 'GUI thread'),
        ]
        self.write_register()

    def tearDown(self) -> None:
        self.directory.cleanup()

    def write_register(self) -> None:
        text = '# header\n\n' + '\n'.join(
            ENTRY.format(path=path, symbol=symbol, category=category, synchronization=synchronization)
            for path, symbol, category, synchronization in self.entries)
        write(self.root, state.REGISTER, text)

    def test_the_register_and_the_tree_agree(self) -> None:
        self.assertEqual(state.check(self.root), [])

    def test_constants_raw_strings_and_directives_are_read_correctly(self) -> None:
        found = state.scan_file(self.root / 'src/AfterNightControls/app/Registry.cpp')
        self.assertEqual(sorted(found), ['gRegistryLock', 'registry'])

    def test_an_unregistered_object_fails(self) -> None:
        write(self.root, 'src/AfterNightProcesses/Cache.cpp', 'static QHash<int, int> gCache;\n')
        problems = state.check(self.root)
        self.assertTrue(any('Cache.cpp:1: gCache has process lifetime' in p for p in problems), problems)

    def test_an_entry_for_a_missing_object_fails(self) -> None:
        self.entries.append(('src/AfterNightControls/app/Registry.cpp', 'gGone', 'ui-state', 'GUI thread'))
        self.write_register()
        self.assertTrue(any('gGone is registered' in p for p in state.check(self.root)))

    def test_categories_are_held_to_their_locations(self) -> None:
        write(self.root, 'src/AfterNightCLI/main.cpp', 'static int gCount = 0;\n')
        self.entries.append(('src/AfterNightCLI/main.cpp', 'gCount', 'ui-state', 'GUI thread'))
        self.entries.append(('src/AfterNightControls/app/Registry.cpp', 'gRegistryLock', 'theme-service', '-'))
        self.entries.pop(0)
        self.write_register()
        problems = state.check(self.root)
        self.assertTrue(any('gCount: ui-state state may only live in a Qt layer' in p for p in problems), problems)
        self.assertTrue(any('gRegistryLock: theme-service' in p for p in problems), problems)

    def test_a_module_seat_lives_only_where_a_context_free_callback_reaches_one(self) -> None:
        write(self.root, 'src/AfterNightCLI/InterruptWatch.cpp', 'InterruptSeat interruptSeat;\n')
        write(self.root, 'src/AfterNightControls/app/Seat.cpp', 'Seat gSeat;\n')
        self.entries.append(('src/AfterNightCLI/InterruptWatch.cpp', 'interruptSeat', 'module-seat', 'a lock'))
        self.entries.append(('src/AfterNightControls/app/Seat.cpp', 'gSeat', 'module-seat', 'a lock'))
        self.write_register()
        problems = state.check(self.root)
        self.assertTrue(any('gSeat: module-seat state may only live in the runtime backend, an extension host or '
                            'the command-line tool' in p for p in problems), problems)
        self.assertFalse(any('interruptSeat' in p for p in problems), problems)

    def test_an_application_seat_lives_only_in_the_application_or_its_context_locator(self) -> None:
        write(self.root, 'src/AfterNightApp/app/Window.cpp', 'static Window* gWindow = nullptr;\n')
        write(self.root, 'src/AfterNightControls/controls/IAppContext.cpp', 'IAppContext* g_app_context = nullptr;\n')
        write(self.root, 'src/AfterNightControls/app/Locator.cpp', 'static Services* gServices = nullptr;\n')
        write(self.root, 'src/AfterNightApp/app/Session.cpp', 'static an::consumer::EngineSession* gSession = nullptr;\n')
        self.entries.append(('src/AfterNightApp/app/Window.cpp', 'gWindow', 'application-seat', 'GUI thread'))
        self.entries.append(('src/AfterNightControls/controls/IAppContext.cpp', 'g_app_context', 'application-seat',
                             'GUI thread'))
        self.entries.append(('src/AfterNightControls/app/Locator.cpp', 'gServices', 'application-seat', 'GUI thread'))
        self.entries.append(('src/AfterNightApp/app/Session.cpp', 'gSession', 'application-seat', 'GUI thread'))
        self.write_register()
        problems = state.check(self.root)
        self.assertTrue(any('gServices: application-seat state may only live in the application, its context '
                            "locator or the shell core's core" in p for p in problems), problems)
        self.assertTrue(any('gSession: application-seat state holds an engine image' in p for p in problems), problems)
        self.assertFalse(any('gWindow' in p or 'g_app_context' in p for p in problems), problems)

    def test_the_shell_core_is_read(self) -> None:
        write(self.root, 'src/AfterNightShellCore/services/Cache.cpp', 'static QHash<int, int> gCache;\n')
        problems = state.check(self.root)
        self.assertTrue(any('src/AfterNightShellCore/services/Cache.cpp:1: gCache has process lifetime' in p
                            for p in problems), problems)

    def test_the_shell_core_is_a_qt_layer(self) -> None:
        write(self.root, 'src/AfterNightShellCore/actions/Order.cpp', 'static int gOrder = 0;\n')
        self.entries.append(('src/AfterNightShellCore/actions/Order.cpp', 'gOrder', 'ui-state', 'GUI thread'))
        self.write_register()
        self.assertEqual(state.check(self.root), [])

    def test_an_application_seat_may_live_in_the_shell_cores_core(self) -> None:
        write(self.root, 'src/AfterNightShellCore/core/Seat.cpp', 'static Core* gCore = nullptr;\n')
        write(self.root, 'src/AfterNightShellCore/services/Seat.cpp', 'static Bus* gBus = nullptr;\n')
        self.entries.append(('src/AfterNightShellCore/core/Seat.cpp', 'gCore', 'application-seat', 'GUI thread'))
        self.entries.append(('src/AfterNightShellCore/services/Seat.cpp', 'gBus', 'application-seat', 'GUI thread'))
        self.write_register()
        problems = state.check(self.root)
        self.assertTrue(any('gBus: application-seat state may only live in the application, its context locator or '
                            "the shell core's core" in p for p in problems), problems)
        self.assertFalse(any('gCore' in p for p in problems), problems)

    def test_platform_services_live_only_in_the_platform_library(self) -> None:
        write(self.root, 'src/AfterNightPlatform/src/Logging.cpp', 'std::shared_ptr<int> gLogger;\n')
        write(self.root, 'src/AfterNightControls/app/Logger.cpp', 'static int gLevel = 0;\n')
        self.entries.append(('src/AfterNightPlatform/src/Logging.cpp', 'gLogger', 'platform-service', 'a lock'))
        self.entries.append(('src/AfterNightControls/app/Logger.cpp', 'gLevel', 'platform-service', 'a lock'))
        self.write_register()
        problems = state.check(self.root)
        self.assertTrue(any('gLevel: platform-service state may only live in the platform library' in p
                            for p in problems), problems)
        self.assertFalse(any('gLogger' in p for p in problems), problems)


    def test_no_permitted_category_holds_an_engine_image_or_context(self) -> None:
        write(self.root, 'src/AfterNightControls/app/Cache.cpp', 'static std::map<int, an::ImageStorage> gImages;\n')
        self.entries.append(('src/AfterNightControls/app/Cache.cpp', 'gImages', 'ui-state', 'GUI thread'))
        self.write_register()
        self.assertTrue(any('gImages: ui-state state holds an engine image' in p for p in state.check(self.root)))
        self.entries[-1] = ('src/AfterNightControls/app/Cache.cpp', 'gImages', 'pending-rehome', 'GUI thread')
        self.write_register()
        self.assertEqual(state.check(self.root), [])

    def test_an_unknown_field_is_refused_rather_than_folded_into_the_one_above(self) -> None:
        text = ENTRY.format(path='src/AfterNightControls/app/Registry.cpp', symbol='registry',
                            category='ui-registration', synchronization='GUI thread')
        write(self.root, state.REGISTER, text + '    priority         high\n')
        entries, problems = state.read_register(self.root)
        self.assertTrue(any("'priority' is not a field" in p for p in problems), problems)
        self.assertEqual(entries[0].fields['reason'], 'a reason')

    def test_ui_state_names_its_synchronization(self) -> None:
        self.entries[1] = ('src/AfterNightControls/app/Registry.cpp', 'registry', 'ui-state', 'nobody')
        self.write_register()
        self.assertTrue(any('names the GUI thread or the lock' in p for p in state.check(self.root)))

    def test_an_identity_counter_is_atomic(self) -> None:
        write(self.root, 'src/AfterNightProcesses/Names.h', 'inline int next()\n{\n    static int counter = 0;\n'
                                                            '    return counter++;\n}\n')
        self.entries.append(('src/AfterNightProcesses/Names.h', 'counter', 'identity', 'none'))
        self.write_register()
        self.assertTrue(any('counter: an identity counter' in p for p in state.check(self.root)))

    def test_a_malformed_record_is_reported(self) -> None:
        write(self.root, state.REGISTER, 'state only-a-path\n    category ui-state\n')
        problems = state.check(self.root)
        self.assertTrue(any('a record starts with' in p for p in problems), problems)

    def test_a_field_continues_on_deeper_indentation(self) -> None:
        text = ENTRY.format(path='src/AfterNightControls/app/Registry.cpp', symbol='registry',
                            category='ui-registration', synchronization='GUI thread')
        text = text.replace('    reason           a reason\n', '    reason           a reason\n                     owner of '
                                                                 'nothing, continued\n')
        write(self.root, state.REGISTER, text)
        entries, problems = state.read_register(self.root)
        self.assertEqual(problems, [])
        self.assertEqual(entries[0].fields['reason'], 'a reason owner of nothing, continued')


if __name__ == '__main__':
    unittest.main()
