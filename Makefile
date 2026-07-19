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

SRC   := swm.c util.c ext-workspace-v1-protocol.c
OBJ   := $(SRC:.c=.o)
PROTO := \
	cursor-shape-v1-protocol.h \
	ext-workspace-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h \
	wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h \
	xdg-shell-protocol.h
HDRS  := config.h swm.h util.h $(PROTO)

WL_SCANNER := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
WL_PROTO   := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)

all: man swm

man: swm.1

swm.1: swm.1.adoc
	@echo "man  $< => $@"
	@asciidoctor -b manpage -o $@ $<

swm: $(OBJ)
	@echo "ld   $^ => $@"
	@$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@echo "ok   $@"

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

formal:
	@for source in formal/*.agda; do \
		echo "agda $$source"; \
		agda --safe --no-libraries --ignore-interfaces "$$source" || exit; \
	done

install: swm swm.1
	install -Dm755 swm $(DESTDIR)$(PREFIX)/bin/swm
	install -Dm644 swm.1 $(DESTDIR)$(MANDIR)/man1/swm.1
	install -Dm644 swm.desktop \
		$(DESTDIR)$(DATADIR)/wayland-sessions/swm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/swm
	rm -f $(DESTDIR)$(MANDIR)/man1/swm.1
	rm -f $(DESTDIR)$(DATADIR)/wayland-sessions/swm.desktop

archive:
	git archive --format=tar.gz --prefix=swm/ -o swm.tar.gz HEAD

package: PKGBUILD
	makepkg --force

clean:
	rm -f swm swm.1 *.o *-protocol.h *-protocol.c formal/*.agdai

.PHONY: all man fmt formal install uninstall archive package clean
