# CENTEN firmware

## V1.3 outdoor recovery update (2026-11-26)

This is the current build and recovery specification. The V1.1 notes below
are historical. The Keil project now uses its own `Common/` and Arm Compiler
6.24; the build script follows that compiler selection.

- `SystemInit()` starts IWDG before the C runtime, HAL, HSE/PLL, peripherals
  or logs. This function uses only registers and a bounded synchronization
  wait. It must remain connected when replacing generated startup code.
  The reload is nominally 10 seconds; actual timeout follows the LSI clock.
- BKP DR1 uses a 16-bit cookie. DR2/DR3 hold the streak and its complement;
  DR4/DR5 hold a requested recovery reset reason and its complement. Software
  recovery resets count as faults; an ordinary `relayreboot` does not add one.
- From the third consecutive fault boot, startup waits 30/60/120/240 seconds,
  then caps at 300 seconds. This happens on HSI after HAL_Init, before HSE/PLL.
  IWDG is fed during this bounded wait. HSE/PLL configuration errors request a
  marked reset. Startup logging occurs after UART initialization.
- Uptime alone no longer clears the streak. Sixty continuous seconds of
  healthy local UART/module replies and expected RF service are required.
  The RF register probe includes RX mode and the saved frequency value;
  it is not an RF/antenna reception test. No platform PING is required to
  qualify local health. An intentional RF-off state or closed work window
  suspends RF recovery. Normal quiet RF does not by itself escalate to MCU reset.
- RF recovery always pulses the hardware reset. It rebuilds SPI after a
  bus error or repeated failures. Failure history survives a successful init
  until stable local operation. Twenty accumulated unstable recovery failure
  events cause a marked MCU reset. Three consecutive TX timeouts schedule RF
  recovery. Recovery status `8|name|RFREC|failures|` can be uploaded with RF off,
  at most once per minute while recovering in a work window.
- Three local UART errors request abort/deinit/peripheral-reset/init/re-arm.
  Three rebuild attempts without stable recovery lead to a marked MCU reset
  on the next required rebuild. Overflowed/bad frames alone are not reboot
  triggers. The existing RAM queue retains failed UART transmissions.
- Automatic DTU resets for missing network time/platform heartbeats back off
  by 3/6/12/24/30 minutes, capped at 30 minutes. A valid PING resets this network
  backoff. A locally silent DTU has an independent three-minute response
  window (after a valid time, query interval plus one minute). Three modem
  resets without a valid local reply lead to a marked MCU reset on the next
  no-response recovery request. Valid CSQ, time, SIM, PING or platform command
  replies prove local liveness; platform failure alone does not reboot MCU.
- `[BOOT]` and `[REC]` diagnostics identify reset cause/streak. Recovery reason
  numbers are 1=RF, 2=UART, 3=unresponsive DTU, 4=clock, 5=IWDG. Software recovery
  reports use existing type 8: `8|name|RECBOOT|reason|`; IWDG keeps `rst_iwdg`.
  These uploads use the existing queue/UART protocol, without a new server ACK.

### Atomic settings and upgrade behavior

V2 settings occupy 104 bytes at the start of each existing reserved 1 KB page
(`0x0800F800`, `0x0800FC00`). Version, sequence, configuration, center coordinate,
device name and power share one CRC and one final commit word. Every save
erases only the inactive page, writes/reads back the payload and commits last.
Boot selects one complete snapshot. Unchanged V2 settings cause no erase.

Original single-page settings and V1.1/V1.2 records remain readable. Complete
V1 pages are preferred over a V1.2 page interrupted between its three commits;
legacy identity fallback checks the other page correctly. Migration occurs on
the first requested save, leaving the selected legacy page intact until the
new snapshot commits. No migration write occurs merely because the board boots.
Preserve the reserved pages during programming. Old firmware cannot read V2;
after both pages migrate, downgrading requires exporting/migrating settings.

### Current build and verification

```powershell
.\tools\build.ps1
.\tools\build.ps1 -TestImage
python -m pip install -r .\tests\requirements.txt
python .\tools\test_support.py
python .\tools\test_dtu_upload.py
python .\tools\test_recovery.py
```

Use `-CompilerBin` for a nonstandard compiler installation, and set
`CENTEN_COMPILER_BIN` for the support-test compiler. Production HEX/AXF are in
`build/`; the distinct test image in `build/test-image/` disables C inlining
to expose private entry points for fault injection. It is not the release HEX.

Recovery tests execute actual ARM settings save/load, early watchdog, backup
state, UART rebuild and recovery control flow with hardware substitutes. They
cut saves at every halfword/commit boundary in both directions, including V1
migration. They also cover failed writes, sequence/tick wrap, corrupt backup
state, RF-off reporting, intentional shutdown/work windows, stable-health
timers, and network versus local-fault recovery.

These tests do not program a device or verify real brownout, watchdog timing,
SPI/RF signals, DTU registration or environmental lifetime. The board still
needs physical fault injection and outdoor validation. No controllable module
power supply has been added; power-cycle-only failures need hardware support.

## V1.1.1 DTU upload diagnostics and scheduling fix

- Restores USART1 queue/send/receive/query diagnostics: `[DTU Q]`,
  `[DTU Q TX]`, `[DTU TX FAIL]`, `[DTU RX]`, `[DTU QUERY]` and `[DTU TIME]`.
- A deferred queue check no longer resets the two-second send throttle.
  The first queued upload can be sent at the existing five-second boot guard
  boundary; a query-blocked upload can proceed as soon as the query and guard
  allow it, without an additional throttle delay.
- An accepted network-time reply immediately clears the query-pending flag.
- V1.1.1 does not change the wire JSON schema or type-9 forwarding policy.
  Type 9 is a radio command, not telemetry. Before network time is available,
  `tm` remains zero; DTU UART completion is not a server acknowledgement.
- `python tools/test_dtu_upload.py` runs the real firmware image's OnSlave,
  sendToDtu, processDtuQueue and nettime response functions in an ARM CPU,
  replacing only UART/GPIO/tick hardware calls. It verifies the user's exact
  type-2 frame, type-9 handling, boot/query guards, HAL_BUSY retention/retry,
  JSON bytes handed to USART3 and restored diagnostic output.


## 2026-11-24 reliability update (V1.1)

This STM32F103C8T6 gateway forwards PAN3029 uplinks through USART3 to a DTU.
The shared `../Common` directory is still required. This update does not edit
the shared driver or change its frequency/SF/BW/CR defaults.

### Changes

- UART always receives into a dedicated byte, then pushes to a 1024-byte SPSC
  ring (1023 usable bytes). Full rings count discarded bytes and continue
  receiving. UART errors request abort/clear/re-arm from the main loop.
- CR/LF-delimited DTU replies and balanced JSON objects are processed one by
  one. Adjacent JSON objects are supported. Legacy unterminated text uses a
  500 ms idle fallback; incomplete JSON is rejected. Frames are limited to
  511 bytes, decoded command fields to 31 characters. There is no shared
  receive/parse buffer.
- A bounded JSON reader supports whitespace, quoted/numeric values, escaped
  strings and numeric/quoted cache expiry (0..120 minutes). It rejects missing
  IDs/commands, duplicates, invalid JSON, truncated fields and frame delimiters.
  Protocol identifiers and values use printable ASCII; IDs/commands use
  letters, digits, hyphen or underscore. Unicode escapes outside ASCII and NUL
  are rejected. Upload strings are JSON escaped.
- LoRa uplinks up to 96 text bytes have an independently reserved NUL terminator.
  An optional on-air trailing NUL is accepted, preserving ONLY_SEND's
  `strlen(msg)+1` format; longer or internally embedded-NUL frames are rejected.
  Outgoing command buffers allow
  the full 97-byte maximum (`9|` plus three 31-byte fields), and all lengths
  are checked before transmission. The terminal must independently support
  the received command length; this update does not change terminal firmware.
- EXTI2 only sets an event flag. All PAN3029/SPI operations run in the main
  loop. SPI errors are latched to avoid repeated blocking failures, and RF
  recovery is retried without trapping the DTU in a boot error loop.
- Downlink jobs preserve the existing 800 ms command / 1200 ms sync delays
  using deadlines instead of HAL_Delay. TXDONE and the 3-second timeout are
  polled from the main loop. At most 8 reply jobs are pending. Expired wake
  windows are discarded. Command cache entries clear on TXDONE, not timeout;
  commands replaced while a TX is in progress are preserved by generation ID.
- No zero timestamp sync frames are sent. Dates and coordinate ranges are
  validated. Time anchors are advanced incrementally and deadlines use signed
  differences so tick wrap is handled. A command cache expiry at tick zero
  is distinct from a permanent entry.
- DTU query spacing is scheduled without 300 ms sleeps. All reset reasons
  share a 180-second registration grace. Failed network time queries can
  trigger a reset only after that grace; the first PING uses its configured
  timeout, default 1200 seconds. UART/heartbeat/query state is reinitialized
  on every DTU reset.
- PA6/MISO is configured as input. TX power is restored after RF wake and
  changes during transmission are deferred until the transmitter is idle.
- Stack allocation increases from 1 KB to 2 KB. Keil uses optimization level
  2 for space, matching `tools/build.ps1`. Unused USART2 MSP branches and
  repetitive/test-only debug output have been removed.
- `readinfo` retains the existing three platform messages and additionally
  prints `[HEALTH]` counters to USART1 for UART drops, rejected frames, TX
  errors, queue drops, resets and RF timeouts/drops.

### Flash compatibility

The final **two 1 KB pages**, `0x0800F800` and `0x0800FC00`, are reserved for
configuration. The linker application limit is **0xF800 (62 KB)**. Each new
record contains a version, sequence, both strings, CRC32 and a commit word.
The inactive page is erased and written, read back, and committed last. The
old record is retained on failure. Unchanged values are not erased again.

At startup a valid new record is preferred. If none exists, the old magic/
string layout in `0x0800FC00` is read and validated. The first new save goes
to `0x0800F800`, leaving that old page intact until the new record commits.
Power loss while writing cannot turn an incomplete new record into a valid
record. Actual brownout/flash failure behavior still needs a board test.

The programming tool must preserve the configuration pages when upgrading.
A full-chip erase removes both formats. Downgrading to older firmware after
both pages have adopted the new format requires a configuration migration.

### Build

On a machine with Keil ARMCC 5.06u7 installed:

```powershell
.\tools\build.ps1 -CompilerBin 'D:\Keil_v5\ARM\ARM_Compiler_5.06u7\Bin'
```

The script compiles every C/assembly file in the Keil project, links with the
same application/RAM limits and creates `build/CENTEN.axf`, `CENTEN.hex`, a map,
call graph and log. It does not launch a GUI, attach a probe, or program a board.
The original Keil project can also be opened normally. The installed MDK-Lite
linker has a 32 KB image limit; keep checking final ROM size when adding code.

The `.ioc` now describes SPI1, the current GPIOs and UART1/UART3. CubeMX
regeneration is not part of the automated checks; review its generated diff
because main-loop business code and `MX_NVIC_Init` are customized.

### ARM CPU regression tests

```powershell
python -m pip install -r .\tests\requirements.txt
python .\tools\test_support.py
```

Run the firmware build first. Set `CENTEN_ARMCC_BIN` if the compiler directory
differs. The test runner links the production support/UART objects, emulates
their ARM instructions with a Cortex-M3 CPU, and replaces only HAL UART start
and HAL_GetTick with test doubles. It tests wrap/full/recovery of the RX ring,
JSON framing and invalid input, 64/100-byte command buffers, escaping, dates,
tick deadlines, CRC corruption and interrupted record writes. It invokes the
production RX callback 1400 times, including ring-full conditions.

These tests do not validate actual SPI signals, RF timing, modem registration,
terminal acknowledgments or physical flash power loss. The DTU platform
protocol still has no new application-level acknowledgement or offline disk
queue. The 5-entry upload queue still discards the oldest message when full;
the 2-second send throttle is unchanged. Device UID/name mappings are unchanged.
The time zone contract with the DTU/platform also remains unchanged.
