pkgname=php-blackhole
pkgver='1.0.0'
pkgrel=1
pkgdesc="PHP extension for sending request duration to the StatsD collector"
url="http://github.com/zarplata/php-blackhole-ext"
arch=('x86_64' 'i686')
license=('MIT')
depends=('php')
makedepends=('gcc' 'autoconf')
source=("https://github.com/zarplata/php-blackhole-ext/archive/${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
    cd "${srcdir}/php-blackhole-ext-${pkgver}"
    phpize
    ./configure --prefix=/usr
    make
}

package() {
    cd "${srcdir}/php-blackhole-ext-${pkgver}"

    make INSTALL_ROOT="${pkgdir}" install
    echo 'extension=blackhole.so' > blackhole.ini
    install -Dm644 blackhole.ini "${pkgdir}/etc/php/conf.d/blackhole.ini"
}
