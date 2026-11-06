# HoloClock homelab migration

<!-- plan-review: complete: 2026-11-05 -->

## Scope and decisions

Move the notification daemon from the Windows PC to core@192.168.1.170 as a
rootless Podman Quadlet, using the existing homelab Ethernet readiness drop-in.
Keep firmware and Board unchanged. Retain enabled notification sources and
filters. Do not bundle the earlier calendar/filtering fixes into this migration.

Example: a stopped Slack worker makes GET /health return 503 with a named
worker failure, and the homelab watchdog records holoclock:daemon. A sleeping
clock is reported separately as unreachable, not as daemon failure. A live
worker is not proof that its provider authenticated or that an alert appeared.

## Implementation

1. Firmware owns daemon source, Linux Containerfile, pinned direct deployment
   requirements, and local tests. Add a credential-free JSON health endpoint
   reporting configured source worker liveness and monitor observation age.
   Enable this endpoint independently of SMS. Preserve SMS request behavior.
   An unhealthy worker must be visible; do not claim thread liveness verifies
   provider connectivity. Existing interactive Google authorization must be
   disabled for the headless deployment, failing visibly if refresh cannot work.
   Track Discord's inner client thread, not only its waiting outer thread.
   Use concurrent HTTP handling with bounded request size/read timeout so a
   partial SMS request cannot block health or shutdown.
2. rays-media-server owns services/holoclock/holoclock.container and its runbook.
   Use a versioned localhost image, persistent /var/home/core/holoclock/state,
   SELinux Z labeling, non-root keep-id mapping, LAN-only port 8090, restart
   policy, and default.target startup. Give Python SIGTERM and >=40s stop time.
   No change to bare-metal provisioning or Board's container.
3. backup-review owns the existing watchdog/dashboard integration. Add a
   bounded read-only HTTP health check and holoclock:daemon dashboard tile/link.
   Reject malformed/missing health data. Keep the existing state.json
   dedup/recovery and Health checked freshness behavior. Preserve unrelated
   dirty files in all three repositories.
4. Build the image in an isolated staging directory on the homelab, without
   credentials in its context. Verify imports and tests before service start.
   Copy the private configuration and OAuth files over SSH only; set private
   permissions and rewrite credential paths for /state. Never print credentials.
5. Before deployment, compare live watchdog/dashboard files with local source;
   do not replace divergent production files wholesale. Back up touched live
   files, install the Quadlet, validate generation, and start only HoloClock.
   Install watchdog integration before publishing the dashboard tile. Wait for
   its next ordinary scheduled successful run before publishing the tile, so
   a fresh pre-migration state file cannot falsely imply HoloClock was checked.
   Validate image identity, process, /health, provider initialization outcomes,
   and dashboard integration. Do not claim physical alert rendering without
   observation. Do not invoke watchdog notification sending as a test.

## Health contract (version 1)

GET /health returns service="holoclock-notify", schema_version=1, generated_at
(Unix seconds), status="ok" or "degraded", sources (name-keyed objects with
worker_alive boolean and state starting/ready/error/stopped), and device.
Device contains enabled, worker_alive, online (boolean or null), checked_at,
age_seconds, and stale. Disabled monitoring has null observation fields and is
not a failure. Enabled monitoring is stale before its first observation or
after max(30, 3 * poll_interval + 3) seconds. An offline but freshly checked
clock does not degrade the daemon. Sources must be nonempty and each ready
and alive for status ok; SMS readiness refers to the HTTP receiver. Provider
readiness is last observed initialization/poll success, not proof of delivery.
503 means degraded; 200 means this bounded health contract passed. No secrets,
message content, account identities, or raw exception strings appear in health.
The watchdog requires exact service/schema, a current generated_at (<=120s),
valid source/device shapes, and status ok. It also checks the user service.

## Verification and limits

Tests cover healthy/dead workers, SMS-only operation, monitor unknown/stale
observations, malformed health response, HTTP failure, and unchanged dashboard
freshness behavior. Container checks exercise the actual Linux image without
connecting sources. Commissioning checks must distinguish source authentication
from worker liveness and device HTTP acceptance from physical presentation.
Rollback stops/removes the new service and restores backed-up monitoring files;
it does not restart unrelated services. Server reboot is not part of this work.

## Evidence

Preflight: server Fedora CoreOS, lingering core user, existing rootless Quadlets;
server-to-clock GET /status returned 200. Host port 8090 was unused. Local Windows
and running Ubuntu WSL had no matching notification daemon process.

### Deployed 2026-11-06 UTC (2026-11-05 Pacific)

- Firmware base: main, 0f20be42454cc6cd0c2eda71ced5e0505ce11144. Daemon source
  SHA256: 6621ff972c8dbf8719bbfe6f04a5b3e233fa323c3f2df351e54c3bd1160aa004.
- rays-media-server base: docs/front-door-hardening,
  11a82790b6b46159e7caf0c5f3e28e1ad93501cf. Existing README/network work preserved.
- backup-review base: main, 014e28a4f3f23deaca6a572671a3783c314421c8. Existing
  dashboard freshness and test edits preserved; HoloClock adds a separate test file.
- nas-backup base: main, 5443cd72de16d59e3c6ba94827bb03eeb492d97f. Added private
  HoloClock state to existence-checked backup paths and explicit restore paths.
- Image: localhost/holoclock-notify:20261105-v1,
  ID 54b41901a8d1248466cf83a579aa5081f243ba500d3f9cf92211ef70e3122c0c.
  Built on the homelab from five explicitly selected nonsecret files. Linux image
  tests: 11 passed, exit 0. Non-root final image contains no pytest or credentials.
- Windows daemon tests: 11 passed, exit 0. Monitoring tests plus existing
  watchdog/dashboard suites: 112 passed, exit 0. Backup/restore bash -n: exit 0.
  Actual daemon health producer -> watchdog consumer check accepted fresh offline
  state and rejected a stopped worker. No real notifications sent by these tests.
- Quadlet dry run passed and showed shared Ethernet prerequisite inheritance.
  The shared drop-in makes the effective RestartSec 10 seconds.
- Private state is core-owned, directory 700, files 600, outside the build context.
- Initial service start 06:16:50 UTC exposed Google invalid_grant. User completed
  fresh local Google authorization; the refreshed token was transferred over SSH
  without printing or committing it. Calendar recovered through normal polling.
- Deployment adjustment from observed behavior: HealthOnFailure=none. Provider
  degradation is monitored without restarting working sources. Restart=always
  still covers process exits. The policy update took effect with one intentional
  restart at 06:19:50 UTC; MainPID 2654041 and NRestarts=0 at 06:24:18 UTC.
- Live /health returned 200, all five sources ready, clock reachable. Sanitized
  log checks confirmed Slack authentication/Socket Mode, Discord login, and IMAP
  IDLE. Calendar readiness followed a successful API poll; SMS is receiver-ready.
- Installed daemon source hash matched the source above. Installed root-context
  watchdog unit/HTTP probes and classifier passed without calling watchdog main.
- Live monitoring and backup files matched baseline local hashes before install.
  Backups are in /var/data/alerts/holoclock-rollback-20261105; staging is private
  /var/home/core/holoclock-staging-20261105-v1. Installed watchdog adoption time
  was 1793946021.8976243. The ordinary scheduled run wrote state at
  1793946622.9127772 (06:30:22 UTC), exited successfully, and reported no
  holoclock:daemon alert. Only then was the dashboard installed and rendered.
  Installed dashboard SHA256:
  c6adb525a67f1df75e98cde77efbdc8eb5bffb2dfdc786e65263bb8da83bc498.
- One synthetic SMS was sent through the deployed server while the user watched.
  SMS ingress returned HTTP 200; daemon journal confirmed its POST to the device
  returned HTTP 200. Existing SMS configuration is an orange T (0xff6600) for
  20 seconds. The user confirmed both badge and LEDs appeared and dismissed.
  This verifies this controlled daemon-to-device path and physical presentation,
  not all provider event filters or a phone's current Tasker destination.

These observations do not verify actual provider-event delivery, a server reboot,
or a completed backup/restore. Earlier recurrence,
calendar delivery-dedup, and Slack filtering findings remain out of scope.

### Slack DM follow-up, 2026-11-06

A user-reported bot reminder used the alias mention form `<@ID|label>` in a DM.
The running credential authenticates as the intended human user, not a bot.
Its granted scopes omit im:history; reading the reported DM with that credential
returned missing_scope, needed im:history. Slack's connected chat integration
could read the message, but that is a different authorization from the daemon.
The daemon and clock were healthy, with no Slack dispatches in the preceding
30-minute log window. The daemon does not record incoming event bodies, so
these observations do not establish receipt of that specific Socket Mode event.

Two offline code failures were reproduced: alias-form mentions did not match,
and bot_message subtype events were always discarded. A narrow local fix now
matches the exact target ID in canonical and alias forms, permits bot_message,
and retains edit/delete/own-message filtering. Optional own-bot identity is also
retained from auth_test. Wrong IDs, ID prefixes, malformed mentions, cooldown,
and spike behavior have negative/regression controls.

- Red: 7 failed / 13 passed before the fix.
- Green: 31 Slack and health tests passed on Windows and in the Linux image.
- Source SHA256: 9b5b5eceb98e5110a75d787a81d85eb6035f467caa9a48834e7f688b1c7793d7.
- Staged image: localhost/holoclock-notify:20261106-slack-v1,
  ID 1cc0b01c3a7cf4db74f6534510996fbfa857778ea598dbe51a2c57939b08f09e.

Deployed with user authorization at 2026-11-06 22:49:31 UTC. The authoritative
homelab Quadlet and live Quadlet now select the staged image above. Only
holoclock.service was restarted; systemd reports active/running and the running
container image ID matches. HTTP /health reports all five workers ready and the
clock online. Independent local validation repeated all 31 tests successfully.
The previous Quadlet is retained on the server as
~/.config/containers/systemd/holoclock.container.pre-slack-20261106 for rollback.

Slack DM delivery remains blocked pending admin approval of user OAuth scope
im:history. Ensure message.im is subscribed under user events, then reauthorize.
Browser access was unavailable, so app settings were not inspected or changed;
subscription presence is unverified. Stage credentials privately if Slack rotates
the user token; do not paste tokens into chat. Provider readiness and synthetic
handler tests do not prove live Slack delivery or physical rendering. A new
external qualifying event and physical observation remain required.
