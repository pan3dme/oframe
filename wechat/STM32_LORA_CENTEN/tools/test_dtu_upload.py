"""Execute the real firmware's uplink/DTU queue path in an ARM CPU.

Only ticks, GPIO writes and UART hardware are replaced. This checks bytes
passed to USART3, not modem forwarding or a server-side acknowledgement.
"""
from pathlib import Path
import json
import struct
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parents[1]
image = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/CENTEN.axf'
data = image.read_bytes()
header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
assert header[0][:7] == b'\x7fELF\x01\x01\x01'
phoff, shoff = header[5], header[6]
phsize, phnum, shsize, shnum = header[9:13]
symbols = {}
for i in range(shnum):
    section = struct.unpack_from('<IIIIIIIIII', data, shoff + i * shsize)
    if section[1] != 2:
        continue
    strings = struct.unpack_from('<IIIIIIIIII', data, shoff + section[6] * shsize)
    names = data[strings[4]:strings[4] + strings[5]]
    for offset in range(section[4], section[4] + section[5], section[9]):
        name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
        symbols[names[name:].split(b'\0', 1)[0].decode('ascii', 'replace')] = value

cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
cpu.mem_map(0x08000000, 0x10000)
cpu.mem_map(0x20000000, 0x5000)
for i in range(phnum):
    kind, off, vaddr, paddr, size, memsize, flags, align = struct.unpack_from('<IIIIIIII', data, phoff + i * phsize)
    if kind == 1 and size:
        cpu.mem_write(vaddr, data[off:off + size])

now = 0
uart_status = 0
uart3_output = []
diagnostics = bytearray()
checks = 0

def check(condition, detail):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(detail)

def read_value(name, size=4):
    return int.from_bytes(cpu.mem_read(symbols[name], size), 'little')

def write_value(name, value, size=4):
    cpu.mem_write(symbols[name], value.to_bytes(size, 'little'))

def return_value(value):
    cpu.reg_write(UC_ARM_REG_R0, value)
    cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

def hook(uc, address, size, user):
    if address == (symbols['HAL_GetTick'] & ~1):
        return_value(now)
    elif address == (symbols['HAL_UART_Transmit'] & ~1):
        uart = cpu.reg_read(UC_ARM_REG_R0)
        ptr = cpu.reg_read(UC_ARM_REG_R1)
        count = cpu.reg_read(UC_ARM_REG_R2)
        if uart == symbols['huart3']:
            if not uart_status:
                uart3_output.append(bytes(cpu.mem_read(ptr, count)))
            return_value(uart_status)
        else:
            return_value(0)
    elif address == (symbols['fputc'] & ~1):
        char = cpu.reg_read(UC_ARM_REG_R0)
        diagnostics.append(char & 255)
        return_value(char)
    elif address == (symbols['HAL_GPIO_WritePin'] & ~1):
        return_value(0)

cpu.hook_add(UC_HOOK_CODE, hook)

def call(name, *args):
    for reg, arg in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
        cpu.reg_write(reg, arg)
    cpu.reg_write(UC_ARM_REG_SP, 0x20004ff0)
    cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
    cpu.emu_start(symbols[name] | 1, 0x0800fff0, timeout=3_000_000, count=3_000_000)
    check(cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0, name + ' did not return')

cpu.mem_write(symbols['deviceName'], b'v3-108\0')
write_value('loraSwOn', 1, 1)
write_value('radioTxPendingSlot', 0xffffffff)

# Real OnSlave: reproduce the user's exact first uplink, including optional NUL.
# RxDoneMsg is packed: 255 payload, 16 PLHD, 10 test, uint16 PLHD/Size/index,
# 1024 mapm, int8 RSSI, float SNR. Match Common/RadioF103/inc/pan3029_rf.h.
payload = b'2|v3-0|63000|78|130\0'
cpu.mem_write(symbols['RxDoneParams'], payload)
cpu.mem_write(symbols['RxDoneParams'] + 283, struct.pack('<H', len(payload)))
cpu.mem_write(symbols['RxDoneParams'] + 1311, struct.pack('<Bf', 201, 14.0))
now = 2800
call('rf_set_recv_flag', 2)
call('OnSlave')
check(read_value('dtuQueueCount', 1) == 1, 'TIME uplink was not queued')
call('processDtuQueue')
check(not uart3_output and read_value('dtuLastSendTick') == 0, 'deferred boot send consumed throttle')
now = 4999
call('processDtuQueue')
check(not uart3_output and read_value('dtuLastSendTick') == 0, 'sent before boot grace elapsed')
now = 5000
call('processDtuQueue')
report = json.loads(b''.join(uart3_output))
check(report['params']['lorainfo'] == '2|v3-0|63000|78|130', 'wrong forwarded uplink')
check(report['params']['upDateDevice'] == 'v3-108', 'wrong relay name')
check(report['params']['rssi'] == -55 and report['params']['snr'] == 14, 'wrong radio metadata')
check(read_value('dtuQueueCount', 1) == 0, 'successful UART send did not dequeue')
check(b'[DTU Q]' in diagnostics and b'[DTU Q TX]' in diagnostics, 'missing queue/send diagnostics')

# A radio command (type 9) must not be uploaded as if it were telemetry.
uart3_output.clear()
payload = b'9|v3-0|upgps|0\0'
cpu.mem_write(symbols['RxDoneParams'], payload)
cpu.mem_write(symbols['RxDoneParams'] + 283, struct.pack('<H', len(payload)))
call('rf_set_recv_flag', 2)
call('OnSlave')
check(read_value('dtuQueueCount', 1) == 0, 'radio command incorrectly queued as telemetry')

# A failed send retains the message and throttles only the actual attempt.
scratch = 0x20004600
cpu.mem_write(scratch, b'1|v3-0|30.12345,109.39087|\0')
call('sendToDtu', scratch, 0xffffffc9, 14)
now = 7000
uart_status = 2  # HAL_BUSY
call('processDtuQueue')
check(read_value('dtuQueueCount', 1) == 1, 'HAL_BUSY dropped the report')
check(b'[DTU TX FAIL]' in diagnostics, 'missing send failure diagnostics')
uart_status = 0
now = 8999
call('processDtuQueue')
check(not uart3_output, 'failed-send retry was not throttled')
now = 9000
call('processDtuQueue')
check(read_value('dtuQueueCount', 1) == 0 and len(uart3_output) == 2, 'retained report did not retry')

# While a query is pending, time must not be spent repeatedly resetting throttle.
uart3_output.clear()
call('sendToDtu', scratch, 0xffffffc9, 14)
write_value('dtuQueryPending', 1, 1)
write_value('dtuQueryTick', 10000)
now = 11000
call('processDtuQueue')
check(read_value('dtuLastSendTick') == 9000, 'query deferral consumed throttle')
cpu.mem_write(scratch, b'config,nettime,ok,2026,11,24,17,30,12,0\0')
call('processDtuFrame', scratch)
check(read_value('dtuQueryPending', 1) == 0, 'valid nettime did not release query wait')
check(read_value('dtuTimeValid', 1) == 1, 'nettime was not accepted')
call('processDtuQueue')
check(read_value('dtuQueueCount', 1) == 0 and len(uart3_output) == 2, 'upload stalled after query response')
check(b'[DTU RX]' in diagnostics and b'[DTU TIME]' in diagnostics, 'missing DTU response/time diagnostics')

message = f'PASS: {checks} full-firmware ARM upload checks; real OnSlave -> JSON queue -> USART3 bytes verified.'
print(message)
(ROOT / 'build/tests_dtu_upload.log').write_text(message + '\n', encoding='utf-8')
