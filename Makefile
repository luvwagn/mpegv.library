# mpegv.library: MPEG-1 and MPEG-2 video decoded by the CPU (libmpeg2
# inside), for any Amiga program. GPL-3.0-or-later, (c) 2026 Brian King.
#
#   make            build/mpegv.library and the example (build/mpvplay)
#   make test       the library's pictures against libmpeg2's own C (as it
#                   was released: MPEGV_PLAIN), on this computer, and the
#                   same through the library on a 68020 under vamos
#   make psnr       each fixture's pictures against ffmpeg's (needs ffmpeg)
#   make prof MPG=file [N=60]
#                   a 68020's cycles under vamos (the decoder built into a
#                   program), and where they go, by stage and function
#   make lha        build/mpegv.lha, for Aminet (no source: it points to
#                   https://github.com/luvwagn/mpegv.library)
#
# Needs m68k-amigaos-gcc (bebbo's toolchain, libnix), and for the tests a
# C compiler for this computer and amitools' vamos.

CC      = m68k-amigaos-gcc
STRIP   = m68k-amigaos-strip
HOSTCC ?= cc
VERSION = 2.0
# A 68020 or better, tuned as for any of them (its hot paths are integer:
# one build serves the 68020 to the 68060)
CPU     = -m68020-60
LIBCC   = $(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer
WARN    = -Wall -Wextra -Wno-unused-parameter
L2      = third_party/libmpeg2
INC     = -Iinclude -Isrc -I$(L2)/include -I$(L2)/libmpeg2
LIB     = build/mpegv.library
HDR     = $(wildcard include/*/*.h) $(wildcard $(L2)/include/*.h) $(L2)/libmpeg2/mpeg2_internal.h \
          $(L2)/libmpeg2/vlc.h src/config.h
L2SRC   = $(addprefix $(L2)/libmpeg2/,alloc.c cpu_accel.c cpu_state.c decode.c header.c idct.c \
          motion_comp.c slice.c)
SRC     = src/mpegv.c src/convert.c $(L2SRC)
L2OBJ   = $(patsubst $(L2)/libmpeg2/%.c,build/l2/%.o,$(L2SRC))
FIX     = tests/fixtures/ps.mpg tests/fixtures/es.m1v tests/fixtures/pal.m2v \
          tests/fixtures/ntsc.m2v tests/fixtures/dvd.vob

.PHONY: all test psnr prof lha clean

all: $(LIB) build/mpvplay

build/libinit.o: src/libinit.c src/libinit.h
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) -Wall -Isrc -DLIB_RT_VERSION=2 -c -o $@ $<

build/%.o: src/%.c $(HDR)
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) $(WARN) $(INC) -c -o $@ $<

build/l2/%.o: $(L2)/libmpeg2/%.c $(HDR)
	@mkdir -p build/l2
	@echo "  CC    $<"
	@$(LIBCC) -Wall -Wno-unused $(INC) -c -o $@ $<

$(LIB): build/libinit.o build/mpegv.o build/convert.o $(L2OBJ)
	@echo "  LD    $@"
	@$(LIBCC) -o $@.dbg $^ -lamiga
	@$(STRIP) -o $@ $@.dbg

# the example: a file played in a window on an RTG (or AGA) screen
build/mpvplay: examples/mpvplay.c $(HDR)
	@mkdir -p build
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul $(WARN) -Iinclude -o $@ $< -lamiga

# ---- the tests: each picture's hash (and its RGB, YUV and grey), through
# the library's functions; and through libmpeg2's C as it was released
build/host/mpv_test: tests/mpv_test.c $(SRC) $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -Itests/host $(INC) -o $@ tests/mpv_test.c $(SRC)
build/host/mpv_plain: tests/mpv_test.c $(SRC) $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -DMPEGV_PLAIN -Itests/host $(INC) -o $@ tests/mpv_test.c $(SRC)
build/68k/mpv_test: tests/mpv_test.c $(HDR) $(LIB)
	@mkdir -p build/68k/Libs
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul $(WARN) -Iinclude -o $@ tests/mpv_test.c
	@cp $(LIB) build/68k/Libs/
# the decoder built into the program (for make prof), each source its own
# object, so that their static functions can be placed (tests/syms.py)
PROFOBJ = $(patsubst %.c,build/68k/obj/%.o,$(notdir tests/mpv_test.c $(SRC)))
build/68k/obj/%.o: tests/%.c $(HDR)
	@mkdir -p build/68k/obj
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w -DMPV_STATIC $(INC) -c -o $@ $<
build/68k/obj/%.o: src/%.c $(HDR)
	@mkdir -p build/68k/obj
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w -DMPV_STATIC $(INC) -c -o $@ $<
build/68k/obj/%.o: $(L2)/libmpeg2/%.c $(HDR)
	@mkdir -p build/68k/obj
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w -DMPV_STATIC $(INC) -c -o $@ $<
build/68k/mpv_prof: $(PROFOBJ)
	@echo "  LD    $@"
	@$(CC) $(CPU) -noixemul -Wl,-Map=$@.map -o $@ $^
	@python3 tests/syms.py $@.map $^ > $@.syms

# the IDCT against libmpeg2's C, block by block, and its IEEE 1180
# accuracy (on the build machine), on both
build/host/%_test: tests/%_test.c $(HDR) $(SRC)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w $(INC) -o $@ $< -lm
build/68k/%_test: tests/%_test.c $(HDR) $(SRC)
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w -DNO_IEEE $(INC) -o $@ $<

test: build/host/mpv_test build/host/mpv_plain build/68k/mpv_test $(FIX) build/host/idct_test \
		build/68k/idct_test
	@build/host/idct_test > build/host/idct.txt \
		|| { cat build/host/idct.txt; exit 1; }; sed 's/^/  /' build/host/idct.txt
	@cd build/68k && vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:idct_test 20000 \
		2>/dev/null > idct.txt || { cat idct.txt; exit 1; }; sed 's/^/  (68020) /' idct.txt
	@for f in $(FIX); do \
		build/host/mpv_plain $$f > build/host/ref.txt; \
		build/host/mpv_test $$f > build/host/lib.txt; \
		cmp -s build/host/ref.txt build/host/lib.txt || { echo $$f; diff build/host/ref.txt build/host/lib.txt | head; exit 1; }; \
	done; echo "  CHECK the pictures as libmpeg2's own C decodes them (host) ok"
	@for f in $(FIX); do build/host/mpv_test -all $$f; done > build/host/all.txt; \
		cmp -s build/host/all.txt tests/expected.txt \
		&& echo "  CHECK pictures, times, RGB, YUV, grey, skipping, flushing as expected (host) ok" \
		|| { diff build/host/all.txt tests/expected.txt | head; exit 1; }
	@cp $(FIX) build/68k/
	@cd build/68k && for f in $(notdir $(FIX)); do \
		vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:mpv_test -all prog:$$f; done \
		> all.txt 2> err.txt; \
		cmp -s all.txt ../../tests/expected.txt \
		&& echo "  CHECK the same through mpegv.library on a 68020 (vamos) ok" \
		|| { diff all.txt ../../tests/expected.txt | head; cat err.txt; exit 1; }

# each fixture's pictures against ffmpeg's: PSNR (dB) of Y, Cb, Cr, the
# worst picture's and the mean
psnr: build/host/mpv_test $(FIX)
	@for f in $(FIX); do python3 tests/psnr.py build/host/mpv_test $$f; done

# a 68020's cycles (vamos counts them), and where they go: the decoder
# built into a program, its PC sampled (tests/prof.py), each function put
# in its stage (tests/stages.py)
N ?= 60
VAMOS_PYTHON ?= $(dir $(shell command -v vamos))python
prof: build/68k/mpv_prof
	@ln -sf $(abspath $(MPG)) build/68k/bench.mpg
	@cd build/68k && rm -f prof.base && $(VAMOS_PYTHON) $(abspath tests/prof.py) mpv_prof.syms -- \
		-C 68020 -m 16384 -H disable -s 64 --slice-cycles 700 -- mpv_prof -n $(N) -nohash $(PROFOPT) bench.mpg \
		> prof.txt 2>&1; python3 $(abspath tests/stages.py) prof.txt

# ---- the Aminet archive: the library, its includes, autodoc and the
# example (built); the source is on GitHub (README.md, mpegv.readme)
lha: all
	@rm -rf build/lha && mkdir -p build/lha/mpegv/Libs build/lha/mpegv/Include build/lha/mpegv/Example
	@cp $(LIB) build/lha/mpegv/Libs/
	@cp -R include/* build/lha/mpegv/Include/
	@cp mpegv.doc mpegv_lib.sfd README.md LICENSE CHANGES build/lha/mpegv/
	@cp build/mpvplay examples/mpvplay.c build/lha/mpegv/Example/
	@cp mpegv.readme build/lha/mpegv.readme
	@cd build/lha && rm -f ../mpegv.lha && lha aqo5 ../mpegv.lha mpegv mpegv.readme
	@echo "  LHA   build/mpegv.lha"

clean:
	rm -rf build
