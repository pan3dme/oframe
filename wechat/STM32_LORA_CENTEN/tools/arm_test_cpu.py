"""Small ELF/Unicorn harness. HAL/RF hardware may be replaced by each test."""
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]

class ArmCPU:
    def __init__(self, image):
        data = Path(image).read_bytes()
        h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
        assert h[0][:7] == b'\x7fELF\x01\x01\x01'
        self.symbols = {}
        for i in range(h[12]):
            s = struct.unpack_from('<IIIIIIIIII', data, h[6] + i * h[11])
            if s[1] != 2:
                continue
            strings = struct.unpack_from('<IIIIIIIIII', data, h[6] + s[6] * h[11])
            names = data[strings[4]:strings[4] + strings[5]]
            for offset in range(s[4], s[4] + s[5], s[9]):
                n, value, size, info, other, idx = struct.unpack_from('<IIIBBH', data, offset)
                self.symbols[names[n:].split(b'\0', 1)[0].decode('ascii', 'replace')] = value
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in ((0x08000000, 0x10000), (0x20000000, 0x5000),
                              (0x40000000, 0x30000), (0xe000e000, 0x2000)):
            self.cpu.mem_map(address, size)
        for i in range(h[10]):
            kind, offset, address, paddr, size, memsize, flags, align = struct.unpack_from('<IIIIIIII', data, h[5] + i * h[9])
            if kind == 1 and size:
                self.cpu.mem_write(address, data[offset:offset + size])
        self.handlers = {}
        self.now = 0
        self.reset = False
        self.output = bytearray()
        self.watchdog_writes = []
        self.calls = []
        self.cpu.hook_add(UC_HOOK_CODE, self._code)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self._write)
        self.on('HAL_GetTick', lambda: self.ret(self.now))
        self.on('HAL_Delay', self._delay)
        self.on('fputc', self._print)

    def on(self, name, handler):
        if name in self.symbols:
            self.handlers[self.symbols[name] & ~1] = (name, handler)

    def _code(self, uc, address, size, user):
        entry = self.handlers.get(address)
        if entry:
            self.calls.append(entry[0])
            entry[1]()

    def _write(self, uc, kind, address, size, value, user):
        if address == 0x40003000:
            self.watchdog_writes.append(value)
        if address == 0xe000ed0c and value & 4:
            self.reset = True
            uc.emu_stop()

    def _delay(self):
        self.now = (self.now + self.arg(0)) & 0xffffffff
        self.ret()

    def _print(self):
        self.output.append(self.arg(0) & 255)
        self.ret(self.arg(0))

    def arg(self, i):
        return self.cpu.reg_read(REGS[i])

    def ret(self, value=0):
        self.cpu.reg_write(UC_ARM_REG_R0, value)
        self.cpu.reg_write(UC_ARM_REG_PC, self.cpu.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args, stopped=False):
        for reg, value in zip(REGS, args):
            self.cpu.reg_write(reg, value & 0xffffffff)
        for i, value in enumerate(args[4:]):
            self.write(0x20004e00 + i * 4, value)
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20004e00)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800e001)
        self.cpu.emu_start(self.symbols[name] | 1, 0x0800e000, timeout=5_000_000, count=10_000_000)
        if not stopped:
            assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800e000, name + ' did not return'
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def address(self, name):
        return self.symbols[name] if isinstance(name, str) else name

    def read(self, name, size=4):
        return int.from_bytes(self.cpu.mem_read(self.address(name), size), 'little')

    def write(self, name, value, size=4):
        self.cpu.mem_write(self.address(name), (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little'))

    def text(self, name, value):
        self.cpu.mem_write(self.address(name), value.encode('ascii') + b'\0')

    def string(self, name, size=32):
        return bytes(self.cpu.mem_read(self.address(name), size)).split(b'\0', 1)[0].decode('ascii')
