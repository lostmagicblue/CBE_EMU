from pathlib import Path
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: disasm_thumb.py [--linear] <addr> [count]\n       disasm_thumb.py --file <path> <file_code_offset> <base> <addr> [count]")

    linear = False
    arg = 1
    data = None
    image_base = 0x1000000
    file_code_offset = 0x9A
    if sys.argv[arg] == "--file":
        data = Path(sys.argv[arg + 1]).read_bytes()
        file_code_offset = int(sys.argv[arg + 2], 0)
        image_base = int(sys.argv[arg + 3], 0)
        arg += 4
    if sys.argv[arg] == "--linear":
        linear = True
        arg += 1

    start = int(sys.argv[arg], 0)
    count = int(sys.argv[arg + 1], 0) if len(sys.argv) > arg + 1 else 80
    if data is None:
        data = Path("bin/CBE/江湖OL.cbe").read_bytes()
    off = file_code_offset + start - image_base
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    if not linear:
        for n, ins in enumerate(md.disasm(data[off : off + 0x800], start)):
            print(f"{ins.address:08x}: {ins.mnemonic}\t{ins.op_str}")
            if n + 1 >= count:
                break
        return

    printed = 0
    pos = 0
    while printed < count and off + pos < len(data):
        insns = list(md.disasm(data[off + pos : off + pos + 4], start + pos, count=1))
        if insns:
            ins = insns[0]
            print(f"{ins.address:08x}: {ins.mnemonic}\t{ins.op_str}")
            pos += ins.size
        else:
            half = struct.unpack_from("<H", data, off + pos)[0]
            print(f"{start + pos:08x}: .hword\t0x{half:04x}")
            pos += 2
        printed += 1


if __name__ == "__main__":
    main()
