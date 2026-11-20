import json
import os
import struct
import subprocess
import tempfile
import urllib.error
import urllib.request
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from native import pack_single
from takoyo_agent import api
from takoyo_agent.gui import (
    _stream_failure_event,
    image_request_prompt,
    is_structured_visual_request,
    structured_visual_fallback,
)
from takoyo_agent.memory import ConversationStore
from takoyo_agent.projects import ProjectStore


def _make_directory_link(link: Path, target: Path):
    if os.name == "nt":
        result = subprocess.run(
            ["cmd.exe", "/c", "mklink", "/J", str(link), str(target)],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            raise OSError(result.stderr or result.stdout)
    else:
        link.symlink_to(target, target_is_directory=True)


class APIHardeningTests(unittest.TestCase):
    class Settings:
        def __init__(self, token="", origins=None):
            self.values = {
                "api_token": token,
                "api_allowed_origins": origins or [],
                "provider": "local",
            }

        def get(self, key, default=None):
            return self.values.get(key, default)

        def model_for(self, provider):
            return "test-model"

    def test_bearer_token_is_required_when_configured(self):
        config = self.Settings("secret")
        self.assertFalse(api._is_authorized(self.Settings(), ""))
        self.assertFalse(api._is_authorized(config, ""))
        self.assertFalse(api._is_authorized(config, "Bearer wrong"))
        self.assertTrue(api._is_authorized(config, "Bearer secret"))

    def test_cors_requires_an_exact_configured_origin(self):
        config = self.Settings(origins=["http://localhost:3000"])
        self.assertTrue(api._allowed_origin(config, "http://localhost:3000"))
        self.assertFalse(api._allowed_origin(config, "http://localhost:3001"))
        self.assertFalse(api._allowed_origin(config, "*"))

    def test_local_api_file_reads_and_writes_stay_in_app_workspace(self):
        with tempfile.TemporaryDirectory() as temp:
            home = Path(temp) / "takoyo"
            workspace = home / "workspace"
            projects = home / "projects"
            workspace.mkdir(parents=True)
            projects.mkdir()
            (workspace / "note.txt").write_text("ok", encoding="utf-8")
            (home / "config.json").write_text("{}", encoding="utf-8")
            with patch.multiple(
                api,
                HOME=home,
                WORKSPACE_DIR=workspace,
                PROJECTS_DIR=projects,
            ):
                self.assertEqual(api._readable_under(".").resolve(), workspace.resolve())
                self.assertEqual(api._readable_under("workspace/note.txt").read_text(encoding="utf-8"), "ok")
                with self.assertRaises(api.ApiError):
                    api._readable_under("config.json")
                with self.assertRaises(api.ApiError):
                    api._writable_under("config.json")

    def test_non_loopback_addresses_are_not_trusted_as_local(self):
        self.assertTrue(api._is_loopback_address("127.0.0.1"))
        self.assertTrue(api._is_loopback_address("::1"))
        self.assertFalse(api._is_loopback_address("0.0.0.0"))
        self.assertFalse(api._is_loopback_address("192.168.1.20"))

    def test_http_api_rejects_missing_bearer_token(self):
        allowed_origin = "http://localhost:3000"
        config = self.Settings("secret", [allowed_origin])
        with patch.object(api, "ensure_dirs"), \
             patch.object(api, "ConversationStore"), \
             patch.object(api, "ProjectStore"), \
             patch.object(api.image_gen, "ImageGenerator"), \
             patch.object(api, "get_engine"):
            server, _thread = api.start_api(config=config, host="127.0.0.1", port=0)
            url = f"http://127.0.0.1:{server.server_address[1]}/"
            try:
                with self.assertRaises(urllib.error.HTTPError) as response:
                    urllib.request.urlopen(url, timeout=2)
                self.assertEqual(response.exception.code, 401)
                request = urllib.request.Request(
                    url,
                    headers={"Authorization": "Bearer secret"},
                )
                with urllib.request.urlopen(request, timeout=2) as result:
                    self.assertEqual(result.status, 200)
                preflight = urllib.request.Request(
                    url,
                    method="OPTIONS",
                    headers={"Origin": allowed_origin},
                )
                with urllib.request.urlopen(preflight, timeout=2) as result:
                    self.assertEqual(result.headers.get("Access-Control-Allow-Origin"), allowed_origin)
                rejected_origin = urllib.request.Request(
                    url,
                    method="OPTIONS",
                    headers={"Origin": "http://untrusted.invalid"},
                )
                with urllib.request.urlopen(rejected_origin, timeout=2) as result:
                    self.assertIsNone(result.headers.get("Access-Control-Allow-Origin"))
            finally:
                api.stop_api(server)

    def test_streaming_api_emits_error_event_without_success_terminator(self):
        config = self.Settings("secret")

        def failing_stream(*args, **kwargs):
            raise api.ai_providers.ProviderError("upstream failed")
            yield

        with patch.object(api, "ensure_dirs"), \
             patch.object(api, "ConversationStore"), \
             patch.object(api, "ProjectStore"), \
             patch.object(api.image_gen, "ImageGenerator"), \
             patch.object(api, "get_engine"), \
             patch.object(api.ai_providers, "stream_chat", failing_stream):
            server, _thread = api.start_api(config=config, host="127.0.0.1", port=0)
            url = f"http://127.0.0.1:{server.server_address[1]}/v1/chat/completions"
            body = json.dumps({
                "stream": True,
                "messages": [{"role": "user", "content": "hello"}],
            }).encode("utf-8")
            request = urllib.request.Request(
                url,
                data=body,
                headers={
                    "Authorization": "Bearer secret",
                    "Content-Type": "application/json",
                },
            )
            try:
                with urllib.request.urlopen(request, timeout=2) as response:
                    stream = response.read().decode("utf-8")
                self.assertIn('"error": {"message": "upstream failed"', stream)
                self.assertNotIn("[DONE]", stream)
            finally:
                api.stop_api(server)


class ConversationRecoveryTests(unittest.TestCase):
    def test_backup_restore_preserves_history_and_avoids_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = ConversationStore(root / "source")
            conversation = source.create(first_message="Hello")
            source.append(conversation["id"], "user", "Hello")
            backup = root / "backup.json"
            self.assertEqual(source.backup(backup), 1)

            restored = ConversationStore(root / "restored")
            self.assertEqual(restored.restore_from(backup), 1)
            self.assertEqual(restored.restore_from(backup), 1)
            conversations = restored.summaries()
            self.assertEqual(len(conversations), 2)
            self.assertEqual(
                sum(item["message_count"] for item in conversations),
                2,
            )

    def test_corrupt_conversation_is_quarantined_and_reported(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            trash = root / "trash"
            directory = root / "conversations"
            directory.mkdir()
            corrupt = directory / "broken.json"
            corrupt.write_text("{", encoding="utf-8")
            with patch("takoyo_agent.memory.TRASH_DIR", trash):
                store = ConversationStore(directory)
                self.assertEqual(store.summaries(), [])
                self.assertTrue(list((trash / "conversations" / "corrupt").glob("*.corrupt")))
                self.assertEqual(len(store.take_recovery_errors()), 1)

    def test_invalid_backup_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            backup = root / "invalid.json"
            backup.write_text(json.dumps({"conversations": []}), encoding="utf-8")
            store = ConversationStore(root / "conversations")
            with self.assertRaisesRegex(ValueError, "Unsupported"):
                store.restore_from(backup)


class ProjectPathRegressionTests(unittest.TestCase):
    def test_project_file_operations_reject_symlink_escapes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            store = ProjectStore(root / "projects")
            project = store.create("Safe project")
            outside_dir = root / "outside"
            outside_dir.mkdir()
            project_root = store.root(project["id"])
            link = project_root / "escape"
            try:
                _make_directory_link(link, outside_dir)
            except (OSError, NotImplementedError) as exc:
                self.skipTest(f"symlink creation unavailable: {exc}")

            (outside_dir / "outside.txt").write_text("outside", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "escapes"):
                store.read_text(project["id"], "escape/outside.txt")
            with self.assertRaisesRegex(ValueError, "escapes"):
                store.write_text(project["id"], "escape/outside.txt", "modified")
            self.assertEqual((outside_dir / "outside.txt").read_text(encoding="utf-8"), "outside")

    def test_project_root_symlink_cannot_escape_projects_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            projects_dir = root / "projects"
            projects_dir.mkdir()
            outside = root / "outside"
            outside.mkdir()
            store = ProjectStore(projects_dir)
            try:
                _make_directory_link(projects_dir / "linked", outside)
            except (OSError, NotImplementedError) as exc:
                self.skipTest(f"symlink creation unavailable: {exc}")

            with self.assertRaisesRegex(ValueError, "escapes"):
                store.safe_path("linked", "note.txt")


class StreamAndImageRegressionTests(unittest.TestCase):
    def test_stream_failure_event_keeps_partial_text(self):
        event = _stream_failure_event("chat", 2, "connection lost", "provider", ["partial ", "reply"])
        self.assertEqual(event[-1], "partial reply")

    def test_structured_diagram_requests_are_detected(self):
        request = "Draw a flowchart of the login process as an image"
        self.assertTrue(is_structured_visual_request(request))
        self.assertTrue(image_request_prompt(request))
        fallback = structured_visual_fallback(request, ["unrelated abstract composition"])
        self.assertIn("No image was generated", fallback)
        self.assertEqual(structured_visual_fallback(request, []), "")
        self.assertFalse(is_structured_visual_request("Paint a purple octopus"))


class PackagingRegressionTests(unittest.TestCase):
    def test_payload_preserves_relative_files_and_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / "stage"
            work = root / "work"
            (stage / "assets").mkdir(parents=True)
            (stage / "Takoyo.exe").write_bytes(b"native image")
            (stage / "assets" / "icon.bin").write_bytes(b"icon")
            work.mkdir()
            with patch.object(pack_single, "STAGE", stage), patch.object(pack_single, "WORK", work):
                payload = pack_single.build_payload(123)
            raw = payload.read_bytes()
            self.assertEqual(raw[:8], pack_single.MAGIC)
            build_id, count = struct.unpack("<II", raw[8:16])
            self.assertEqual((build_id, count), (123, 2))
            offset = 16
            entries = {}
            for _ in range(count):
                name_size = struct.unpack("<H", raw[offset:offset + 2])[0]
                offset += 2
                name = raw[offset:offset + name_size].decode("utf-8")
                offset += name_size
                original_size, packed_size = struct.unpack("<QQ", raw[offset:offset + 16])
                offset += 16
                content = zlib.decompress(raw[offset:offset + packed_size])
                offset += packed_size
                self.assertEqual(len(content), original_size)
                entries[name] = content
            self.assertEqual(entries["Takoyo.exe"], b"native image")
            self.assertEqual(entries["assets/icon.bin"], b"icon")


if __name__ == "__main__":
    unittest.main()
