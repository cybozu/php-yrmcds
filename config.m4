dnl config.m4 for extension yrmcds

PHP_ARG_ENABLE(yrmcds, whether to enable yrmcds support,
[  --enable-yrmcds         Enable yrmcds support])

extra_sources="libyrmcds/close.c libyrmcds/connect.c libyrmcds/recv.c \
               libyrmcds/send.c libyrmcds/set_compression.c \
               libyrmcds/socket.c libyrmcds/strerror.c lz4/lz4.c"


if test "$PHP_YRMCDS" != "no"; then
    PHP_SUBST(YRMCDS_SHARED_LIBADD)
    PHP_NEW_EXTENSION(yrmcds, yrmcds.c $extra_sources, $ext_shared,,
                      "-D_GNU_SOURCE")
    PHP_ADD_INCLUDE([$ext_srcdir/libyrmcds])
fi
