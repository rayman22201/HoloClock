# Clock layout without music

Current state: the final layout groups month/day on the left (36px), weekday
on the right (28px), and uses 56px time digits. It has been flashed to COM9
with data-hash verification. The user confirmed both the larger fonts and the final date grouping look good.
The sections below record the implementation and follow-up evidence.

Request: remove the currently playing music and make time/date slightly larger.

Time uses 56px Montserrat Medium digits (previously 48px); month uses 36px
(previously 32px), and day/weekday uses 28px (previously 24px). The date row
moves down 10px, time down 8px, and AM/PM down 22px into the freed space.
The ticker module and its lifecycle/poll calls are removed. `/np-debug` remains
available for WiFi/heap diagnostics, with `np: {"enabled": false}`.

## Font provenance

`src/resource/font/clock_digits_56.c` was generated from Google Fonts Montserrat:
https://raw.githubusercontent.com/google/fonts/main/ofl/montserrat/Montserrat%5Bwght%5D.ttf

Source SHA-256: 0F7B311B2F3279E4EEF9B2F968BCDBAB6E28F4DAEB1F049F4F278A902BCD82F7.
License is preserved in `src/resource/font/Montserrat-OFL.txt` (paths under Holo-fw).
FontTools 4.59.2 instantiated weight 500, then lv_font_conv 1.5.3 generated
56px, 4bpp, uncompressed digits 0x30?0x39, named `clock_digits_56`.
The generated include was adjusted to `lvgl.h` for this project's include paths.
The digits-only bitmap is 5,873 bytes and needs no runtime scaling buffer.

## Review and verification

Base: main, abe28082287f9c03060933efb5eb76cd88b39be0; starting tree clean.
Independent source/metric review found a 109px wide date against the initial
108px label. Corrected to 112px at X62; the right edge stays inside 240px.
Time pairs fit 78px labels; months fit 92px; heights include parallax clearance.

Initial build was blocked by PlatformIO cache permissions. Authorized build
access reached compilation and found the generated font include mismatch.
Corrected the include and rebuilt both environments. Final results recorded below.

No flashing, device reset, daemon startup or live-setting changes were performed.
Source metrics do not establish physical optical readability or runtime timing.

Final build: `pio run --project-dir Holo-fw -e Holo_Releases -e Holo_Debug`
Both environments reported success (debug 124.269s; release 154.396s), but the
PowerShell wrapper returned 1. A confirmation run explicitly captured
`PLATFORMIO_EXIT=0` and exited 0: debug 46.865s; release 62.861s, 2/2 succeeded.
Both report RAM 82,320/327,680 bytes and flash 1,634,824/3,145,728 bytes.
`pio project config --project-dir Holo-fw` and `git diff --check` exited 0.
Warnings remain in vendored libraries/framework (MPU6050 manifest, SPI frequency,
FastLED SPI selection, ESP32 pointer types); no warning remediation was attempted.

## Review verdict

VERDICT: PASS
Mode: quick
Verified: task-scoped diff, music call removal, font data and metrics, cleanup,
label boundaries, both firmware environments, whitespace.
Evidence: final build command above exit=0, 2/2 environments; initial include
failure observed in both environments, corrected and rebuilt.
Checklist 1?9: PASS; no test assertions introduced/changed, no added sleeps or
runtime allocations. Existing notification and clock lifecycles retained.
New failures: none in final builds. No baseline build was run.
Environment-caused failure: initial PlatformIO lock permission error, resolved
by authorized cache access.
Suspect transitions: none. Waived: none.
Verification boundary: no automated runtime tests, physical display/gestures,
network execution, timing or optical validation. No device operations executed.
Side effects: seven existing source/config files changed (two deleted), generated
font and license added, this record added; ignored local build/font tooling under
`.tmp`. No commits or uploads.
Blocking findings: none. Date width finding fixed (108 to 112px); independent
metric review confirmed the largest affected date is 109px.

## Deployment

User authorized deployment in the follow-up turn.
Receipt recorded: 2026-11-05T21:24:59.663954-08:00
Target: COM9, USB-SERIAL CH340, VID:PID 1A86:7523, location 1-1.3.
Release firmware SHA-256: 0D00A060FDD53DC0014BD873BC97B46D0F48B4104D6A9702769116F68AF79039.
`pio run --project-dir Holo-fw -e Holo_Releases --target upload --upload-port COM9`
returned native exit 0, 1/1 environment succeeded (37.020s).
esptool wrote 1,634,928 application bytes at 0x00010000, verified the
flashed data hash, and issued a hard reset via RTS. Upload log: `.tmp/clock-upload.log`.
The update takes effect on boot following that reset. Boot completion,
running application identity and physical display readability were not observed.

## Follow-up: group month and day

User confirmed the first deployment succeeded and the larger font looked good.
Requested date grouping is now `Nov 5` on the left and `Thu` on the right.
The month/day label uses the existing 36px month font; weekday retains 28px.
The left label spans x2..145 (144px), right x166..237 (72px).
A single date update formats both labels from the same time snapshot.

VERDICT: PASS
Mode: quick
Scope: ui.cpp, ui.h, ui_img_1_png.h date formatting and horizontal bounds.
Verified: declarations/callers updated together; no old date helper callers;
all 372 month/day combinations and 7 weekdays measured from vendored glyph
advances and actual kerning classes. Maximum widths: 130/144px and 66/72px.
An initial deliberately conservative bound using maximum kerning for every
pair exceeded the width; actual pair-specific font metrics resolve that bound.
Checklist 1?9: PASS; existing valid-date input contract and object lifecycle
unchanged; no new allocations, sleeps, tests or test assertions introduced.
Evidence: `pio run --project-dir Holo-fw -e Holo_Releases -e Holo_Debug`
native exit 0; 2/2 succeeded (debug 34.734s, release 31.613s).
`git diff --check` exit 0; font metric enumeration exit 0.
New failures: none. Waived: none. Blocking findings: none.
Verification boundary: this follow-up has not been flashed or physically viewed.
Side effects: three source/header files edited; this record appended. Prior
music removal and font changes preserved. No device operations this turn.

## Final date-grouping deployment

User authorized flashing, committing and pushing. COM9 was re-enumerated as
USB-SERIAL CH340 (1A86:7523, location 1-1.3).
Release firmware SHA-256: 40A406B4678F5044DFE9DE493A94134C88ABD43EEC08EF587BEEFF3C6602D14E.
`pio run --project-dir Holo-fw -e Holo_Releases --target upload --upload-port COM9`
returned native exit 0, 1/1 success (37.203s). esptool wrote 1,634,912 application
bytes at 0x00010000, verified the flashed data hash, and issued hard reset via RTS.
Log: `.tmp/date-grouping-upload.log`. No runtime or physical-display claims
are inferred from the upload result. This supersedes the follow-up's pre-flash
status above. The initial deployment's user-confirmed appearance remains valid
for its tested layout only.

Final physical validation: after the date-grouping flash, the user reported
"visual verification confirmed. looks good." This confirms the observed layout;
long-running stability and unrelated hardware behaviors were not tested.
Pre-commit whitespace cleanup removed trailing spaces from the font license and
an extra final blank line in the generated font; no executable code changed.
