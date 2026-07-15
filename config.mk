VERSION    = 1.0.0
PKG_CONFIG = pkg-config
CC         = clang
PREFIX     = /usr/local
MANDIR     = $(PREFIX)/share/man
DATADIR    = $(PREFIX)/share
WLR_INCS   = `$(PKG_CONFIG) --cflags wlroots-0.20`
WLR_LIBS   = `$(PKG_CONFIG) --libs wlroots-0.20`
