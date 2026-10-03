#!/usr/bin/env python3
"""Print an used/limit memory report from an SDLD .mem file.

The SDCC linker (sdld) prints only the bare errors to stdout:

    ?ASlink-Error-Insufficient EXTERNAL RAM memory.
    ?ASlink-Error-Insufficient ROM/EPROM/FLASH memory.

while the actual numbers (used / limit) land in the .mem file it writes next
to the output.  This tool surfaces them in the make log, so an overflowing link
says how much was used, how much fits and by how much it overshoots:

    memory report: build/osrctherm_r02_v1.01_CA51F253L3.mem
      PAGED EXT. RAM        0 / 256   free 256
      EXTERNAL RAM       2084 / 2048  over by 36
      ROM/EPROM/FLASH   34289 / 32256  over by 2033
      stack: 156 bytes available (SP 0x64)

Invoked by the CA51F253L3 Makefile only when a link fails, so a clean build
keeps its current log.  The exit status is always 0: the build result belongs
to make, not to this reporter.
"""
import re
import sys


def _columns(dashes):
    """Column ranges of the sdld table, taken from its '---- ----' ruler."""
    return [(m.start(), m.end()) for m in re.finditer(r"-+", dashes)]


def _rows(text):
    """(name, size, max) for every row of the 'Other memory:' table."""
    lines = text.splitlines()
    rows = []
    for i, line in enumerate(lines):
        if line.strip() != "Other memory:":
            continue
        if i + 2 >= len(lines):
            break
        cols = _columns(lines[i + 2])
        if len(cols) < 5:
            break
        for row in lines[i + 3:]:
            if row.startswith("***") or not row.strip():
                break
            padded = row.ljust(cols[4][1])
            try:
                name = padded[cols[0][0]:cols[0][1]].strip()
                size = int(padded[cols[3][0]:cols[3][1]].strip())
                mx = int(padded[cols[4][0]:cols[4][1]].strip())
            except ValueError:
                break          # not a table row any more
            if name:
                rows.append((name, size, mx))
        break
    return rows


def _stack(text):
    """(sp, bytes_available) or None - sdld omits the line when there is none."""
    m = re.search(r"Stack starts at:\s*(0x[0-9a-fA-F]+).*?with\s+(\d+)\s+"
                  r"bytes available", text)
    return (m.group(1), int(m.group(2))) if m else None


def report(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        print("memory report: cannot read %s (%s)" % (path, exc))
        print("no memory report: the linker did not produce it")
        return

    rows = _rows(text)
    print("memory report: %s" % path)
    if not rows:
        print("no memory report: no 'Other memory' table in %s" % path)
        return

    width = max(len(name) for name, _, _ in rows)
    for name, size, mx in rows:
        if size > mx:
            state = "over by %d" % (size - mx)
        else:
            state = "free %d" % (mx - size)
        print("  %-*s %7d / %-7d %s" % (width, name, size, mx, state))

    st = _stack(text)
    if st:
        print("  stack: %d bytes available (SP %s)" % (st[1], st[0]))


def main(argv):
    if len(argv) < 2:
        print("usage: mem_report.py <file.mem>")
        return 0
    report(argv[1])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
