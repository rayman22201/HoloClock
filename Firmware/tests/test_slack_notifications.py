"""Offline Slack event regressions; no provider or device connections."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

import pytest

SPEC = importlib.util.spec_from_file_location(
    "notify_slack", Path(__file__).resolve().parents[1] / "tools/holoclock_notify.py")
notify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notify)


def source(**settings):
    sent, acknowledged = [], []
    config = {"notify_mentions": True, "notify_all": False, "cooldown": 0,
              "spike_detection": False, **settings}
    instance = notify.SlackSource(config, lambda **kw: sent.append(kw) or True)
    instance._user_id = "U_TARGET"
    client = SimpleNamespace(send_socket_mode_response=acknowledged.append)
    return instance, client, sent, acknowledged


def deliver(instance, client, text, **fields):
    event = {"type": "message", "user": "U_OTHER", "channel": "C_FIXTURE",
             "text": text, **fields}
    instance._handle_event(client, SimpleNamespace(
        type="events_api", envelope_id="fixture", payload={"event": event}))


@pytest.mark.parametrize("text", ["hello <@U_TARGET>", "hello <@U_TARGET|ray>"])
@pytest.mark.parametrize("subtype", [None, "bot_message"])
def test_target_mention_in_new_message_dispatches(text, subtype):
    instance, client, sent, acknowledged = source()
    deliver(instance, client, text, subtype=subtype)
    assert len(sent) == 1
    assert len(acknowledged) == 1


@pytest.mark.parametrize("text", [
    "<@U_OTHER|ray>", "<@U_TARGET_EXTRA>", "<@U_TARGET_EXTRA|ray>",
    "@ray", "<@U_TARGET", "<@U_TARGET|>", "<@U_TARGET|ray", "<@U_TARGET|ra<y>",
])
def test_wrong_id_or_malformed_mention_does_not_dispatch(text):
    instance, client, sent, acknowledged = source()
    deliver(instance, client, text)
    assert not sent
    assert len(acknowledged) == 1


@pytest.mark.parametrize("subtype", ["message_changed", "message_deleted", "channel_join"])
def test_nonposting_subtypes_remain_filtered(subtype):
    instance, client, sent, acknowledged = source()
    deliver(instance, client, "<@U_TARGET|ray> fixture", subtype=subtype)
    assert not sent
    assert len(acknowledged) == 1


def test_own_user_and_own_bot_are_filtered_but_other_bot_is_allowed():
    instance, client, sent, _ = source()
    instance._bot_id = "B_OWN"
    deliver(instance, client, "<@U_TARGET> fixture", user="U_TARGET")
    deliver(instance, client, "<@U_TARGET> fixture", user=None,
            subtype="bot_message", bot_id="B_OWN")
    assert not sent
    deliver(instance, client, "<@U_TARGET> fixture", user=None,
            subtype="bot_message", bot_id="B_OTHER")
    assert len(sent) == 1


def test_missing_bot_identity_does_not_filter_external_bot():
    instance, client, sent, _ = source()
    deliver(instance, client, "<@U_TARGET> fixture", user=None, subtype="bot_message")
    assert len(sent) == 1


def test_cooldown_and_all_message_behavior_remain(monkeypatch):
    monkeypatch.setattr(notify.time, "time", lambda: 1000)
    instance, client, sent, _ = source(cooldown=30, notify_all=True)
    deliver(instance, client, "ordinary fixture", subtype="bot_message")
    deliver(instance, client, "<@U_TARGET|ray> fixture")
    assert len(sent) == 1


def test_spike_path_remains_independent():
    instance, client, sent, _ = source(spike_detection=True, spike_threshold=2)
    deliver(instance, client, "ordinary fixture")
    assert not sent
    deliver(instance, client, "ordinary fixture", subtype="bot_message")
    assert len(sent) == 1


def test_authenticated_bot_identity_is_retained(monkeypatch):
    import slack_sdk.web
    import slack_sdk.socket_mode
    class Web:
        def __init__(self, **kwargs):
            pass
        def auth_test(self):
            return {"user_id": "U_TARGET", "bot_id": "B_OWN"}
    class Socket:
        def __init__(self, **kwargs):
            self.socket_mode_request_listeners = []
        def connect(self):
            pass
        def disconnect(self):
            pass
    monkeypatch.setattr(slack_sdk.web, "WebClient", Web)
    monkeypatch.setattr(slack_sdk.socket_mode, "SocketModeClient", Socket)
    instance, _, _, _ = source(bot_token="fixture", app_token="fixture")
    instance._stop_event.set()
    instance._run()
    assert instance._bot_id == "B_OWN"
