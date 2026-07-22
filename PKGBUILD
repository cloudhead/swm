# Maintainer: Alexis Sellier <self@cloudhead.io>

pkgname=swm
pkgver=1.0.0
pkgrel=1
pkgdesc='Simple window manager for Wayland'
arch=('x86_64')
url='https://github.com/cloudhead/swm'
license=('GPL-3.0-only')
options=('!debug')
backup=('usr/share/wayland-sessions/swm.desktop')
depends=(
  'glibc'
  'libinput'
  'libxcb'
  'libxkbcommon'
  'wayland'
  'wlroots0.20'
  'xcb-util-wm'
)
optdepends=('xorg-xwayland: X11 application support')
makedepends=(
  'asciidoctor'
  'clang'
  'lld'
  'wayland-protocols'
)
source=()
b2sums=()

prepare() {
  cp "${startdir}"/{Makefile,config.mk,swm.c,swm.h,swmctl.c,util.c,util.h} \
    "${srcdir}/"
  cp "${startdir}"/{config.def.h,swm.1.adoc,swmctl.1.adoc,swm.desktop} \
    "${srcdir}/"
  install -d "${srcdir}/protocols"
  cp "${startdir}"/protocols/*.xml "${srcdir}/protocols/"
}

build() {
  cd "${srcdir}"
  make
}

package() {
  cd "${srcdir}"
  make DESTDIR="${pkgdir}" PREFIX=/usr install
}
