"""Fault injection into the real ARM firmware; no board access or programming.

Run tools/build.ps1 -TestImage first. Only tick, UART transmission, RF hardware
and flash operations are mocked. UART init/abort/receive and boot code execute.
"""
from pathlib import Path
import json
import struct
import sys
import zlib
from arm_test_cpu import ArmCPU

ROOT = Path(__file__).resolve().parents[1]
IMAGE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/test-image/CENTEN.axf'
A, B = 0x0800f800, 0x0800fc00
BKP, CSR = 0x40006c00, 0x40021024
checks = 0
scenarios = []

def check(value, message):
    global checks
    checks += 1
    assert value, message

def settings(seq, cfg='30,0M', name='v3-108', power=7, center='30.12345,109.39087'):
    body = struct.pack('<III32s32s16sI', 0x43454e54, 2, seq,
                       cfg.encode(), center.encode(), name.encode(), power)
    return body + struct.pack('<II', zlib.crc32(body), 0x434f4d54)

def legacy(seq, cfg='20,0M', name='v3-107', power=8, complete=True):
    body = struct.pack('<III32s32s', 0x43454e54, 1, seq, cfg.encode(), b'')
    page = bytearray(b'\xff' * 1024)
    page[:84] = body + struct.pack('<II', zlib.crc32(body), 0x434f4d54)
    nbody = struct.pack('<I16s', 0x4e414d45, name.encode())
    page[256:284] = nbody + struct.pack('<II', zlib.crc32(nbody), 0x4e414d31 if complete else 0xffffffff)
    page[320:336] = struct.pack('<IIII', 0x52574f50, power, (~power) & 0xffffffff,
                               0x52573031 if complete else 0xffffffff)
    return bytes(page)

def cpu():
    c = ArmCPU(IMAGE)
    c.on('HAL_UART_Transmit', lambda: c.ret())
    return c

def load(c):
    c.text('configStr', '30,0M')
    c.text('centerStr', '')
    c.text('deviceName', 'v0-100')
    c.write('localTxPower', 10, 1)
    c.call('loadConfigFromFlash')
    c.call('loadNameFromFlash')
    return c.string('configStr'), c.string('deviceName', 16), c.read('localTxPower', 1)

class Flash:
    def __init__(self, c, cut=None, error=None):
        self.c, self.cut, self.error = c, cut, error
        self.steps = self.erases = 0
        self.interrupted = False
        c.on('HAL_FLASH_Unlock', lambda: c.ret())
        c.on('HAL_FLASH_Lock', lambda: c.ret())
        c.on('HAL_FLASHEx_Erase', self.erase)
        c.on('HAL_FLASH_Program', self.program)

    def erase(self):
        page = self.c.read(self.c.arg(0) + 8)
        self.erases += 1
        self.c.cpu.mem_write(page, b'\xff' * 1024)
        if self.cut == 0:
            self.interrupted = True
            self.c.cpu.emu_stop()
        else:
            self.c.ret()

    def program(self):
        address, value = self.c.arg(1), self.c.arg(2)
        width = 2 if self.c.arg(0) == 1 else 4
        for off in range(0, width, 2):
            self.steps += 1
            if self.error == self.steps:
                self.c.ret(1)
                return
            old = self.c.read(address + off, 2)
            new = (value >> (8 * off)) & 0xffff
            assert old & new == new, 'Flash attempted a zero-to-one transition'
            self.c.write(address + off, new, 2)
            if self.cut == self.steps:
                self.interrupted = True
                self.c.cpu.emu_stop()
                return
        self.c.ret()

# Cut after erase and EVERY halfword, including each half of the final commit.
for active, inactive in ((A, B), (B, A)):
    for cut in range(53):
        c = cpu()
        c.cpu.mem_write(A, b'\xff' * 2048)
        c.cpu.mem_write(active, settings(10, '20,0M', 'v3-107', 8))
        c.write('configActiveAddr', active)
        c.write('configSequence', 10)
        c.text('configStr', '30,0M')
        c.text('centerStr', '30.12345,109.39087')
        c.text('deviceName', 'v3-108')
        c.write('localTxPower', 7, 1)
        flash = Flash(c, cut=cut)
        c.call('flash_save_all', stopped=True)
        check(flash.interrupted, f'cut {cut} was not reached')
        values = load(c)
        expected = ('30,0M', 'v3-108', 7) if cut == 52 else ('20,0M', 'v3-107', 8)
        check(values == expected, f'mixed snapshot at page {active:x}, cut {cut}: {values}')
        check(c.call('centen_settings_valid', active), 'old snapshot erased')
scenarios.append('106 halfword/erase power-loss cutpoints, both page directions')

# The first V2 save must protect the previous V1 identity through every cut.
for active, inactive in ((A, B), (B, A)):
    for cut in range(53):
        c = cpu()
        c.cpu.mem_write(A, b'\xff' * 2048)
        c.cpu.mem_write(active, legacy(10))
        check(load(c) == ('20,0M', 'v3-107', 8), 'legacy precondition failed')
        c.text('configStr', '30,0M'); c.text('deviceName', 'v3-108')
        c.write('localTxPower', 7, 1)
        f = Flash(c, cut=cut)
        c.call('flash_save_all', stopped=True)
        expected = ('30,0M', 'v3-108', 7) if cut == 52 else ('20,0M', 'v3-107', 8)
        check(load(c) == expected, f'migration mixed identity at {active:x}/{cut}')
        check(c.call('centen_record_valid', active), 'V1 source erased during migration')
scenarios.append('106 additional cutpoints during V1-to-V2 migration')

# Program errors must preserve the previous active snapshot and report failure.
for error in (1, 25, 50, 51, 52):
    c = cpu()
    c.cpu.mem_write(A, b'\xff' * 2048)
    c.cpu.mem_write(A, settings(1, '20,0M', 'v3-107', 8))
    c.write('configActiveAddr', A)
    c.text('configStr', '30,0M'); c.text('deviceName', 'v3-108')
    f = Flash(c, error=error)
    check(c.call('flash_save_all') == 0, 'program error was reported as success')
    check(load(c) == ('20,0M', 'v3-107', 8), 'program error lost previous snapshot')

# Legacy migration and the V1.2 B/B fallback regression, in both directions.
for full, partial in ((A, B), (B, A)):
    c = cpu()
    c.cpu.mem_write(full, legacy(10))
    c.cpu.mem_write(partial, legacy(11, name='v3-999', complete=False))
    check(load(c) == ('20,0M', 'v3-107', 8), 'incomplete legacy page selected')
    f = Flash(c)
    check(c.call('flash_save_all') == 1, 'V1 migration failed')
    check(c.call('centen_settings_valid', partial), 'migration did not target inactive page')
    check(c.call('centen_record_valid', full), 'migration erased source V1 page')
    check(load(c) == ('20,0M', 'v3-107', 8), 'migration changed identity')
    f.steps = f.erases = 0
    check(c.call('flash_save_all') == 1 and not f.erases, 'unchanged save caused wear')

c = cpu()
c.cpu.mem_write(A, b'\xff' * 2048)
c.write(B, 0xa5a5a5a5); c.text(B + 4, '20,0M')
check(load(c) == ('20,0M', 'v0-100', 10), 'original one-page format not loaded')
f = Flash(c)
check(c.call('flash_save_all') == 1, 'original format migration failed')
check(c.read(B) == 0xa5a5a5a5, 'original page erased before migration completed')
c.cpu.mem_write(A, settings(0xffffffff, '20,0M', 'v3-107', 8))
c.cpu.mem_write(B, settings(0))
check(load(c) == ('30,0M', 'v3-108', 7), 'sequence wrap selected wrong page')
scratch = 0x20004000
record = settings(2)
for index in range(len(record)):
    damaged = bytearray(record); damaged[index] ^= 1
    c.cpu.mem_write(scratch, bytes(damaged))
    check(not c.call('centen_settings_valid', scratch), 'corrupt field accepted')
scenarios.append('write failures, original/V1 migration, legacy B/B regression, CRC and sequence wrap')

# Early watchdog runs from SystemInit without any HAL_GetTick/HAL_Delay calls.
c = cpu()
c.call('SystemInit')
check(0xcccc in c.watchdog_writes and c.watchdog_writes[-1] == 0xaaaa, 'watchdog did not start early')
check('HAL_GetTick' not in c.calls and 'HAL_Delay' not in c.calls, 'early watchdog depends on tick')
c.write(CSR, 1 << 27)  # PORRSTF
c.call('wdg_capture_boot')
check(c.read('wdgStreak') == 0 and c.read(BKP + 4) == 0xce13, 'cold boot state invalid')
for expected in range(1, 6):
    c.write(CSR, 1 << 29)
    c.call('wdg_capture_boot')
    check(c.read('wdgStreak') == expected, 'IWDG streak did not persist')
    check(c.read(BKP + 12, 2) == ((~expected) & 0xffff), 'streak inverse invalid')
check(c.read('bootBackoffMs') == 120000, 'backoff did not increase')
c.now = 0xfffff000
c.call('wdg_boot_backoff')
check(c.now == (0xfffff000 + 120000) & 0xffffffff, 'backoff tick wrap failed')
for reason in (1, 2, 3, 4):
    c.reset = False
    c.call('recoveryReboot', reason, stopped=True)
    check(c.reset and c.read(BKP + 16) == reason, 'software recovery reason not persisted')
    before = c.read('wdgStreak')
    c.write(CSR, 1 << 28)
    c.call('wdg_capture_boot')
    check(c.read('wdgStreak') == before + 1 and c.read('recoveryBootReason', 2) == reason,
          'software recovery did not increment streak')
before = c.read('wdgStreak')
c.write(CSR, 1 << 28)
c.call('wdg_capture_boot')
check(c.read('wdgStreak') == before, 'user software reboot counted as fault')
c.write(BKP + 12, 0)  # corrupt complement
c.write(CSR, 1 << 29)
c.call('wdg_capture_boot')
check(c.read('wdgStreak') == 1, 'corrupt BKP state not rejected')
check(c.call('centen_boot_backoff_ms', 15) == 300000, 'backoff cap failed')
for _ in range(20):
    c.write(CSR, 1 << 29); c.call('wdg_capture_boot')
check(c.read('wdgStreak') == 15, 'streak did not saturate')
c.write(CSR, 1 << 27); c.call('wdg_capture_boot')
check(c.read('wdgStreak') == 0, 'power-on did not clear streak')
for failure in ('HAL_RCC_OscConfig', 'HAL_RCC_ClockConfig'):
    c = cpu()
    c.on('HAL_RCC_OscConfig', lambda: c.ret(1 if failure == 'HAL_RCC_OscConfig' else 0))
    c.on('HAL_RCC_ClockConfig', lambda: c.ret(1))
    c.call('SystemClock_Config', stopped=True)
    check(c.reset and c.read(BKP + 16) == 4, 'clock failure did not mark a recovery reset')
scenarios.append('pre-runtime watchdog, IWDG/software fault streaks, corrupt BKP, tick wrap and capped backoff')

def rf_hardware(c, init_status=0):
    c.call('MX_SPI1_Init')
    c.on('rf_init', lambda: c.ret(init_status))
    for name in ('rf_set_default_para', 'rf_set_tx_power', 'rf_enter_continous_rx'):
        c.on(name, lambda: c.ret())
    c.on('rf_read_freq', lambda: c.ret(433000000))
    c.on('rf_get_mode', lambda: c.ret(6))
    c.on('rf_get_recv_flag', lambda: c.ret())
    c.on('rf_get_transmit_flag', lambda: c.ret())

# RF failure must escalate even though the main loop can still feed IWDG.
c = cpu(); rf_hardware(c, 1)
for attempt in range(20):
    check(not c.call('lora_rf_wake'), 'failed RF init accepted')
check(c.read('radioRecoverPending', 1) == 1, 'failed RF did not enter recovery')
c.call('serviceRadio', stopped=True)
check(c.reset and c.read(BKP + 16) == 1, 'persistent RF failure did not escalate')

# Recovery reports remain possible with LoRa off, and init success alone
# cannot clear the count. Healthy checks must span sixty continuous seconds.
c = cpu(); rf_hardware(c)
c.call('scheduleRadioRecovery')
c.now = 60000
c.call('serviceRadio')
check(c.read('dtuQueueCount', 1) == 1, 'RF-off recovery report suppressed')
check(c.read(c.symbols['rfRecovery'] + 8, 2) != 0, 'RF failure history vanished at init')
c.call('MX_USART3_UART_Init')
c.call('noteDtuReply')
c.write('wdgStreak', 4)
c.call('serviceRecovery')
c.now += 59000
c.call('noteDtuReply'); c.write('radioProbeTick', c.now)
c.call('serviceRecovery')
check(c.read('wdgStreak') == 4, 'healthy state cleared too early')
c.now += 1000
c.call('noteDtuReply'); c.write('radioProbeTick', c.now)
c.call('serviceRecovery')
check(c.read('wdgStreak') == 0, 'healthy services did not clear streak')

c = cpu(); rf_hardware(c)
c.call('lora_rf_wake'); c.call('MX_USART3_UART_Init'); c.call('noteDtuReply')
c.write('wdgStreak', 4); c.call('serviceRecovery')
c.now = 59000; c.call('noteUartFailure'); c.call('noteDtuReply')
c.write('radioProbeTick', c.now); c.call('serviceRecovery')
c.now = 60000; c.call('serviceRecovery')
check(c.read('wdgStreak') == 4, 'UART fault failed to restart the stable-health timer')
c.now = 119000; c.call('noteDtuReply'); c.write('radioProbeTick', c.now)
c.call('serviceRecovery')
check(c.read('wdgStreak') == 0, 'new stable-health interval did not complete')

c = cpu(); rf_hardware(c, 1)
c.call('MX_USART3_UART_Init'); c.call('noteDtuReply')
c.call('scheduleRadioRecovery'); c.write('wdgStreak', 4)
for now in (1000, 61000, 121000):
    c.now = now; c.call('noteDtuReply'); c.call('serviceRecovery')
check(c.read('wdgStreak') == 4, 'RF-off failure was mistaken for healthy uptime')

def feed(c, frame):
    for byte in frame.encode('ascii') + b'\r\n':
        c.call('centen_ring_push', c.symbols['centenDtuRing'], byte)
    c.call('processDtuInput')

# An intentional RF shutdown suspends recovery; it must not cause a reboot.
c = cpu(); rf_hardware(c, 1)
c.text('deviceName', 'v3-108')
c.call('scheduleRadioRecovery')
feed(c, json.dumps({'cominfo': json.dumps({'cmd': 'lorasw', 'deviceId': 'v3-108', 'value': 0})}))
c.now = 200000
c.call('serviceRadio')
check(not c.reset and not c.read('radioRecoverPending', 1), 'manual RF off did not suspend recovery')

# Work-window closure also suspends escalation, without discarding its budget.
c = cpu(); rf_hardware(c, 1)
c.text('configStr', '30,00')
feed(c, 'config,nettime,ok,2026,11,26,12,0,0,0')
for _ in range(20): c.call('scheduleRadioRecovery')
c.call('serviceRadio')
check(not c.reset, 'out-of-window RF recovery rebooted the MCU')
feed(c, 'config,nettime,ok,2026,11,26,0,0,0,0')
c.call('serviceRadio', stopped=True)
check(c.reset, 'RF escalation budget lost across closed window')

c = cpu(); scratch = 0x20004000
c.cpu.mem_write(scratch, bytes(12))
c.call('centen_recovery_failed', scratch, 0xfffffff0)
c.call('centen_recovery_stable', scratch, 0xfffffff0, 1)
check(not c.call('centen_recovery_stable', scratch, 59983, 1), 'stability completed early across wrap')
check(c.call('centen_recovery_stable', scratch, 59984, 1), 'stability tick wrap failed')
scenarios.append('persistent RF escalation, RF-off reports and sixty-second local-health clearing')

# Real HAL abort/deinit/init/receive rebuilds the MCU UART, then an inability
# to arm the receive engine causes a marked software reset after three tries.
c = cpu(); c.call('MX_USART3_UART_Init')
for _ in range(3): c.call('noteUartFailure')
c.now = 1000
c.call('serviceUartRecovery')
check(c.read('uartRebuildCount', 1) == 1 and not c.read('uartRecoveryPending', 1), 'UART rebuild failed')
c.on('HAL_UART_Receive_IT', lambda: c.ret(2))
for i in range(3):
    for _ in range(3): c.call('noteUartFailure')
    c.now += 1000
    c.call('serviceUartRecovery', stopped=(i == 2))
check(c.reset and c.read(BKP + 16) == 2, 'persistent UART failure did not escalate')

# A network outage only backs off DTU resets. A non-responsive local modem
# has a separate escalation and is not blocked by a long network cooldown.
c = cpu()
for i in range(6):
    c.now = max(c.now, c.read('dtuNextAutoReset'))
    check(c.call('dtuAutomaticReset', 0) == 1, 'network reset not attempted')
    check(c.call('dtuAutomaticReset', 0) == 0, 'network reset ignored cooldown')
check(not c.reset, 'network outage reset the MCU')
check(c.read('dtuNextAutoReset') - c.now == 1800000, 'network backoff not capped')
for i in range(4):
    c.now = (c.now + 180001) & 0xffffffff
    c.call('dtuAutomaticReset', 1, stopped=(i == 3))
check(c.reset and c.read(BKP + 16) == 3, 'unresponsive DTU did not escalate')
scenarios.append('real UART rebuild, persistent UART escalation, network-only backoff and unresponsive DTU escalation')

result = {'checks': checks, 'scenarios': scenarios, 'image': str(IMAGE),
          'limits': 'Mocked hardware and controlled ticks; no physical brownout, RF or board watchdog validation'}
print(json.dumps(result, ensure_ascii=False, indent=2))
(ROOT / 'build/tests_recovery.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
