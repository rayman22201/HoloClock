"""Offline daemon checks; only HTTP fixtures bind a loopback ephemeral port."""
import importlib.util
import json
import socket
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import urlopen, Request

SPEC = importlib.util.spec_from_file_location(
    "notify", Path(__file__).resolve().parents[1] / "tools/holoclock_notify.py")
notify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notify)


class WaitingSource(notify.Source):
    name = "fixture"

    def _run(self):
        self._mark_ready()
        self._stop_event.wait()


class HealthTests(unittest.TestCase):
    def ready_source(self):
        src = WaitingSource({}, lambda **kw: True)
        src.start()
        self.addCleanup(src.stop)
        deadline = time.monotonic() + 2
        while src.health()["state"] != "ready" and time.monotonic() < deadline:
            time.sleep(.01)
        return src

    def test_worker_readiness_failure_and_stop(self):
        src = self.ready_source()
        health = notify.health_snapshot([src], None)
        self.assertEqual(health["status"], "ok")
        self.assertEqual(health["service"], "holoclock-notify")
        self.assertEqual(health["schema_version"], 1)
        src.stop()
        self.assertEqual(notify.health_snapshot([src], None)["status"], "degraded")
        self.assertFalse(src.health()["worker_alive"])

    def test_exception_reports_type_without_secret(self):
        class FailedSource(notify.Source):
            def _run(self):
                raise ValueError("private token fixture")
        src = FailedSource({}, lambda **kw: True)
        src.start()
        src._thread.join(2)
        self.assertEqual(src.health()["state"], "error")
        self.assertEqual(src.health()["error_type"], "ValueError")
        self.assertNotIn("private", json.dumps(src.health()))

    def test_concurrent_notifications_keep_cooldown_without_blocking_health(self):
        dispatching, release = threading.Event(), threading.Event()
        calls = []
        def dispatch(**kwargs):
            calls.append(kwargs)
            dispatching.set()
            release.wait(2)
            return True
        src = notify.SMSSource({"cooldown": 30}, dispatch)
        worker = threading.Thread(target=src._notify)
        worker.start()
        try:
            self.assertTrue(dispatching.wait(1))
            self.assertFalse(src._notify())
            self.assertEqual(src.health()["state"], "starting")
            self.assertEqual(len(calls), 1)
        finally:
            release.set()
            worker.join(2)

    def test_imap_failed_search_is_not_readiness_success(self):
        class Mail:
            def search(self, *args):
                return "NO", [b"fixture failure"]
        with self.assertRaises(RuntimeError):
            notify.EmailSource._get_unseen_count(Mail())

    def test_monitor_unknown_fresh_offline_and_stale(self):
        src = self.ready_source()
        monitor = notify.DeviceMonitor("unused", 81, 10)
        monitor._thread = threading.current_thread()
        self.assertIsNone(monitor.health()["online"])
        self.assertTrue(monitor.health()["stale"])
        self.assertEqual(notify.health_snapshot([src], monitor)["status"], "degraded")
        monitor._set_online(False)
        self.assertEqual(notify.health_snapshot([src], monitor)["status"], "ok")
        with patch.object(notify.time, "time", return_value=time.time() + 40):
            self.assertTrue(monitor.health()["stale"])
            self.assertEqual(notify.health_snapshot([src], monitor)["status"], "degraded")
        disabled = notify.health_snapshot([src], None)["device"]
        self.assertFalse(disabled["enabled"])
        self.assertIsNone(disabled["checked_at"])
        self.assertIsNone(disabled["online"])

    def test_observation_during_snapshot_never_postdates_generated_at(self):
        monitor = notify.DeviceMonitor("unused", 81, 10)
        original = monitor.health
        with patch.object(notify.time, "time", return_value=100) as clock:
            def concurrent_observation(*args):
                clock.return_value = 101
                monitor._set_online(False)
                return original(*args)
            with patch.object(monitor, "health", side_effect=concurrent_observation):
                result = notify.health_snapshot([], monitor)
        self.assertLessEqual(result["device"]["checked_at"], result["generated_at"])
        self.assertEqual(result["device"]["age_seconds"],
                         result["generated_at"] - result["device"]["checked_at"])

    def test_discord_inner_thread_failure_is_not_hidden(self):
        import discord
        class FailingClient:
            def event(self, function):
                return function
            async def start(self, token):
                raise RuntimeError("private login detail")
            async def close(self):
                pass
        with patch.object(discord, "Client", FailingClient):
            src = notify.DiscordSource({"token": "fixture"}, lambda **kw: True)
            src.start()
            self.addCleanup(src.stop)
            deadline = time.monotonic() + 2
            while src.health()["state"] != "error" and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(src.health()["state"], "error")
            self.assertFalse(src.health()["worker_alive"])
            self.assertEqual(notify.health_snapshot([src], None)["status"], "degraded")

    def test_google_headless_never_opens_browser(self):
        from google_auth_oauthlib.flow import InstalledAppFlow
        src = notify.CalendarSource({"type": "google", "google_interactive_auth": False,
                                     "google_credentials": "/nonexistent/fixture/credentials.json"},
                                    lambda **kw: True)
        with patch.object(notify.Path, "exists", return_value=False), \
                patch.object(InstalledAppFlow, "from_client_secrets_file") as flow:
            with self.assertRaisesRegex(RuntimeError, "interactive"):
                src._check_google()
            flow.assert_not_called()
            src.start()
            self.addCleanup(src.stop)
            deadline = time.monotonic() + 2
            while src.health()["state"] != "error" and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(src.health()["state"], "error")
            self.assertEqual(src.health()["error_type"], "RuntimeError")

    def test_sms_request_bounds_and_timeout(self):
        sms = notify.SMSSource({}, lambda **kw: True)
        server, url = self.server(sms, [sms])
        sms.start()
        for data, status in [(b'x' * 16385, 413), (b'{"body":42}', 400), (b'\xff', 400)]:
            with self.assertRaises(HTTPError) as error:
                urlopen(Request(url + "/sms", data=data), timeout=3)
            self.assertEqual(error.exception.code, status)
        with socket.create_connection(("127.0.0.1", server.server_port), timeout=4) as connection:
            connection.sendall(b"POST /sms HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nx")
            self.assertIn(b"408", connection.recv(4096).split(b"\r\n")[0])

    def server(self, sms, sources):
        server = notify.start_push_server(0, sms, lambda: notify.health_snapshot(sources, None),
                                          host="127.0.0.1")
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return server, f"http://127.0.0.1:{server.server_port}"

    def test_health_without_sms_and_empty_sources_degraded(self):
        src = self.ready_source()
        server, url = self.server(None, [src])
        with urlopen(url + "/health", timeout=2) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(json.load(response)["sources"]["fixture"]["state"], "ready")
        src.stop()
        with self.assertRaises(HTTPError) as error:
            urlopen(url + "/health", timeout=2)
        self.assertEqual(error.exception.code, 503)
        self.assertEqual(notify.health_snapshot([], None)["status"], "degraded")

    def test_sms_valid_invalid_and_partial_request_does_not_block_health_or_shutdown(self):
        delivered = []
        sms = notify.SMSSource({"cooldown": 0}, lambda **kw: delivered.append(kw) or True)
        server, url = self.server(sms, [sms])
        sms.start()
        with urlopen(Request(url + "/sms", data=b'{"from":"fixture","body":"hello"}'), timeout=2) as response:
            self.assertEqual(response.status, 200)
        self.assertEqual(len(delivered), 1)
        with self.assertRaises(HTTPError) as error:
            urlopen(Request(url + "/sms", data=b'[]'), timeout=2)
        self.assertEqual(error.exception.code, 400)
        partial = socket.create_connection(("127.0.0.1", server.server_port), timeout=2)
        self.addCleanup(partial.close)
        partial.sendall(b"POST /sms HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\nx")
        with urlopen(url + "/health", timeout=2) as response:
            self.assertEqual(response.status, 200)
        started = time.monotonic()
        server.shutdown()
        server.server_close()
        self.assertLess(time.monotonic() - started, 2)
        self.assertFalse(sms.health()["worker_alive"])


if __name__ == "__main__":
    unittest.main()
