#!/usr/bin/env python3
# mpegv.library's tests: a 68k program's cycles under vamos (Musashi counts
# a 68020's), all of them: its own code, the libraries it opens, the
# little vamos does for it. Run with amitools' Python:
#
#   cycles.py -- [vamos arguments] program [its arguments]
#
# MIT licence, (c) 2026 Brian King.
import atexit
import sys

from amitools.vamos.machine import machine as mm

total = [0]
_execute = mm.Machine.execute


def execute(self, max_cycles=1000):
    er = _execute(self, max_cycles)
    total[0] += er.cycles
    return er


mm.Machine.execute = execute


def report():
    print("total %d cycles" % total[0], file=sys.__stdout__)


atexit.register(report)

from amitools.tools.vamos import main

sys.argv = ["vamos"] + sys.argv[sys.argv.index("--") + 1:]
sys.exit(main())
