# Controller identity and rolling-code persistence

Security+ 2.0 transmission now requires a provisioned LittleFS controller record.
The library never formats or ends LittleFS, invents a new identity on boot, or
resets a counter automatically. Security+ 1.0 and dry-contact control are unchanged.
Keep the existing board flash/filesystem partition layout across firmware updates.

## Application integration

```cpp
if (!setupRATGDO()) {
  Serial.println(ratgdoStorageError()); // Receive-only; operator action required.
}
// Continue loopRATGDO() for passive state observations.
// Check ratgdoStorageReady() before exposing SP2 command availability.
```

`bool setupRATGDO()` retains the normal pin/serial setup and checks storage
synchronously. `bool ratgdoStorageReady()` and `const char* ratgdoStorageError()`
also expose later reservation failures. Errors are printed to Serial from normal
setup/command context, not from interrupts; no network logger is introduced.
The error string remains valid until another storage operation. Readiness means
the store is initialized, not that a future flash write cannot fail.

The first controller loop retains the six existing non-actuating startup/status
messages. Each message reserves its code before transmission. Status queries,
motion-triggered queries, light/lock commands, and dry-contact-triggered SP2 door
commands all use the same allocator. Failed encoding/reservation cannot transmit
a previous buffer. A prepared frame can be transmitted only once.

The old `readCounterFromFlash` / `writeCounterToFlash` API is removed: independently
writing an ID or lowering a counter bypasses the safety invariant.
`getRollingCode()` now returns success/failure. Public `idCode` and
`rollingCodeCounter` remain diagnostic mirrors, not mutable persistence controls.

For Security+ 2.0, `lastRX` is refreshed by `gdoStateLoop()` only after a complete
19-byte frame successfully decodes. `readRollingCode()` now returns `bool`:
invalid frames return false, log a Serial decode error, and leave all decoded
state unchanged. Valid repeated statuses refresh receive freshness even if no
field changes. This is a global bus-receive timestamp, not a per-field freshness
guarantee or proof of opener identity: valid queries or other decoded commands
also refresh it without updating every state field. Security+ 1.0 retains its
existing header-time `lastRX` behavior for transmit spacing.

### Security+ 2.0 light observations

Received LIGHT (`0x281`) uses the action nibble (data bits 8-11): `0` means
OFF, `1` ON, and `2` TOGGLE, matching upstream
[LightAction definitions](https://github.com/ratgdo/esphome-ratgdo/blob/fb50a818e480bec7b474e18788f224890d46f517/components/ratgdo/ratgdo_state.h)
and [LIGHT decoding](https://github.com/ratgdo/esphome-ratgdo/blob/fb50a818e480bec7b474e18788f224890d46f517/components/ratgdo/secplus2.cpp).
Explicit OFF/ON replaces the cached light state, including unknown (`2`), and
repeated explicit commands do not invert it. TOGGLE only inverts a known OFF/ON
state; without a baseline it leaves unknown unchanged. Unsupported actions
(`3`-`15`) leave state unchanged. Serial diagnostics include the action and
resulting state, with a reason when an action cannot update the cache.

STATUS (`0x81`) remains authoritative: byte2 bit 1 replaces light state.
LIGHT does not change door, lock, motion, or obstruction state. A successfully
decoded LIGHT frame still refreshes global `lastRX`, even for an unsupported
action or a toggle without known state; malformed frames do not. No periodic
polling, network logging, public API changes, or transmit behavior changes are
introduced by this decoding fix.

This corrects the proven unconditional-inversion bug, not a confirmed hardware
diagnosis. The reported beam-triggered light/timeout mismatch had no packet
capture identifying which messages the opener emitted. Missed frames or a light
change without an observed LIGHT/STATUS message can still leave the cache stale;
global receive freshness alone does not establish light-state freshness.

### Temporary buffered protocol diagnostics

`ratgdo.h` exposes an opt-in, fixed 16-record receive FIFO. Capture is disabled
at boot. Call `setRatgdoRxDiagnosticsEnabled(true)` to start a capture;
**every** enable/disable call clears pending records and resets the dropped
counter, even if already in that mode. Drain before disabling if final records
are needed. `readRatgdoRxDiagnostic(record)` removes the oldest record and
returns true, or returns false without changing the argument when empty.
`ratgdoRxDiagnosticsDropped()` reports dropped-newest records since the last
enable/disable call, saturating at `UINT32_MAX`. Reading does not reset it.
An overflow never changes receive processing or state.

`RatgdoRxDiagnostic` contains only these fields:

| Field | Meaning |
| --- | --- |
| `uptimeMs` | 32-bit `millis()` at complete-frame decoder entry, before Serial logging; wraps naturally, not wall time or first-byte arrival |
| `command` | Full decoded command ID; `0xffff` on decode failure |
| `value` | LIGHT action nibble or STATUS light bit; `0xff` otherwise |
| `light` | Resulting cached light state (`0` OFF, `1` ON, `2` unknown) |
| `kind` | `RatgdoRxKind::Command`, `Status`, `Light`, or `DecodeFailure` |

Every successfully decoded SP2 command is captured, including unrecognized
commands, repeated/unchanged STATUS and LIGHT, unsupported LIGHT actions, and
toggle without a baseline. Failed decodes produce one failure record without
using partially decoded values. Noise and partial packets that never reach the
decoder are not recorded; SP1 is not recorded. The FIFO stores no raw packet,
rolling counter, controller identity, or secrets. Existing Serial diagnostics
are unchanged; the new feed does not forward their raw packet text.

Capture, drain, and query APIs belong on the Arduino loop thread, not ISRs,
concurrent tasks, or network callbacks. The FIFO performs no allocation, file
write, network call, or callback. Consumers may drain a bounded number of
records into their existing logging buffer after `loopRATGDO()`, correlating
using `uptimeMs` rather than delayed log-delivery time. Consumers own capture
duration, access control, and network logging; enabling capture never sends
a status query.

### Manual one-shot status query

`bool requestRATGDOStatus()` synchronously attempts **one** SP2 GET_STATUS
(`0x80`), reusing `getRollingCode("reboot2")`, durable code reservation, and
the same single-use guarded transmission path. It rejects an uninitialized
SP2 UART, a different current protocol, or unavailable storage. Reservation,
encoding, and incomplete serial writes return false and log the failure.
True means the serial driver accepted the complete frame, **not** an opener
acknowledgment, successful status response, or confirmed light state. Even
partially transmitted codes remain consumed; there is no automatic retry.

This call does not update cached state or `lastRX`, schedule work, or add
periodic polling. Only a subsequently decoded receive frame can refresh
`lastRX`. A later STATUS response may reconcile the cache, but cannot be
distinguished as a response to this query by this API. Existing startup sync
and motion-triggered queries remain unchanged. Defer any HTTP request to a
single bounded main-loop action; do not call this API from the HTTP handler.

## Deliberate first provisioning and migration

1. **Do not guess an old identity's counter.** The previous RAM-only map did not
   retain a trustworthy high-water mark. A currently observed counter, a stale
   backup, or the last received opener packet cannot prove the highest value
   that this controller ever transmitted. Retain an old ID only if a reliable
   external record supplies a next value strictly above every previously used
   value and below `0x10000000`.
2. Prefer a **previously unused controller ID**, explicitly selected and recorded
   by the operator, ending in hexadecimal `539`. The old generator could only
   produce IDs below `0x00800000`; selecting an unused ID above that range avoids
   collision with that generator. Check IDs belonging to other controllers, too.
   For example, `0x10001539` is in the new range, but is **not a universal default**:
   do not copy it without checking that it has never been used with your opener.
   Counter zero is safe for a genuinely unused ID only. A new identity can require
   supervised enrollment using the opener manufacturer's instructions; the
   library does not enter learn mode or actuate the door to enroll it.
3. LittleFS must already be initialized and mountable. If mounting fails, stop
   and inspect/back up the shared filesystem and partition configuration. A truly
   new, unformatted device needs a separate, deliberately authorized filesystem
   initialization procedure. This library and its provisioning example do not
   provide automatic formatting, deletion, or recovery.
4. With no `/ratgdo.state` or `/ratgdo.tmp` present, edit `examples/Provision`:
   set the approved ID, its known-safe next counter, and `ProvisionApproved = true`.
   Run that maintenance firmware only as an explicit operator action. It calls
   `provisionRATGDO(id, next)` once, checks and reports the result, and does **not**
   configure garage pins, call `setupRATGDO()` / `loopRATGDO()`, sync, or send any
   door commands. Retain the recorded identity independently.
5. Restore the normal firmware without erasing/changing the filesystem. It loads
   the provisioned identity and reserves codes for the normal startup queries.
   Do not put provisioning in a setup-failure handler or an automatic retry loop.

`provisionRATGDO()` refuses existing, unreadable, or interrupted controller state.
Corrupt or exhausted records require manual investigation, not an automatic
overwrite. Preserve evidence and backups; if the previous high-water mark cannot
be proven, use a verified unused identity through a separately authorized
maintenance/recovery procedure. Never restore an older valid controller record:
checksums detect damage, not rollback of an otherwise valid historical record.

## Durability and bounded storage

Only `/ratgdo.state` and `/ratgdo.tmp` are used; parent `/hkc*` logs are untouched.
Each record is exactly 24 bytes: little-endian magic, version, controller ID,
exclusive reserved end, generation, and CRC-32. Allocation uses fixed blocks of
64 codes, at most one write per block (plus provisioning). Each boot skips all
previously reserved codes; frequent reboots therefore advance the counter and
can require opener resynchronization. No retry automatically actuates the door.

Before issuing the first code in a block, the next exclusive end is written to
the temporary record. The adapter checks write length, calls flush, checks the
stream error, closes and reopens to verify exact length/content, atomically
renames over the committed record, and verifies the committed content again.
ESP8266's Arduino `File::flush()`/`close()` return `void` and suppress the raw
LittleFS sync result, so verification uses the closed/reopened file plus the
checked LittleFS rename, rather than treating `flush()` alone as confirmation.
This relies on LittleFS's documented atomic rename and power-loss-safe metadata
commit behavior and the flash driver honoring successful operations.

A reset before rename leaves the old committed end, whose next block has not
been transmitted; a reset after rename skips the new block. An orphan temporary
file alongside a valid committed record is ignored and overwritten only by a
future reservation. A temporary file without a committed record is an interrupted
provision, not a blank controller. A corrupt committed record is **never**
replaced by an older record or a temporary file.

The door protocol's press/release messages intentionally share one reserved
counter; this existing protocol behavior is retained only for an immediately
paired, transmitted press in the current boot. A reset cannot replay either
half. All other messages consume separate codes, including status queries.
The final partial block ends at `0x10000000`; the last legal counter is
`0x0fffffff`. Exhaustion disables subsequent transmission instead of wrapping.

Do not unmount, format, delete/restore controller files, or concurrently provision
the store while the controller is running. Calls belong on the Arduino loop
thread, never in ISRs or concurrent tasks. Flash wear leveling and physical
capacity are managed by LittleFS; filesystem metadata consumes more than the
48-byte bounded record payload. Uncorrectable flash corruption fails closed,
but arbitrary hardware faults or external rollback cannot be repaired safely.

## Host checks

Run `sh tests/run.sh` with a C++11 compiler supporting address/undefined-behavior
sanitizers. Pure allocator tests cover reboot skips, bounded writes, failed/torn
commits, corruption, explicit provision, and exhaustion. Integration tests compile
the actual controller, allocator, and LittleFS adapter against host fakes,
checking mount/write/flush/rename/readback failures, shared-file preservation,
all command entry points, stale frames, paired releases, and non-actuating boot.
Transmission fault tests use a recording fake codec. The receive regression
links the actual installed `secplus.c` and feeds encoded frames through
`gdoStateLoop()`, covering complete/partial/invalid frames, unchanged repeated
status, global freshness, and unchanged Security+ 1.0 receive timing. LIGHT tests
execute the actual decoder directly and through receive assembly for all 16
action nibbles from OFF/ON/unknown, repeated commands, unrelated-state
preservation, rejected/partial LIGHT frames, and authoritative STATUS
reconciliation followed by a toggle.
Additional tests cover disabled/reset/drain/overflow/wraparound diagnostic
behavior, unchanged STATUS, all LIGHT actions, unknown commands, decoder
failures, and receipt timestamps. One-shot query tests verify GET_STATUS
payload, initialization/protocol/storage guards, reservation/encoding/write
failures, replay prevention, unchanged cache/freshness, and no periodic retry.
The runner defaults to `../secplus/src`; set `SECPLUS_SRC` to the installed codec
source directory if it lives elsewhere. Missing codec sources fail the run
rather than skipping receive coverage. An Arduino sketch build checks the
installed codec and filesystem APIs. These checks are not a physical power-cut
or opener-compatibility test.
