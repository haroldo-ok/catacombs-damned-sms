# ---------------------------------------------------------------------------
# Catacombs of the Damned! - Sega Master System
# ---------------------------------------------------------------------------
DK      ?= $(HOME)/.devkitsms
PROG    := catacombs
SRCDIR  := src
OBJDIR  := build

CC      := sdcc
AS      := sdasz80

# The register allocator's effort.  25000 is worth ~2% of frame time and 1.4K
# of ROM over the default-ish 3000, at the cost of a four minute build, so it
# is the shipped setting; use `make ALLOCS=3000` while iterating.
ALLOCS  ?= 25000

CFLAGS  := -mz80 --std-sdcc99 -I$(DK)/include \
           --peep-file $(DK)/include/peep-rules.txt \
           --max-allocs-per-node $(ALLOCS) --opt-code-speed
LDFLAGS := -mz80 --no-std-crt0 --data-loc 0xC010 -Wl-b_BANK2=0x8000 -Wl-u

# Modules parked in the permanently-mapped third 16K bank at 0x8000.  The ROM
# never switches banks, so slot 2 is simply a flat extension of the address
# space and no trampolines are needed - it just keeps the fixed 32K (which has
# to hold the SEGA header at 0x7FF0) from overflowing.
BANKED  := gamedata mapgen menu font

CSRC    := $(wildcard $(SRCDIR)/*.c)
CRELS   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.rel,$(CSRC))
ARELS   := $(OBJDIR)/blit.rel $(OBJDIR)/fastmath.rel $(OBJDIR)/prims.rel $(OBJDIR)/hot.rel
RELS    := $(OBJDIR)/main.rel $(filter-out $(OBJDIR)/main.rel,$(CRELS)) $(ARELS)

.PHONY: all clean playtest run check asm profile

all: $(PROG).sms

$(OBJDIR):
	@mkdir -p $(OBJDIR)

build/crt0_sms.rel: $(DK)/lib/crt0_sms.rel | $(OBJDIR)
	@cp $< $@ && touch build/crt0_sms.lst

$(SRCDIR)/blit.s: tools/genblit.py
	python3 tools/genblit.py $@

$(OBJDIR)/blit.rel: $(SRCDIR)/blit.s | $(OBJDIR)
	$(AS) -plosgff $@ $<

$(OBJDIR)/fastmath.rel: $(SRCDIR)/fastmath.s | $(OBJDIR)
	$(AS) -plosgff $@ $<

$(OBJDIR)/prims.rel: $(SRCDIR)/prims.s | $(OBJDIR)
	$(AS) -plosgff $@ $<

$(OBJDIR)/hot.rel: $(SRCDIR)/hot.s | $(OBJDIR)
	$(AS) -plosgff $@ $<

BANKFLAGS = $(if $(filter $(BANKED),$(basename $(notdir $<))),--codeseg BANK2 --constseg BANK2,)

$(OBJDIR)/%.rel: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) -c $(CFLAGS) $(BANKFLAGS) $< -o $@

# The bundled SMSlib.lib records a stale "-mz80" option string even though it
# is built for the modern __sdcccall(1) ABI (verified by running a call through
# smstest), so ASlink emits a spurious "conflicting sdcc options" warning and
# sdcc exits non-zero.  Filter that one out and fail on anything real.
.PHONY: $(PROG).ihx
$(PROG).ihx: $(RELS) build/crt0_sms.rel
	@set -o pipefail; $(CC) -o $@ $(LDFLAGS) build/crt0_sms.rel $(RELS) \
	      $(DK)/lib/SMSlib.lib $(DK)/lib/PSGlib.lib 2>&1 | tee build/link.log; true
	@if grep -q "Undefined Global" build/link.log; then \
	    echo "LINK FAILED: undefined symbols"; exit 1; fi
	@if grep -qi "error" build/link.log; then echo "LINK FAILED"; exit 1; fi
	@grep -c "Conflicting sdcc options" build/link.log >/dev/null && \
	    echo "(ignored $$(grep -c 'Conflicting sdcc options' build/link.log) stale-ABI-metadata warnings from SMSlib.lib)" || true
	@test -s $@
	@# src/hot.s forms &wBuffer[xi] and &horizonBuffer[xi] from a page byte,
	@# so the two buffers must stay page aligned and 0x80 apart.
	@python3 -c "import sys;\
a={l.split()[1]:int(l.split()[2],16) for l in open('$(PROG).noi') if l.startswith('DEF')};\
w,h=a['_wBuffer'],a['_horizonBuffer'];\
sys.exit('LAYOUT FAILED: wBuffer=%04X horizonBuffer=%04X' % (w,h)) if (w%256 or h-w!=128) else print('layout ok: wBuffer=%04X horizonBuffer=%04X' % (w,h))"

$(PROG).sms: $(PROG).ihx
	$(DK)/bin/ihx2sms $< $@

# Re-emit every translation unit as assembly and scan it for the SDCC 4.5.0
# "destructive compare" miscompile (see tools/checkasm.py).
asm: $(SRCDIR)/blit.s
	@mkdir -p build/asm
	@for f in $(CSRC); do b=$$(basename $$f .c); \
	  bf=""; case " $(BANKED) " in *" $$b "*) bf="--codeseg BANK2 --constseg BANK2";; esac; \
	  $(CC) -S $(CFLAGS) $$bf $$f -o build/asm/$$b.asm 2>/dev/null; done

check: asm
	@python3 tools/checkasm.py build/asm/*.asm

playtest: $(PROG).sms
	@mkdir -p shots
	$(DK)/bin/smstest $(PROG).sms tests/playtest.txt

profile: $(PROG).sms
	$(DK)/bin/smstest $(PROG).sms tests/prof.txt
	python3 $(DK)/bin/prof2.py build/cg.out $(PROG).noi build/*.rst --top 20

clean:
	rm -rf $(OBJDIR) $(PROG).sms $(PROG).ihx $(PROG).map $(PROG).noi \
	       $(PROG).lk $(PROG).lst $(PROG).sym $(PROG).rst shots/*.ppm
