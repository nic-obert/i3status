# Maintainer: Levente Polyak <anthraxx@archlinux.org>
# Maintainer: Daniel M. Capella <polyzen@archlinux.org>
# Maintainer: T.J. Townsend <blakkheim@archlinux.org>
# Contributor: Jelle van der Waa <jelle@vdwaa.nl>
# Contributor: Thorsten Töpper <atsutane-tu@freethoughts.de>
# Contributor: Ingo Bürk <ingo.buerk@airblader.de>

pkgname=i3status
pkgver=3.5
pkgrel=1
pkgdesc='Generates status bar to use with i3bar'
arch=('x86_64')
url='https://i3wm.org/i3status/'
license=('BSD')
groups=('i3')
depends=('alsa-lib' 'libnl' 'libpulse' 'yajl')
makedepends=('asciidoc' 'git' 'meson' 'xmlto')
backup=('etc/i3status.conf')
options=('docs')

build() {
  cd $startdir/i3status/meson
  arch-meson \
    -Dmans=true \
    -Dc_args='-march=native -O2' \
    .. build
  meson compile -C build
}

package() {
  cd $startdir/i3status/meson
  meson install -C build --destdir "$pkgdir"
  install -Dm644 -t "$pkgdir"/usr/share/licenses/$pkgname ../LICENSE
}
