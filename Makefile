# mpegv.library: MPEG-1 and MPEG-2 video decoded by the CPU (libmpeg2
# inside), for any Amiga program. GPL-3.0-or-later, (c) 2026 Brian King.
#
#   make            build/mpegv.library and the example (build/mpvplay)
#   make test       the library's pictures against libmpeg2's own C (as it
#                   was released: MPEGV_PLAIN), on this computer, and the
#                   same through the library on a 68020 under vamos
#   make psnr       each fixture's pictures against ffmpeg's (needs ffmpeg)
#   make halfpsnr [MPG=file] [N=pictures]
#                   half size's pictures against the whole ones averaged 2x2
#   make prof MPG=file [N=60]
#                   a 68020's cycles under vamos (the decoder built into a
#                   program), and where they go, by stage and function
#   make lha        build/mpegv.lha, for Aminet (no source: it points to
#                   https://github.com/luvwagn/mpegv.library)
#
# Needs AmigaPorts' m68k-amigaos-gcc 16.2 (rc24 or later; the continuation
# of bebbo's amiga-gcc), with libnix (-noixemul), and for the tests a
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
L2OBJ   = $(patsubst $(L2)/libmpeg2/%.c,build/l2/%.o,$(L2SRC)) build/l2/idct_68k.o \
          build/l2/idct_aan_68k.o build/l2/vlc_68k.o
# the 68k's own: the IDCT's columns and the coefficients' decoding written
# by hand (the host builds the C)
L2ASM   = $(L2)/libmpeg2/idct_68k.s $(L2)/libmpeg2/idct_aan_68k.s $(L2)/libmpeg2/vlc_68k.s
AS      = m68k-amigaos-as -m68020
FIX     = tests/fixtures/ps.mpg tests/fixtures/es.m1v tests/fixtures/pal.m2v \
          tests/fixtures/ntsc.m2v tests/fixtures/dvd.vob

.PHONY: all test psnr halfpsnr prof lha clean

all: $(LIB) build/mpvplay

# Its Resident's version from src/mpegv.c's LibVersion, so the two cannot
# differ: ramlib turns a library away whose Resident is older than the
# version asked for, whatever its lib_Version (make test checks it)
LIBVER = $(shell sed -n 's/^const UWORD LibVersion = \([0-9]*\);.*/\1/p' src/mpegv.c)
build/libinit.o: src/libinit.c src/libinit.h src/mpegv.c Makefile
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) -Wall -Isrc -DLIB_RT_VERSION=$(LIBVER) -c -o $@ $<

build/%.o: src/%.s
	@mkdir -p build
	@echo "  AS    $<"
	@$(AS) -o $@ $<

build/%.o: src/%.c $(HDR)
	@mkdir -p build
	@echo "  CC    $<"
	@$(LIBCC) $(WARN) $(INC) -c -o $@ $<

build/l2/%.o: $(L2)/libmpeg2/%.s
	@mkdir -p build/l2
	@echo "  AS    $<"
	@$(AS) -o $@ $<

build/l2/%.o: $(L2)/libmpeg2/%.c $(HDR)
	@mkdir -p build/l2
	@echo "  CC    $<"
	@$(LIBCC) -Wall -Wno-unused $(INC) -c -o $@ $<

$(LIB): build/libinit.o build/mpegv.o build/convert.o build/convert_68k.o $(L2OBJ)
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
build/host/mpv_exact: tests/mpv_test.c $(SRC) $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -DMPEGV_EXACT_IDCT -Itests/host $(INC) -o $@ tests/mpv_test.c $(SRC)
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
PROFOBJ = $(patsubst %.c,build/68k/obj/%.o,$(notdir tests/mpv_test.c $(SRC))) build/l2/idct_68k.o build/l2/idct_aan_68k.o build/l2/vlc_68k.o \
          build/convert_68k.o
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

# the IDCT and the motion compensation against libmpeg2's C, block by
# block (and the IDCT's IEEE 1180 accuracy), on both
build/host/%_test: tests/%_test.c $(HDR) $(SRC)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w $(INC) -o $@ $< -lm
# MpvToRGB and MpvToRGBScaled, every format and size, against a pixel at a
# time (src/convert.c built in; on the 68k with its YUV and grey loops)
build/host/rgb_test: tests/rgb_test.c src/convert.c $(HDR)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -Itests/host $(INC) -o $@ $<
build/68k/rgb_test: tests/rgb_test.c src/convert.c $(HDR) build/convert_68k.o
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w $(INC) -o $@ $< build/convert_68k.o
build/host/idct_exact: tests/idct_test.c $(HDR) $(SRC)
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w -DMPEGV_EXACT_IDCT $(INC) -o $@ $< -lm
build/68k/idct_test: tests/idct_test.c $(HDR) $(SRC) build/l2/idct_68k.o build/l2/idct_aan_68k.o
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w -DNO_IEEE $(INC) -o $@ $< build/l2/idct_68k.o build/l2/idct_aan_68k.o
build/68k/mc_test: tests/mc_test.c $(HDR) $(SRC)
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w $(INC) -o $@ $<
# the blocks' coefficients as vlc_68k.s decodes them against slice.c's C
# (the rest of libmpeg2 linked in, as slice.c needs it)
VLCOBJ  = $(filter-out build/l2/slice.o,$(L2OBJ))
build/68k/vlc_test: tests/vlc_test.c $(HDR) $(L2)/libmpeg2/slice.c $(VLCOBJ)
	@mkdir -p build/68k
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -fomit-frame-pointer -w $(INC) -o $@ $< $(VLCOBJ)

test: build/host/mpv_test build/host/mpv_exact build/host/mpv_plain build/68k/mpv_test $(FIX) build/host/idct_test \
		build/host/idct_exact build/host/mc_test build/68k/idct_test build/68k/mc_test build/68k/vlc_test $(LIB) \
		build/host/rgb_test build/68k/rgb_test
	@python3 -c "import sys; d=open('$(LIB)','rb').read(); i=d.find(bytes([0x4a,0xfc])); \
		sys.exit(0 if i >= 0 and d[i+11] == $(LIBVER) else 'its Resident says version %d, not $(LIBVER)' % d[i+11])" \
		&& echo "  CHECK the library's Resident: version $(LIBVER), as LibVersion (ramlib turns an older one away)"
	@build/host/idct_test > build/host/idct.txt && build/host/mc_test >> build/host/idct.txt \
		&& build/host/idct_exact 20000 | sed 's/^/(2.0, MPEGV_EXACT_IDCT) /' >> build/host/idct.txt \
		|| { cat build/host/idct.txt; exit 1; }; sed 's/^/  /' build/host/idct.txt
	@cd build/68k && { vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:idct_test 20000 2>/dev/null \
		&& vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:mc_test 300 \
		&& vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:vlc_test 20000; } 2>/dev/null \
		> idct.txt || { cat idct.txt; exit 1; }; sed 's/^/  (68020) /' idct.txt
	@build/host/rgb_test > build/host/rgb.txt || { cat build/host/rgb.txt; exit 1; }; sed 's/^/  /' build/host/rgb.txt
	@cd build/68k && vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:rgb_test > rgb.txt 2>/dev/null \
		|| { cat rgb.txt; exit 1; }; sed 's/^/  (68020) /' rgb.txt
	@for f in $(FIX); do \
		build/host/mpv_plain $$f > build/host/ref.txt; \
		build/host/mpv_exact $$f > build/host/lib.txt; \
		cmp -s build/host/ref.txt build/host/lib.txt || { echo $$f; diff build/host/ref.txt build/host/lib.txt | head; exit 1; }; \
	done; echo "  CHECK the pictures as libmpeg2's own C decodes them, with 2.0's IDCT (MPEGV_EXACT_IDCT, host) ok"
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
	@for f in $(FIX); do build/host/mpv_test -half $$f; build/host/mpv_test -switch 2 $$f; done > build/host/half.txt; \
		cmp -s build/host/half.txt tests/expected_half.txt \
		&& echo "  CHECK half size (MPVA_Half, MPVC_HALF every 2 pictures) as expected (host) ok" \
		|| { diff build/host/half.txt tests/expected_half.txt | head; exit 1; }
	@cd build/68k && for f in $(notdir $(FIX)); do \
		vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:mpv_test -half prog:$$f; \
		vamos -C 68020 -m 16384 -H disable -V prog:$(abspath build/68k) -- prog:mpv_test -switch 2 prog:$$f; done \
		> half.txt 2> err.txt; \
		cmp -s half.txt ../../tests/expected_half.txt \
		&& echo "  CHECK the same through mpegv.library on a 68020 (vamos) ok" \
		|| { diff half.txt ../../tests/expected_half.txt | head; cat err.txt; exit 1; }

# each fixture's pictures against ffmpeg's: PSNR (dB) of Y, Cb, Cr, the
# worst picture's and the mean
psnr: build/host/mpv_test $(FIX)
	@for f in $(FIX); do python3 tests/psnr.py build/host/mpv_test $$f; done

# 2.6: half size against the whole picture averaged 2x2 (MPG=file for
# another stream; N pictures at most)
halfpsnr: build/host/mpv_test $(FIX)
	@for f in $(if $(MPG),$(MPG),$(FIX)); do python3 tests/halfpsnr.py build/host/mpv_test $$f $(N); done

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
# example, built; no source: it is on GitHub (README.md, mpegv.readme;
# GPL-3 section 6(d))
lha: all
	@rm -rf build/lha && mkdir -p build/lha/mpegv/Libs build/lha/mpegv/Include build/lha/mpegv/Example
	@cp $(LIB) build/lha/mpegv/Libs/
	@cp -R include/* build/lha/mpegv/Include/
	@cp mpegv.doc mpegv_lib.sfd README.md LICENSE CHANGES build/lha/mpegv/
	@cp build/mpvplay build/lha/mpegv/Example/
	@cp mpegv.readme build/lha/mpegv.readme
	@cd build/lha && rm -f ../mpegv.lha && lha aqo5 ../mpegv.lha mpegv mpegv.readme
	@echo "  LHA   build/mpegv.lha"

clean:
	rm -rf build
