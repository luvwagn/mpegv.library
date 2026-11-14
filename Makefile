# mpegv.library: MPEG-1 video decoded by the CPU (pl_mpeg inside), for
# any Amiga program. MIT licence, (c) 2026 Brian King.
#
#   make            build/mpegv.library and the example (build/mpvplay)
#   make test       the library's pictures against pl_mpeg's own, bit for
#                   bit, on this computer and on a 68020 under vamos
#   make prof MPG=file.mpg [N=60]
#                   a 68020's cycles under vamos: through the library, and
#                   pl_mpeg called directly as a program would build it in
#   make lha        build/mpegv.lha, for Aminet
#
# Needs m68k-amigaos-gcc (bebbo's toolchain, libnix), and for the tests a
# C compiler for this computer and amitools' vamos.

CC      = m68k-amigaos-gcc
STRIP   = m68k-amigaos-strip
HOSTCC ?= cc
VERSION = 1.0
# A 68020 or better, tuned as for any of them (its hot paths are integer:
# one build serves the 68020 to the 68060)
CPU     = -m68020-60
LIBCC   = $(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer
WARN    = -Wall -Wextra -Wno-unused-parameter
INC     = -Iinclude -Ithird_party/pl_mpeg -Isrc
LIB     = build/mpegv.library
HDR     = $(wildcard include/*/*.h) third_party/pl_mpeg/pl_mpeg.h
FIX     = tests/fixtures/ps.mpg tests/fixtures/es.m1v

.PHONY: all test prof lha clean

all: $(LIB) build/mpvplay

build/libinit.o: src/libinit.c src/libinit.h
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) -Wall -Isrc -DLIB_RT_VERSION=1 -c -o $@ $<

build/mpegv.o: src/mpegv.c $(HDR)
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) $(WARN) $(INC) -c -o $@ $<

$(LIB): build/libinit.o build/mpegv.o
	@echo "  LD    $@"
	@$(LIBCC) -o $@.dbg $^ -lm -lamiga
	@$(STRIP) -o $@ $@.dbg

# the example: a file played in a window on an RTG (or AGA) screen
build/mpvplay: examples/mpvplay.c $(HDR)
	@mkdir -p build
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul $(WARN) -Iinclude -o $@ $< -lamiga

# ---- the tests: each picture's hash (and its RGB, YUV and grey), through
# the library's functions, and from pl_mpeg called directly
build/host/mpv_test: tests/mpv_test.c src/mpegv.c $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 $(WARN) -Wno-sign-compare -Itests/host $(INC) -o $@ tests/mpv_test.c src/mpegv.c
build/host/plm_ref: tests/plm_ref.c $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -Ithird_party/pl_mpeg -o $@ $<
build/68k/mpv_test: tests/mpv_test.c $(HDR) $(LIB)
	@mkdir -p build/68k/Libs
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul $(WARN) -Iinclude -o $@ tests/mpv_test.c
	@cp $(LIB) build/68k/Libs/
build/68k/plm_ref: tests/plm_ref.c $(HDR)
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -w -Ithird_party/pl_mpeg -o $@ $< -lm

test: build/host/mpv_test build/host/plm_ref build/68k/mpv_test build/68k/plm_ref $(FIX)
	@for f in $(FIX); do \
		build/host/plm_ref $$f > build/host/ref.txt; \
		build/host/mpv_test $$f > build/host/lib.txt; \
		cmp -s build/host/ref.txt build/host/lib.txt || { diff build/host/ref.txt build/host/lib.txt | head; exit 1; }; \
	done; echo "  CHECK the library's pictures as pl_mpeg's (host) ok"
	@for f in $(FIX); do build/host/mpv_test -all $$f; done > build/host/all.txt; \
		cmp -s build/host/all.txt tests/expected.txt \
		&& echo "  CHECK pictures, RGB, YUV, grey and skipping as expected (host) ok" \
		|| { diff build/host/all.txt tests/expected.txt | head; exit 1; }
	@cp $(FIX) build/68k/
	@cd build/68k && for f in $(notdir $(FIX)); do \
		vamos -C 68020 -m 8192 -V prog:$(abspath build/68k) -- prog:mpv_test -all prog:$$f; done \
		> all.txt 2> err.txt; \
		cmp -s all.txt ../../tests/expected.txt \
		&& echo "  CHECK the same through mpegv.library on a 68020 (vamos) ok" \
		|| { diff all.txt ../../tests/expected.txt | head; cat err.txt; exit 1; }

# a 68020's cycles (vamos counts them): pl_mpeg built into the program, and
# the same pictures through mpegv.library
N ?= 60
VAMOS_PYTHON ?= $(dir $(shell command -v vamos))python
prof: build/68k/mpv_test build/68k/plm_ref
	@ln -sf $(abspath $(MPG)) build/68k/bench.mpg
	@cd build/68k && $(VAMOS_PYTHON) $(abspath tests/cycles.py) -- -C 68020 -m 8192 -s 64 \
		-V prog:$(abspath build/68k) -- prog:plm_ref prog:bench.mpg $(N) nohash
	@cd build/68k && $(VAMOS_PYTHON) $(abspath tests/cycles.py) -- -C 68020 -m 8192 -s 64 \
		-V prog:$(abspath build/68k) -- prog:mpv_test -n $(N) -nohash prog:bench.mpg

# ---- the Aminet archive: the library, its includes, autodoc, the example
# and the whole source
lha: all
	@rm -rf build/lha && mkdir -p build/lha/mpegv/Libs build/lha/mpegv/Include build/lha/mpegv/Example \
		build/lha/mpegv/Source
	@cp $(LIB) build/lha/mpegv/Libs/
	@cp -R include/* build/lha/mpegv/Include/
	@cp mpegv.doc mpegv_lib.sfd README.md LICENSE build/lha/mpegv/
	@cp build/mpvplay examples/mpvplay.c build/lha/mpegv/Example/
	@cp -R Makefile src include third_party tests examples mpegv_lib.sfd mpegv.doc build/lha/mpegv/Source/
	@cp mpegv.readme build/lha/mpegv.readme
	@cd build/lha && rm -f ../mpegv.lha && lha aqo5 ../mpegv.lha mpegv mpegv.readme
	@echo "  LHA   build/mpegv.lha"

clean:
	rm -rf build
