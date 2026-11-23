# Project-local PAN3029 driver

Snapshot of `../Common/RadioF103` taken on 2026-11-20 for this firmware only.
The uVision project now compiles `Radio/src` and includes `Radio/inc`.
The shared directory was not changed.

Local changes:

- ISR/foreground RX and TX flags are volatile.
- SPI polling has a tick-independent bound and detects failed transactions.
- CS transactions preserve interrupt state and cannot be interleaved by EXTI2.
- SPI failures use the project's recorded reset path in `reliability.c`.

Keep these changes when importing a future vendor driver update. Do not replace
this directory with a shared path without reviewing the watchdog behavior.

2026-11-23 reliability update:

- Page selection plus access and read/modify/write operations preserve the caller interrupt mask as one transaction.
- Deep-sleep preparation checks readable mode writes, retries preparation once, and reports failure to the caller. The retained second pass is deliberately unchecked because a sleeping chip cannot be read back.
- Application sleep requests are idempotent. Do not send a new verified sleep sequence to an already sleeping module.
- Final deep-sleep entry and board current still require measurement; SPI success does not prove low current.
