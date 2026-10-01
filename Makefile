ifneq (,$(wildcard .env))
include .env
endif

SQLITE_VERSION := 3.8.11.1
SQLITE_SRC ?= vendor/sqlite/sqlite3.c
SQLITE_HEADER ?= vendor/sqlite/sqlite3.h
SQLITE_INCLUDE_DIR ?= $(dir $(SQLITE_HEADER))
NAMES_HEADER ?= include/sqlite3_mvs_names.h
BUILDDIR ?= build
LOAD_MODULE ?= SQLTTEST
TSO_MODULE ?= SQLITSO
SDK_ROOT ?= $(abspath build/sdk)
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

SQLITE_MVS_CPPFLAGS := -include include/sqlite3_mvs_compat.h \
	-include $(NAMES_HEADER)

.PHONY: probe probe-c probe-asm probe-link tso cobol-api cobol-api-x cobol-bridge names sdk as370-large clean mbt-build upgrade-probe-c upgrade-probe upgrade-matrix

UPGRADE_VERSION ?= 3.15.2

upgrade-probe-c:
	tools/probe_sqlite_upgrade.sh $(UPGRADE_VERSION) c

upgrade-probe:
	tools/probe_sqlite_upgrade.sh $(UPGRADE_VERSION) full

upgrade-matrix:
	@for version in 3.15.2 3.22.0 3.31.1 3.35.5 3.37.2 \
		3.40.1 3.45.3 3.49.2 3.53.4; do \
		tools/probe_sqlite_upgrade.sh $$version full || exit $$?; \
	done

sdk:
	$(MAKE) -C toolchain/cc370 PREFIX=$(SDK_ROOT) install
	PATH=$(SDK_ROOT)/bin:$(PATH) $(MAKE) -C toolchain/libc370 install

# Phase 1 deliberately stops before sqlite3_mvs.c: prove what the toolchain
# can consume and preserve complete diagnostics for comparison.
probe: probe-c probe-asm probe-link

names: $(NAMES_HEADER)

$(NAMES_HEADER): $(SQLITE_HEADER) tools/gen_mvs_names.py
	python3 tools/gen_mvs_names.py --header $(SQLITE_HEADER) --output $@

probe-c: $(BUILDDIR)/sqlite3.mvs.s

$(BUILDDIR)/sqlite3.raw.s: $(SQLITE_SRC) $(NAMES_HEADER) include/sqlite3_mvs_compat.h
	@mkdir -p $(BUILDDIR)
	@set -o pipefail; $(CC370) -std=gnu89 -O1 $(SQLITE_MVS_CPPFLAGS) \
		-S $< -o $@ \
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

probe-asm: $(BUILDDIR)/sqlite3.o

$(BUILDDIR)/sqlite3.o: $(BUILDDIR)/sqlite3.mvs.s $(AS370_LARGE)
	@set -o pipefail; $(AS370_LARGE) -I $(shell dirname $$(command -v $(AS370)))/../cc370/macros \
		-o $@ $< \
		2>&1 | tee $(BUILDDIR)/as370.log

$(BUILDDIR)/sqlite3_mvs.o: src/sqlite3_mvs.c $(NAMES_HEADER)
	$(CC370) -std=gnu89 -O1 -Iinclude -I$(SQLITE_INCLUDE_DIR) $(SQLITE_MVS_CPPFLAGS) -c $< -o $@

$(BUILDDIR)/core_link.o: tests/core_link.c $(NAMES_HEADER)
	$(CC370) -std=gnu89 -O1 -Iinclude -I$(SQLITE_INCLUDE_DIR) $(SQLITE_MVS_CPPFLAGS) -c $< -o $@

probe-link: $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite3_mvs.o $(BUILDDIR)/core_link.o
	$(LD370) -L$(shell dirname $$(command -v $(CC370)))/../cc370/lib \
		--name $(LOAD_MODULE) -e @@CRT0 \
		$(shell dirname $$(command -v $(CC370)))/../cc370/lib/crt1.o \
		$^ -lc -iebcopy -o $(BUILDDIR)/$(LOAD_MODULE)

$(BUILDDIR)/sqlite_tso.o: src/sqlite_tso.c $(NAMES_HEADER)
	$(CC370) -std=gnu89 -O1 -Iinclude -I$(SQLITE_INCLUDE_DIR) $(SQLITE_MVS_CPPFLAGS) -c $< -o $@

$(BUILDDIR)/tsqtget.o: asm/tsqtget.asm
	@mkdir -p $(BUILDDIR)
	$(AS370) -o $@ $<

$(BUILDDIR)/tsqtput.o: asm/tsqtput.asm
	@mkdir -p $(BUILDDIR)
	$(AS370) -o $@ $<

tso: $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite3_mvs.o \
		$(BUILDDIR)/sqlite_tso.o $(BUILDDIR)/tsqtget.o $(BUILDDIR)/tsqtput.o
	$(LD370) -L$(shell dirname $$(command -v $(CC370)))/../cc370/lib \
		--name $(TSO_MODULE) -e @@CRT0 \
		$(shell dirname $$(command -v $(CC370)))/../cc370/lib/crt0.o \
		$^ -lc -iebcopy -o $(BUILDDIR)/$(TSO_MODULE)

$(BUILDDIR)/sqlite_cobol_start.o: src/sqlite_cobol_start.c include/sqlite_cobol.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

$(BUILDDIR)/sqlite_cobol_api.o: src/sqlite_cobol_api.c include/sqlite_cobol.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

cobol-api: $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite3_mvs.o \
		$(BUILDDIR)/sqlite_cobol_start.o $(BUILDDIR)/sqlite_cobol_api.o
	$(LD370) -L$(shell dirname $$(command -v $(CC370)))/../cc370/lib \
		--name SQLITEA -e @@CRT0 \
		$(shell dirname $$(command -v $(CC370)))/../cc370/lib/crt1.o \
		$^ -lc -iebcopy -o $(BUILDDIR)/SQLITEA

$(BUILDDIR)/sqlite_cobol_x_start.o: src/sqlite_cobol_x_start.c include/sqlite_cobol_x.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

$(BUILDDIR)/sqlite_cobol_x_api.o: src/sqlite_cobol_x_api.c include/sqlite_cobol_x.h
	$(CC370) -std=gnu89 -O1 -Iinclude -Ivendor/sqlite -c $< -o $@

cobol-api-x: $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite3_mvs.o \
		$(BUILDDIR)/sqlite_cobol_x_start.o $(BUILDDIR)/sqlite_cobol_x_api.o
	$(LD370) -L$(shell dirname $$(command -v $(CC370)))/../cc370/lib \
		--name SQLITEX -e @@CRT0 \
		$(shell dirname $$(command -v $(CC370)))/../cc370/lib/crt1.o \
		$^ -lc -iebcopy -o $(BUILDDIR)/SQLITEX

$(BUILDDIR)/sqliteabr.o: asm/sqliteabr.asm
	@mkdir -p $(BUILDDIR)
	$(AS370) -o $@ $<

cobol-bridge: $(BUILDDIR)/sqliteabr.o

# Full MBT integration is intentionally separate from the compiler probe.
# It becomes the normal build once the amalgamation can produce an object.
mbt-build:
	@$(MAKE) -f mbt.mk all

clean:
	@rm -f $(BUILDDIR)/sqlite3.raw.s $(BUILDDIR)/sqlite3.mvs.s $(BUILDDIR)/sqlite3.o \
		$(BUILDDIR)/sqlite3_mvs.o $(BUILDDIR)/core_link.o $(BUILDDIR)/SQLTTEST \
		$(BUILDDIR)/sqlite_tso.o $(BUILDDIR)/tsqtget.o $(BUILDDIR)/tsqtput.o \
		$(BUILDDIR)/SQLITSO \
		$(BUILDDIR)/sqlite_cobol_start.o $(BUILDDIR)/sqlite_cobol_api.o \
		$(BUILDDIR)/sqlite_cobol_x_start.o $(BUILDDIR)/sqlite_cobol_x_api.o \
		$(BUILDDIR)/SQLITEX \
		$(BUILDDIR)/sqliteabr.o $(BUILDDIR)/SQLITEA \
		$(BUILDDIR)/cc370.log $(BUILDDIR)/as370.log
