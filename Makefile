ifneq (,$(wildcard .env))
include .env
endif

SQLITE_VERSION := 3.8.11.1
SQLITE_SRC := vendor/sqlite/sqlite3.c
BUILDDIR := build
SDK_ROOT := $(abspath $(BUILDDIR)/sdk)
ifneq (,$(wildcard $(SDK_ROOT)/bin/cc370))
CC370 ?= $(SDK_ROOT)/bin/cc370
AS370 ?= $(SDK_ROOT)/bin/as370
LD370 ?= $(SDK_ROOT)/bin/ld370
else
CC370 ?= cc370
AS370 ?= as370
LD370 ?= ld370
endif
AS370_LARGE ?= $(BUILDDIR)/tools/as370
CC370_SOURCE ?= toolchain/cc370

SQLITE_MVS_CPPFLAGS := \
	-DSQLITE_OS_OTHER=1 \
	-DSQLITE_THREADSAFE=0 \
	-DSQLITE_OMIT_LOAD_EXTENSION=1 \
	-DSQLITE_OMIT_WAL=1 \
	-DSQLITE_TEMP_STORE=3 \
	-DSQLITE_DEFAULT_PAGE_SIZE=4096 \
	-DSQLITE_MAX_DEFAULT_PAGE_SIZE=4096 \
	-DSQLITE_MAX_MMAP_SIZE=0 \
	-DSQLITE_OMIT_AUTOINIT=1

.PHONY: probe probe-c probe-asm probe-link names sdk as370-large clean mbt-build

sdk:
	$(MAKE) -C toolchain/cc370 PREFIX=$(SDK_ROOT) install
	PATH=$(SDK_ROOT)/bin:$(PATH) $(MAKE) -C toolchain/libc370 install

# Phase 1 deliberately stops before sqlite3_mvs.c: prove what the toolchain
# can consume and preserve complete diagnostics for comparison.
probe: probe-c probe-asm probe-link

names: include/sqlite3_mvs_names.h

include/sqlite3_mvs_names.h: vendor/sqlite/sqlite3.h tools/gen_mvs_names.py
	python3 tools/gen_mvs_names.py

probe-c: $(BUILDDIR)/sqlite3.mvs.s

$(BUILDDIR)/sqlite3.raw.s: $(SQLITE_SRC) include/sqlite3_mvs_names.h
	@mkdir -p $(BUILDDIR)
	@set -o pipefail; $(CC370) -std=gnu89 -O1 $(SQLITE_MVS_CPPFLAGS) \
		-include include/sqlite3_mvs_names.h -S $< -o $@ \
		2>&1 | tee $(BUILDDIR)/cc370.log

$(BUILDDIR)/sqlite3.mvs.s: $(BUILDDIR)/sqlite3.raw.s tools/shorten_cc370_labels.py
	python3 tools/shorten_cc370_labels.py $< $@

as370-large: $(AS370_LARGE)

$(AS370_LARGE): patches/cc370-as370-large-input.patch
	@test -f $(CC370_SOURCE)/as370/src/as370.c || { \
		echo "Set CC370_SOURCE to an mvslovers/cc370 checkout"; exit 2; }
	@mkdir -p $(@D) $(BUILDDIR)/tools/cc370
	@cp -R $(CC370_SOURCE)/as370 $(CC370_SOURCE)/common $(BUILDDIR)/tools/cc370/
	@cp $(CC370_SOURCE)/Makefile $(BUILDDIR)/tools/cc370/Makefile
	@patch -d $(BUILDDIR)/tools/cc370 -p1 < $<
	@$(MAKE) -C $(BUILDDIR)/tools/cc370 as370/as370
	@cp $(BUILDDIR)/tools/cc370/as370/as370 $@

probe-asm: $(BUILDDIR)/sqlite3.mvs.s $(AS370_LARGE)
	@set -o pipefail; $(AS370_LARGE) -I $(shell dirname $$(command -v $(AS370)))/../cc370/macros \
		-o $(BUILDDIR)/sqlite3.o $< \
		2>&1 | tee $(BUILDDIR)/as370.log

$(BUILDDIR)/sqlite3_mvs.o: src/sqlite3_mvs.c include/sqlite3_mvs_names.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

$(BUILDDIR)/core_link.o: tests/core_link.c include/sqlite3_mvs_names.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

probe-link: $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite3_mvs.o $(BUILDDIR)/core_link.o
	$(LD370) -L$(shell dirname $$(command -v $(CC370)))/../cc370/lib \
		--name SQLTTEST -e @@CRT0 \
		$(shell dirname $$(command -v $(CC370)))/../cc370/lib/crt1.o \
		$^ -lc -iebcopy -o $(BUILDDIR)/SQLTTEST

# Full MBT integration is intentionally separate from the compiler probe.
# It becomes the normal build once the amalgamation can produce an object.
mbt-build:
	@$(MAKE) -f mbt.mk all

clean:
	@rm -f $(BUILDDIR)/sqlite3.raw.s $(BUILDDIR)/sqlite3.mvs.s $(BUILDDIR)/sqlite3.o \
		$(BUILDDIR)/sqlite3_mvs.o $(BUILDDIR)/core_link.o $(BUILDDIR)/SQLTTEST \
		$(BUILDDIR)/cc370.log $(BUILDDIR)/as370.log
