#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Host regressions for generated documentation and Testing deployment configuration."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import glossary
from website import testing_update

spec = importlib.util.spec_from_file_location("install_testing", Path(__file__).parent / "website/install-testing.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class Glossary(unittest.TestCase):
    def test_markdown_links_use_forward_slashes_on_windows(self):
        data = {"categories": ["Protocol"], "entries": [
            {"term": "Example", "definition": "A linked term.", "scope": "dev",
             "category": "Protocol", "link": "spec/example.md"}]}
        with patch.object(glossary.os.path, "relpath", return_value="..\\spec\\example.md"):
            page = glossary.render(data, "dev")
        self.assertIn("[More](../spec/example.md)", page)
        self.assertNotIn("\\", page)


class Deployment(unittest.TestCase):
    def test_installer_reads_the_selected_project(self):
        with patch.object(installer.subprocess, "check_output", return_value='{"name":"test-project"}') as command:
            self.assertEqual(installer.compose_config(Path("compose.yaml"), "test-project"), {"name": "test-project"})
        self.assertEqual(command.call_args.args[0],
                         ["docker", "compose", "-p", "test-project", "-f", "compose.yaml", "config", "--format", "json"])

    def test_installer_rejects_invalid_projects_before_running_docker(self):
        with patch.object(installer.subprocess, "check_output") as command:
            for project in ("", "Uppercase", "-option", "bad/name", "white space"):
                with self.subTest(project=project), self.assertRaises(ValueError):
                    installer.compose_config(Path("compose.yaml"), project)
        command.assert_not_called()

    def test_updater_uses_the_configured_project(self):
        with patch.object(testing_update.subprocess, "run") as command:
            testing_update.compose({"compose": "compose.json", "project": "another-project"})
        self.assertEqual(command.call_args.args[0][:7],
                         ["docker", "compose", "-p", "another-project", "-f", "compose.json", "up"])
        self.assertTrue(command.call_args.kwargs["check"])

    def test_legacy_config_keeps_the_project_in_the_compose_file(self):
        with patch.object(testing_update.subprocess, "run") as command:
            testing_update.compose({"compose": "compose.json"})
        self.assertEqual(command.call_args.args[0][:5], ["docker", "compose", "-f", "compose.json", "up"])


if __name__ == "__main__":
    # tools/ci.sh discovers tools exposing this option; these tests never contact a service.
    unittest.main(argv=[sys.argv[0]] + [arg for arg in sys.argv[1:] if arg != "--self-test"])
