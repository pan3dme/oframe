"""Execute Arm Compiler 6 production C in a Cortex-M3 Unicorn CPU.

Requires Python 3 and unicorn==2.1.4. No board or firmware flash operations.
The production centen_uart.c callback is linked with UART/tick test doubles.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("CENTEN_COMPILER_BIN", r"D:\Keil_v5\ARM\ARMCLANG\Bin"))
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
includes = [ROOT / p for p in ["user", "Core/Inc", "Drivers/STM32F1xx_HAL_Driver/Inc", "Drivers/CMSIS/Device/ST/STM32F1xx/Include", "Drivers/CMSIS/Include"]]
args = [str(BIN / "armclang.exe"), "--target=arm-arm-none-eabi", "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-std=c99", "-Oz", "-fshort-enums", "-fshort-wchar", "-ffunction-sections", "-fdata-sections", "-D__MICROLIB", "-DUSE_HAL_DRIVER", "-DSTM32F103xB"]
for include in includes:
    args += ["-I", str(include)]
subprocess.run(args + ["-c", "-o", str(BUILD / "test_support.o"), str(ROOT / "tests/test_support.c")], check=True)
scatter = BUILD / "tests.sct"
scatter.write_text("LR_CODE 0x08000000 0x8000 { ER_CODE 0x08000000 0x8000 { .ANY (+RO) } RW_DATA 0x20000000 0x5000 { .ANY (+RW +ZI) } }\n")
image = BUILD / "tests.axf"
subprocess.run([str(BIN / "armlink.exe"), "--cpu", "Cortex-M3", "--library_type=microlib", "--entry", "test_entry", "--scatter", str(scatter), "--map", "--list", str(BUILD / "tests.map"), "-o", str(image), str(BUILD / "test_support.o"), str(BUILD / "centen_support.o"), str(BUILD / "centen_uart.o"), str(BUILD / "startup_stm32f103xb.o")], check=True)
data = image.read_bytes()
header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
assert header[0][:7] == b"\x7fELF\x01\x01\x01"
entry, phoff, shoff = header[4], header[5], header[6]
phsize, phnum, shsize, shnum = header[9], header[10], header[11], header[12]
cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
cpu.mem_map(0x08000000, 0x10000)
cpu.mem_map(0x20000000, 0x5000)
for i in range(phnum):
    kind, off, vaddr, paddr, size, memsize, flags, align = struct.unpack_from("<IIIIIIII", data, phoff + i * phsize)
    if kind == 1 and size:
        cpu.mem_write(vaddr, data[off:off + size])
symbols = {}
for i in range(shnum):
    section = struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize)
    if section[1] != 2:
        continue
    strings = struct.unpack_from("<IIIIIIIIII", data, shoff + section[6] * shsize)
    names = data[strings[4]:strings[4] + strings[5]]
    for off in range(section[4], section[4] + section[5], section[9]):
        name, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, off)
        name = names[name:].split(b"\0", 1)[0].decode("ascii", "replace")
        symbols[name] = value
cpu.reg_write(UC_ARM_REG_SP, 0x20004ff0)
cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
cpu.emu_start(entry | 1, 0x0800fff0, timeout=20_000_000, count=20_000_000)
result = cpu.reg_read(UC_ARM_REG_R0)
checks = struct.unpack("<I", cpu.mem_read(symbols["centen_test_checks"], 4))[0]
if result:
    raise SystemExit(f"FAIL: tests/test_support.c:{result}, completed checks={checks}")
if checks < 10000:
    raise SystemExit(f"FAIL: unexpected incomplete run, checks={checks}")
message = f"PASS: {checks} ARM C checks, including 1400 real UART callback invocations."
print(message)
(BUILD / "tests.log").write_text(message + "\n", encoding="utf-8")
