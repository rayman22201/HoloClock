"""
HoloClock multi-source notification daemon.

Monitors Slack, Discord, Email (IMAP), Google Calendar / ICS feeds,
and receives SMS pushes from Android/Tasker — then sends ambient
notifications to the HoloClock ESP32.

Usage:
    python holoclock_notify.py --config config.yaml
"""

import argparse
import imaplib
import json
import select
import logging
import signal
import threading
import time
from collections import deque
from datetime import datetime, timedelta, timezone
from functools import partial
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

import requests
import yaml

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
)
log = logging.getLogger("holoclock")
logging.getLogger("googleapiclient.discovery_cache").setLevel(logging.ERROR)

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

def load_config(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)

# ---------------------------------------------------------------------------
# Dispatcher
# ---------------------------------------------------------------------------

def send_notification(
    ip: str,
    port: int,
    source: str,
    color: int,
    duration_ms: int = 0,
) -> bool:
    """POST to http://{ip}:{port}/notify"""
    url = f"http://{ip}:{port}/notify"
    payload: dict = {"source": source, "color": color}
    if duration_ms:
        payload["duration_ms"] = duration_ms
    try:
        resp = requests.post(url, json=payload, timeout=5)
        log.info("Notification sent: source=%s color=0x%06X (status %d)", source, color, resp.status_code)
        return resp.status_code == 200
    except requests.RequestException as e:
        log.error("Failed to send notification: %s", e)
        return False

# ---------------------------------------------------------------------------
# Device health monitor
# ---------------------------------------------------------------------------

class DeviceMonitor:
    """Background poller that tracks whether the HoloClock device is reachable."""

    def __init__(self, ip: str, port: int, poll_interval: int):
        self._url = f"http://{ip}:{port}/status"
        self._poll_interval = poll_interval
        self._online = True
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None

    @property
    def is_online(self) -> bool:
        with self._lock:
            return self._online

    def _set_online(self, state: bool) -> None:
        with self._lock:
            prev = self._online
            self._online = state
        if state != prev:
            label = "online" if state else "offline"
            log.info("[monitor] Device is now %s", label)

    def _run(self) -> None:
        log.info("[monitor] Started (polling %s every %ds)", self._url, self._poll_interval)
        while not self._stop_event.is_set():
            try:
                resp = requests.get(self._url, timeout=3)
                self._set_online(resp.status_code == 200)
            except requests.RequestException:
                self._set_online(False)
            self._stop_event.wait(self._poll_interval)

    def start(self) -> None:
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="device-monitor", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=5)
        log.info("[monitor] Stopped")

# ---------------------------------------------------------------------------
# Source base class
# ---------------------------------------------------------------------------

class Source:
    """Base class for all notification sources."""

    name: str = "base"

    def __init__(self, cfg: dict, dispatcher):
        self.cfg = cfg
        self.dispatcher = dispatcher
        self.source_char = cfg.get("source", "?")
        self.color = cfg.get("color", 0xFFFFFF)
        self.cooldown = cfg.get("cooldown", 30)
        self.duration_ms = cfg.get("duration_ms", 0)
        self._last_notify = 0.0
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None

    def _notify(self, duration_ms: int = 0) -> bool:
        now = time.time()
        if self.cooldown and now - self._last_notify < self.cooldown:
            log.debug("[%s] Cooldown active, skipping", self.name)
            return False
        self._last_notify = now
        return self.dispatcher(
            source=self.source_char,
            color=self.color,
            duration_ms=duration_ms or self.duration_ms,
        )

    def _run(self) -> None:
        raise NotImplementedError

    def start(self) -> None:
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name=f"src-{self.name}", daemon=True)
        self._thread.start()
        log.info("[%s] Started", self.name)

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=5)
        log.info("[%s] Stopped", self.name)

# ---------------------------------------------------------------------------
# Slack source
# ---------------------------------------------------------------------------

class SlackSource(Source):
    name = "slack"

    def __init__(self, cfg: dict, dispatcher):
        super().__init__(cfg, dispatcher)
        self._channel_id_map: dict[str, str] = {}
        self._user_id: str = ""

        # Mention filtering
        self._notify_mentions: bool = cfg.get("notify_mentions", True)
        self._notify_all: bool = cfg.get("notify_all", False)

        # Spike detection
        self._spike_detection: bool = cfg.get("spike_detection", False)
        self._spike_color: int = cfg.get("spike_color", 0xFFA500)
        self._spike_cooldown: int = cfg.get("spike_cooldown", 120)
        self._spike_window: int = cfg.get("spike_window", 60)
        self._spike_threshold: int = cfg.get("spike_threshold", 15)
        self._spike_length_threshold: int = cfg.get("spike_length_threshold", 2000)
        self._last_spike_notify: float = 0.0
        self._channel_timestamps: dict[str, deque] = {}
        self._spike_lock = threading.Lock()

    def _build_channel_map(self, web_client) -> None:
        wanted_names = self.cfg.get("channels") or []
        if not wanted_names:
            return
        wanted = set(wanted_names)
        cursor = None
        while True:
            resp = web_client.conversations_list(
                cursor=cursor, limit=200,
                types="public_channel,private_channel",
            )
            for ch in resp["channels"]:
                if ch["name"] in wanted:
                    self._channel_id_map[ch["id"]] = ch["name"]
            cursor = resp.get("response_metadata", {}).get("next_cursor")
            if not cursor:
                break
        log.info("[slack] Monitoring channels: %s", self._channel_id_map)

    def _is_mention(self, text: str) -> bool:
        """Check if text contains @here, @everyone, @channel, or @user."""
        if "<!here>" in text or "<!everyone>" in text or "<!channel>" in text:
            return True
        if self._user_id and f"<@{self._user_id}>" in text:
            return True
        return False

    def _notify_spike(self, duration_ms: int = 0) -> bool:
        """Send spike notification with independent cooldown."""
        now = time.time()
        if self._spike_cooldown and now - self._last_spike_notify < self._spike_cooldown:
            log.debug("[slack] Spike cooldown active, skipping")
            return False
        self._last_spike_notify = now
        return self.dispatcher(
            source=self.source_char,
            color=self._spike_color,
            duration_ms=duration_ms or self.duration_ms,
        )

    def _record_and_check_spike(self, channel_id: str, text: str) -> bool:
        """Record message timestamp and check for activity spike."""
        with self._spike_lock:
            # Large single message → immediate spike
            if len(text) >= self._spike_length_threshold:
                log.info("[slack] Spike: large message (%d chars) in %s",
                         len(text), channel_id)
                return True

            # Sliding window burst detection
            now = time.time()
            if channel_id not in self._channel_timestamps:
                self._channel_timestamps[channel_id] = deque()
            dq = self._channel_timestamps[channel_id]
            dq.append(now)

            # Prune entries older than the window
            cutoff = now - self._spike_window
            while dq and dq[0] < cutoff:
                dq.popleft()

            if len(dq) >= self._spike_threshold:
                log.info("[slack] Spike: %d messages in %ds window in %s",
                         len(dq), self._spike_window, channel_id)
                return True

        return False

    def _handle_event(self, sm_client, req) -> None:
        from slack_sdk.socket_mode.response import SocketModeResponse
        sm_client.send_socket_mode_response(
            SocketModeResponse(envelope_id=req.envelope_id)
        )
        if req.type != "events_api":
            return
        event = req.payload.get("event", {})
        if event.get("type") != "message" or event.get("subtype"):
            return

        # Channel filter
        if self._channel_id_map:
            if event.get("channel", "") not in self._channel_id_map:
                return

        # Skip own messages
        if self._user_id and event.get("user") == self._user_id:
            return

        text = event.get("text", "")

        # Path 1: Mention / all-message notification
        if self._notify_all or (self._notify_mentions and self._is_mention(text)):
            self._notify()

        # Path 2: Spike detection (independent, fires on same message)
        if self._spike_detection and self._record_and_check_spike(
            event.get("channel", ""), text
        ):
            self._notify_spike()

    def _run(self) -> None:
        from slack_sdk.web import WebClient
        from slack_sdk.socket_mode import SocketModeClient

        web_client = WebClient(token=self.cfg["bot_token"])
        self._build_channel_map(web_client)

        # Fetch authenticated user ID for mention matching
        try:
            self._user_id = web_client.auth_test()["user_id"]
            log.info("[slack] Authenticated as user %s", self._user_id)
        except Exception as e:
            log.warning("[slack] Could not fetch user ID: %s", e)

        sm_client = SocketModeClient(
            app_token=self.cfg["app_token"],
            web_client=web_client,
        )
        sm_client.socket_mode_request_listeners.append(self._handle_event)
        sm_client.connect()
        log.info("[slack] Connected via Socket Mode")

        self._stop_event.wait()
        sm_client.disconnect()

# ---------------------------------------------------------------------------
# Discord source
# ---------------------------------------------------------------------------

class DiscordSource(Source):
    name = "discord"

    def _run(self) -> None:
        import asyncio
        import discord

        cfg = self.cfg
        wanted_channels = set(cfg.get("channels") or [])
        notify_dms = cfg.get("notify_dms", True)
        notify_mentions = cfg.get("notify_mentions", True)
        client = discord.Client()

        @client.event
        async def on_ready():
            log.info("[discord] Logged in as %s", client.user)

        @client.event
        async def on_message(message: discord.Message):
            if message.author == client.user:
                return

            is_dm = isinstance(message.channel, discord.DMChannel)
            is_mention = client.user in message.mentions

            # DM notification
            if is_dm and notify_dms:
                self._notify()
                return

            # @mention notification
            if is_mention and notify_mentions:
                self._notify()
                return

            # Watched channel notification (only if channels list is non-empty)
            if wanted_channels and not is_dm and message.channel.name in wanted_channels:
                self._notify()
                return

        loop = asyncio.new_event_loop()

        def run_bot():
            asyncio.set_event_loop(loop)
            loop.run_until_complete(client.start(cfg["token"]))

        bot_thread = threading.Thread(target=run_bot, daemon=True)
        bot_thread.start()

        self._stop_event.wait()
        asyncio.run_coroutine_threadsafe(client.close(), loop)

# ---------------------------------------------------------------------------
# Email / IMAP source
# ---------------------------------------------------------------------------

class EmailSource(Source):
    name = "email"

    def _run(self) -> None:
        cfg = self.cfg
        host = cfg["imap_host"]
        port = cfg.get("imap_port", 993)
        user = cfg["username"]
        pwd = cfg["password"]
        folders = cfg.get("folders", ["INBOX"])

        while not self._stop_event.is_set():
            mail = None
            try:
                mail = imaplib.IMAP4_SSL(host, port)
                mail.login(user, pwd)
                log.info("[email] Connected to %s", host)

                for folder in folders:
                    mail.select(folder)

                # Try IDLE-based push
                if self._supports_idle(mail):
                    self._idle_loop(mail, folders)
                else:
                    self._poll_loop(mail, folders)

            except Exception as e:
                log.error("[email] Error: %s — reconnecting in 30s", e)
                self._stop_event.wait(30)
            finally:
                if mail:
                    try:
                        mail.logout()
                    except Exception:
                        pass

    def _supports_idle(self, mail: imaplib.IMAP4_SSL) -> bool:
        try:
            typ, caps = mail.capability()
            if typ == "OK" and caps:
                return b"IDLE" in caps[0].upper().split()
        except Exception:
            pass
        return False

    def _idle_loop(self, mail: imaplib.IMAP4_SSL, folders: list[str]) -> None:
        log.info("[email] Using IMAP IDLE")
        folder = folders[0]
        mail.select(folder)
        # Mark current state so we only see NEW messages
        self._get_unseen_count(mail)

        while not self._stop_event.is_set():
            try:
                tag = mail._new_tag().decode()
                mail.send(f"{tag} IDLE\r\n".encode())
                # Wait for EXISTS or up to 29 minutes (RFC recommends <29m)
                mail.readline()  # + idling

                deadline = time.time() + 29 * 60
                got_exists = False
                while time.time() < deadline and not self._stop_event.is_set():
                    ready = select.select([mail.sock], [], [], 30)
                    if not ready[0]:
                        continue  # 30s poll, check stop_event / deadline
                    line = mail.readline()
                    if b"EXISTS" in line:
                        got_exists = True
                        break

                # Send DONE to end IDLE
                mail.send(b"DONE\r\n")
                mail.readline()  # tagged OK response

                if got_exists:
                    unseen = self._get_unseen_count(mail)
                    if unseen > 0:
                        log.info("[email] %d new unseen message(s)", unseen)
                        self._notify()

            except Exception as e:
                log.error("[email] IDLE error: %s", e)
                break

    def _poll_loop(self, mail: imaplib.IMAP4_SSL, folders: list[str]) -> None:
        log.info("[email] IDLE not supported, polling every 30s")
        prev_unseen: dict[str, int] = {}
        for folder in folders:
            mail.select(folder)
            prev_unseen[folder] = self._get_unseen_count(mail)

        while not self._stop_event.is_set():
            self._stop_event.wait(30)
            if self._stop_event.is_set():
                break
            for folder in folders:
                try:
                    mail.select(folder)
                    count = self._get_unseen_count(mail)
                    if count > prev_unseen.get(folder, 0):
                        log.info("[email] New mail in %s (%d unseen)", folder, count)
                        self._notify()
                    prev_unseen[folder] = count
                except Exception as e:
                    log.error("[email] Poll error on %s: %s", folder, e)
                    raise  # trigger reconnect

    @staticmethod
    def _get_unseen_count(mail: imaplib.IMAP4_SSL) -> int:
        typ, data = mail.search(None, "UNSEEN")
        if typ != "OK" or not data[0]:
            return 0
        return len(data[0].split())

# ---------------------------------------------------------------------------
# Calendar source (ICS + Google)
# ---------------------------------------------------------------------------

class CalendarSource(Source):
    name = "calendar"

    def __init__(self, cfg: dict, dispatcher):
        super().__init__(cfg, dispatcher)
        self._notified_events: set[str] = set()

    def _run(self) -> None:
        cal_type = self.cfg.get("type", "ics")
        poll = self.cfg.get("poll_interval", 60)

        while not self._stop_event.is_set():
            try:
                if cal_type == "ics":
                    self._check_ics()
                elif cal_type == "google":
                    self._check_google()
            except Exception as e:
                log.error("[calendar] Error: %s", e)
            self._stop_event.wait(poll)

    def _check_ics(self) -> None:
        from icalendar import Calendar

        url = self.cfg.get("ics_url", "")
        if not url:
            return
        resp = requests.get(url, timeout=15)
        resp.raise_for_status()
        cal = Calendar.from_ical(resp.content)
        now = datetime.now(timezone.utc)
        reminder_minutes = self.cfg.get("reminder_minutes", [5])

        for component in cal.walk():
            if component.name != "VEVENT":
                continue
            dtstart = component.get("dtstart")
            if not dtstart:
                continue
            start = dtstart.dt
            if not hasattr(start, "hour"):
                # All-day event, skip
                continue
            if start.tzinfo is None:
                start = start.replace(tzinfo=timezone.utc)

            uid = str(component.get("uid", ""))
            event_key = f"{uid}_{start.isoformat()}"

            for mins in reminder_minutes:
                trigger_time = start - timedelta(minutes=mins)
                reminder_key = f"{event_key}_{mins}"
                if reminder_key in self._notified_events:
                    continue
                if trigger_time <= now < start:
                    summary = str(component.get("summary", "event"))
                    log.info("[calendar] Reminder: %s in %d min", summary, mins)
                    self._notify()
                    self._notified_events.add(reminder_key)

        # Prune old entries (events more than 1 day past)
        cutoff = now - timedelta(days=1)
        self._notified_events = {
            k for k in self._notified_events
            if self._event_key_recent(k, cutoff)
        }

    @staticmethod
    def _event_key_recent(key: str, cutoff: datetime) -> bool:
        # key format: uid_isodate_minutes
        parts = key.rsplit("_", 2)
        if len(parts) < 2:
            return False
        try:
            dt = datetime.fromisoformat(parts[-2])
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            return dt > cutoff
        except (ValueError, IndexError):
            return True  # keep if we can't parse

    def _check_google(self) -> None:
        try:
            from google.oauth2.credentials import Credentials
            from google_auth_oauthlib.flow import InstalledAppFlow
            from google.auth.transport.requests import Request as GRequest
            from googleapiclient.discovery import build
        except ImportError:
            log.error("[calendar] Google Calendar deps not installed. "
                      "Run: pip install google-api-python-client google-auth-oauthlib")
            self._stop_event.set()
            return

        creds_path = self.cfg.get("google_credentials", "")
        token_path = Path(creds_path).parent / "token.json" if creds_path else Path("token.json")
        scopes = ["https://www.googleapis.com/auth/calendar.readonly"]

        creds = None
        if token_path.exists():
            creds = Credentials.from_authorized_user_file(str(token_path), scopes)
        if not creds or not creds.valid:
            if creds and creds.expired and creds.refresh_token:
                creds.refresh(GRequest())
            else:
                flow = InstalledAppFlow.from_client_secrets_file(creds_path, scopes)
                creds = flow.run_local_server(port=0)
            with open(token_path, "w") as f:
                f.write(creds.to_json())

        service = build("calendar", "v3", credentials=creds)
        now = datetime.now(timezone.utc)
        reminder_minutes = self.cfg.get("reminder_minutes", [5])
        max_lookahead = max(reminder_minutes) + 5
        time_min = now.isoformat()
        time_max = (now + timedelta(minutes=max_lookahead)).isoformat()

        events_result = service.events().list(
            calendarId="primary",
            timeMin=time_min,
            timeMax=time_max,
            singleEvents=True,
            orderBy="startTime",
        ).execute()

        for event in events_result.get("items", []):
            start_str = event["start"].get("dateTime")
            if not start_str:
                continue
            start = datetime.fromisoformat(start_str)
            if start.tzinfo is None:
                start = start.replace(tzinfo=timezone.utc)

            uid = event.get("id", "")
            event_key = f"{uid}_{start.isoformat()}"

            for mins in reminder_minutes:
                trigger_time = start - timedelta(minutes=mins)
                reminder_key = f"{event_key}_{mins}"
                if reminder_key in self._notified_events:
                    continue
                if trigger_time <= now < start:
                    log.info("[calendar] Reminder: %s in %d min",
                             event.get("summary", "event"), mins)
                    self._notify()
                    self._notified_events.add(reminder_key)

# ---------------------------------------------------------------------------
# SMS source (push receiver — no thread, handled by push server)
# ---------------------------------------------------------------------------

class SMSSource(Source):
    name = "sms"

    def start(self) -> None:
        # No dedicated thread — the push server calls handle_post directly
        log.info("[sms] Registered (push receiver, no dedicated thread)")

    def stop(self) -> None:
        log.info("[sms] Stopped")

    def handle_post(self, data: dict) -> None:
        sender = data.get("from", "unknown")
        body = data.get("body", "")
        log.info("[sms] Message from %s: %s", sender, body[:80])
        self._notify()

# ---------------------------------------------------------------------------
# Push HTTP server
# ---------------------------------------------------------------------------

class PushHandler(BaseHTTPRequestHandler):
    sms_source: SMSSource | None = None

    def do_POST(self):
        if self.path == "/sms":
            self._handle_sms()
        else:
            self.send_error(404)

    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"OK")
        else:
            self.send_error(404)

    def _handle_sms(self):
        if not self.sms_source:
            self.send_error(503, "SMS source not configured")
            return
        content_length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(content_length)
        try:
            data = json.loads(raw)
        except (json.JSONDecodeError, ValueError):
            self.send_error(400, "Invalid JSON")
            return

        self.sms_source.handle_post(data)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"status": "ok"}).encode())

    def log_message(self, format, *args):
        log.debug("[push] %s", format % args)


def start_push_server(port: int, sms_source: SMSSource | None) -> HTTPServer:
    PushHandler.sms_source = sms_source
    server = HTTPServer(("0.0.0.0", port), PushHandler)
    thread = threading.Thread(target=server.serve_forever, name="push-server", daemon=True)
    thread.start()
    log.info("Push HTTP server listening on :%d", port)
    return server

# ---------------------------------------------------------------------------
# Source registry
# ---------------------------------------------------------------------------

SOURCE_REGISTRY: dict[str, type[Source]] = {
    "slack": SlackSource,
    "discord": DiscordSource,
    "email": EmailSource,
    "calendar": CalendarSource,
    "sms": SMSSource,
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="HoloClock notification daemon")
    parser.add_argument(
        "--config",
        default=str(Path(__file__).parent / "config.yaml"),
        help="Path to config.yaml (default: same directory as script)",
    )
    args = parser.parse_args()

    config = load_config(args.config)
    holo = config["holoclock"]
    ip, port = holo["ip"], holo["port"]
    poll_interval = holo.get("status_poll_interval", 10)

    if poll_interval > 0:
        monitor = DeviceMonitor(ip, port, poll_interval)
        monitor.start()
        raw_dispatcher = partial(send_notification, ip, port)
        def dispatcher(**kwargs) -> bool:
            if not monitor.is_online:
                log.debug("Device offline — dropping notification: %s", kwargs)
                return False
            return raw_dispatcher(**kwargs)
    else:
        monitor = None
        dispatcher = partial(send_notification, ip, port)

    sources: list[Source] = []
    sms_source: SMSSource | None = None

    for name, cfg in config.get("sources", {}).items():
        if not cfg.get("enabled"):
            continue
        cls = SOURCE_REGISTRY.get(name)
        if cls is None:
            log.warning("Unknown source: %s — skipping", name)
            continue
        src = cls(cfg, dispatcher)
        sources.append(src)
        if isinstance(src, SMSSource):
            sms_source = src

    if not sources:
        log.warning("No sources enabled — nothing to do")
        return

    # Start push server if needed
    push_cfg = config.get("push_server", {})
    push_server = None
    if push_cfg.get("enabled") or sms_source:
        push_port = push_cfg.get("port", 8090)
        push_server = start_push_server(push_port, sms_source)

    # Start all sources
    for src in sources:
        src.start()

    log.info("Daemon running — %d source(s) active. Press Ctrl+C to stop.", len(sources))

    # Block until interrupted
    shutdown = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: shutdown.set())
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda *_: shutdown.set())
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, lambda *_: shutdown.set())
    while not shutdown.is_set():
        shutdown.wait(timeout=1)

    log.info("Shutting down...")
    if monitor:
        monitor.stop()
    for src in sources:
        src.stop()
    if push_server:
        push_server.shutdown()
    log.info("Goodbye")


if __name__ == "__main__":
    main()
