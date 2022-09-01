dnl config.m4 for extension yrmcds

PHP_ARG_ENABLE(yrmcds, whether to enable yrmcds support,
[  --enable-yrmcds         Enable yrmcds support])

if test ! -z "$phpincludedir"; then
    PHP_MAJOR_VERSION=$(awk '/PHP_MAJOR_VERSION/ {print $3}' $phpincludedir/main/php_version.h)
elif test ! -z "$PHP_CONFIG"; then
    PHP_MAJOR_VERSION=$($PHP_CONFIG --version | sed -r 's/^([0-9]+).*$/\1/')
fi

extra_sources="libyrmcds/close.c libyrmcds/connect.c libyrmcds/recv.c \
               libyrmcds/send.c libyrmcds/set_compression.c \
               libyrmcds/socket.c libyrmcds/strerror.c \
               libyrmcds/lz4/lib/lz4.c"

if test "$PHP_YRMCDS" != "no"; then
PHP_YRMCDS_CFLAGS="-D_GNU_SOURCE -DLIBYRMCDS_USE_LZ4 -I@ext_srcdir@/libyrmcds/lz4/lib"
    PHP_SUBST(YRMCDS_SHARED_LIBADD)
    if test "$PHP_MAJOR_VERSION" -lt 7; then
        PHP_NEW_EXTENSION(yrmcds, yrmcds_php5.c $extra_sources, $ext_shared,,
                          $PHP_YRMCDS_CFLAGS)
    else
        PHP_NEW_EXTENSION(yrmcds, yrmcds.c $extra_sources, $ext_shared,,
                          $PHP_YRMCDS_CFLAGS)
    fi
    PHP_ADD_INCLUDE([$ext_srcdir/libyrmcds])
fi
