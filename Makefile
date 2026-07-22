include config.mk

PKGS = wayland-server xkbcommon libinput xcb xcb-icccm

CPPFLAGS += -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	        -DVERSION=\"$(VERSION)\" \
	        `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS)
CFLAGS   := -fvisibility=hidden -std=c23 -Os \
	        -Wall -Wextra -Wpedantic \
	        -Wformat=2 -Wformat-security \
	        -Wnull-dereference -Wno-format-nonliteral \
	        -Wunused -Wuninitialized -Wmissing-field-initializers \
	        -Wdeclaration-after-statement -Wno-unused-parameter \
	        -Wshadow -Wunused-macros \
	        -Werror=strict-prototypes -Werror=implicit -Werror=return-type \
	        -Werror=incompatible-pointer-types -Wfloat-conversion \
	        -fno-common -fstack-protector-all -mcmodel=medium
LDFLAGS  := -fuse-ld=lld
LDLIBS   := `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

# swmctl
CTL_PKGS      = wayland-client
CTL_CPPFLAGS := `$(PKG_CONFIG) --cflags $(CTL_PKGS)`
CTL_LDLIBS   := `$(PKG_CONFIG) --libs $(CTL_PKGS)`

SRC   := swm.c util.c ext-workspace-v1-protocol.c swm-workspace-v1-protocol.c
OBJ   := $(SRC:.c=.o)
PROTO := \
	cursor-shape-v1-protocol.h \
	ext-workspace-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h \
	wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h \
	swm-workspace-v1-protocol.h \
	xdg-shell-protocol.h
HDRS  := config.h swm.h util.h $(PROTO)

WL_SCANNER := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
WL_PROTO   := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)

TEST_BUILD    ?= test/.build
TEST_SRC      := $(TEST_BUILD)/source
TEST_BIN      := $(TEST_BUILD)/bin
TEST_CPPFLAGS := -I$(TEST_SRC) $(CPPFLAGS)
TEST_CFLAGS   := -std=c23 -g -O0 \
	-Wno-unused-function -Wno-unused-variable -Wno-unused-parameter

ifeq ($(TEST_COVERAGE),1)
TEST_CFLAGS += -fprofile-instr-generate -fcoverage-mapping
endif

all: man swm swmctl

man: swm.1 swmctl.1

swm.1: swm.1.adoc
	@echo "man  $< => $@"
	@asciidoctor -b manpage -o $@ $<

swmctl.1: swmctl.1.adoc
	@echo "man  $< => $@"
	@asciidoctor -b manpage -o $@ $<

swm: $(OBJ)
	@echo "ld   $^ => $@"
	@$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@echo "ok   $@"

swmctl: swmctl.o swm-workspace-v1-protocol.o
	@echo "ld   $^ => $@"
	@$(CC) $(LDFLAGS) $^ $(CTL_LDLIBS) -o $@
	@echo "ok   $@"

swmctl.o: swmctl.c swm-workspace-v1-client-protocol.h Makefile config.mk
	@echo "cc   $< => $@"
	@$(CC) $(CTL_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ): Makefile config.mk

%.o: %.c $(HDRS)
	@echo "cc   $< => $@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

cursor-shape-v1-protocol.h: \
	$(WL_PROTO)/staging/cursor-shape/cursor-shape-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) enum-header $< $@
ext-workspace-v1-protocol.h: \
	$(WL_PROTO)/staging/ext-workspace/ext-workspace-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) server-header $< $@
ext-workspace-v1-protocol.c: \
	$(WL_PROTO)/staging/ext-workspace/ext-workspace-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) private-code $< $@
swm-workspace-v1-protocol.h: protocols/swm-workspace-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) server-header $< $@
swm-workspace-v1-client-protocol.h: protocols/swm-workspace-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) client-header $< $@
swm-workspace-v1-protocol.c: protocols/swm-workspace-v1.xml
	@echo "src  $@"
	@$(WL_SCANNER) private-code $< $@
pointer-constraints-unstable-v1-protocol.h: \
	$(WL_PROTO)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) enum-header $< $@
wlr-layer-shell-unstable-v1-protocol.h: \
	protocols/wlr-layer-shell-unstable-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) enum-header $< $@
wlr-output-power-management-unstable-v1-protocol.h: \
	protocols/wlr-output-power-management-unstable-v1.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) server-header $< $@
xdg-shell-protocol.h: \
	$(WL_PROTO)/stable/xdg-shell/xdg-shell.xml
	@echo "hdr  $@"
	@$(WL_SCANNER) server-header $< $@

config.h:
	@test -e $@ || cp config.def.h $@

fmt:
	git ls-files "*.c" "*.h" | xargs clang-format -i

install: swm swmctl swm.1 swmctl.1
	install -Dm755 swm $(DESTDIR)$(PREFIX)/bin/swm
	install -Dm755 swmctl $(DESTDIR)$(PREFIX)/bin/swmctl
	install -Dm644 swm.1 $(DESTDIR)$(MANDIR)/man1/swm.1
	install -Dm644 swmctl.1 $(DESTDIR)$(MANDIR)/man1/swmctl.1
	@test -e $(DESTDIR)$(DATADIR)/wayland-sessions/swm.desktop || \
		install -Dm644 swm.desktop \
			$(DESTDIR)$(DATADIR)/wayland-sessions/swm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/swm
	rm -f $(DESTDIR)$(PREFIX)/bin/swmctl
	rm -f $(DESTDIR)$(MANDIR)/man1/swm.1
	rm -f $(DESTDIR)$(MANDIR)/man1/swmctl.1
	rm -f $(DESTDIR)$(DATADIR)/wayland-sessions/swm.desktop

archive:
	git archive --format=tar.gz --prefix=swm/ -o swm.tar.gz HEAD

package: PKGBUILD
	makepkg --force

clean:
	rm -f swm swmctl swm.1 swmctl.1 *.o *-protocol.h *-protocol.c

$(TEST_SRC) $(TEST_BIN):
	@mkdir -p $@

$(TEST_SRC)/swm.c: swm.c | $(TEST_SRC)
	@cp -p $< $@

$(TEST_SRC)/config.h: config.def.h | $(TEST_SRC)
	@cp -p $< $@

TEST_COMMON := Makefile config.mk $(TEST_SRC)/swm.c $(TEST_SRC)/config.h \
	swm.h util.c util.h $(PROTO) ext-workspace-v1-protocol.c swm-workspace-v1-protocol.c

$(TEST_BIN)/unit: test/unit.c $(TEST_COMMON) | $(TEST_BIN)
	@echo "cc   test/unit.c => $@"
	@$(CC) $(TEST_CPPFLAGS) $(TEST_CFLAGS) test/unit.c util.c \
		ext-workspace-v1-protocol.c swm-workspace-v1-protocol.c $(LDLIBS) -o $@

$(TEST_BIN)/swm: $(TEST_COMMON) | $(TEST_BIN)
	@echo "cc   swm.c => $@"
	@$(CC) $(TEST_CPPFLAGS) $(TEST_CFLAGS) $(TEST_SRC)/swm.c util.c \
		ext-workspace-v1-protocol.c swm-workspace-v1-protocol.c $(LDLIBS) -o $@

$(TEST_BIN)/swmctl: swmctl.c swm-workspace-v1-client-protocol.h \
		swm-workspace-v1-protocol.c | $(TEST_BIN)
	@echo "cc   swmctl.c => $@"
	@$(CC) $(CTL_CPPFLAGS) $(TEST_CFLAGS) swmctl.c \
		swm-workspace-v1-protocol.c $(CTL_LDLIBS) -o $@

test-build: $(TEST_BIN)/unit $(TEST_BIN)/swm $(TEST_BIN)/swmctl

test: test-build
	@SWM_TEST_BUILD=$(abspath $(TEST_BUILD)) PKG_CONFIG=$(PKG_CONFIG) $(PYTHON) test/run.py

test-unit: $(TEST_BIN)/unit
	@SWM_TEST_BUILD=$(abspath $(TEST_BUILD)) PKG_CONFIG=$(PKG_CONFIG) $(PYTHON) test/run.py unit

test-integration: $(TEST_BIN)/swm $(TEST_BIN)/swmctl
	@SWM_TEST_BUILD=$(abspath $(TEST_BUILD)) PKG_CONFIG=$(PKG_CONFIG) $(PYTHON) test/run.py integration

coverage:
	@$(MAKE) --no-print-directory TEST_BUILD=$(TEST_BUILD)/coverage TEST_COVERAGE=1 test-build
	@SWM_TEST_BUILD=$(abspath $(TEST_BUILD)/coverage) PKG_CONFIG=$(PKG_CONFIG) \
		$(PYTHON) test/run.py coverage

test-clean:
	@SWM_TEST_BUILD=$(abspath $(TEST_BUILD)) $(PYTHON) test/run.py clean

.PHONY: all man fmt install uninstall archive package clean coverage
.PHONY: test-build test test-unit test-integration test-clean
