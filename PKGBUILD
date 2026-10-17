# Maintainer: Nikita Bushuev <enxyox@gmail.com>
#
# Installs the daemon, sony-ctl and the systemd user unit. The widget is
# installed with `omarchy plugin add`. pkgver must match "version" in
# manifest.json: the widget compares the two.
pkgname=omarchy-sony-xm5
pkgver=0.1.0
pkgrel=1
pkgdesc="Daemon and CLI for the Sony WH-1000XM5 and WF-1000XM5, for the Omarchy bar widget"
arch=('x86_64' 'aarch64')
url="https://github.com/enxy0/omarchy-sony-xm5"
license=('MIT')
depends=('bluez' 'bluez-libs' 'dbus' 'gcc-libs' 'glibc')
makedepends=('cmake' 'ninja' 'git')
install=omarchy-sony-xm5.install
source=("$pkgname::git+$url.git#tag=v$pkgver")
sha256sums=('SKIP')

build() {
  cmake -S "$pkgname" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
  # The unit in the repository runs the daemon from ~/.local/bin, where
  # setup installs it.
  sed -i 's|^ExecStart=.*|ExecStart=/usr/bin/sony-headphones-daemon|' \
    "$pkgdir/usr/lib/systemd/user/sony-headphones.service"
  install -Dm644 "$pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm644 "$pkgname/THIRD_PARTY_NOTICES.md" "$pkgdir/usr/share/licenses/$pkgname/THIRD_PARTY_NOTICES.md"
}
