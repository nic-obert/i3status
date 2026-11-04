# Maintainer: Levente Polyak <anthraxx@archlinux.org>
# Maintainer: Daniel M. Capella <polyzen@archlinux.org>
# Maintainer: T.J. Townsend <blakkheim@archlinux.org>
# Contributor: Jelle van der Waa <jelle@vdwaa.nl>
# Contributor: Thorsten Töpper <atsutane-tu@freethoughts.de>
# Contributor: Ingo Bürk <ingo.buerk@airblader.de>

pkgname=i3status
pkgver=3.0
pkgrel=1
pkgdesc='Generates status bar to use with i3bar'
arch=('x86_64')
url='https://i3wm.org/i3status/'
license=('BSD')
groups=('i3')
depends=('alsa-lib' 'confuse' 'libnl' 'libpulse' 'yajl')
makedepends=('asciidoc' 'git' 'meson' 'xmlto')
backup=('etc/i3status.conf')
options=('docs')

build() {
  cd $pkgname/meson
  arch-meson -Dmans=true .. build
  meson compile -C build
}

package() {
  cd $pkgname/meson
  meson install -C build --destdir "$pkgdir"
  install -Dm644 -t "$pkgdir"/usr/share/licenses/$pkgname ../LICENSE
}
