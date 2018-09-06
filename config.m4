dnl config.m4 for extension blackhole

PHP_ARG_ENABLE(blackhole, whether to enable blackhole support,
[  --enable-blackhole           Enable blackhole support])

if test "$PHP_BLACKHOLE" != "no"; then
  AC_DEFINE(HAVE_BLACKHOLE, 1, [ Have blackhole support ])

  PHP_NEW_EXTENSION(blackhole, blackhole.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi