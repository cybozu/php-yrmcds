// (C) 2013 Cybozu.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_yrmcds.h"
#include "zend_exceptions.h"

#ifdef HAVE_SPL
#include "ext/spl/spl_exceptions.h"
#endif

#include <errno.h>

ZEND_DECLARE_MODULE_GLOBALS(yrmcds)

/* Macro functions */

#define HASH_KEY(key, node)                             \
    spprintf(&(key), 0, PHP_YRMCDS_HASH_KEY, (node))
#define CHECK_YRMCDS(e)                                                 \
    do {                                                                \
        int __e = (e);                                                  \
        if( __e != 0 ) {                                                \
            if( __e == YRMCDS_SYSTEM_ERROR ) {                          \
                zend_throw_exception_ex(ce_yrmcds_error, __e TSRMLS_CC, \
                                        (char*)sys_errlist[errno]);     \
            } else {                                                    \
                zend_throw_exception_ex(ce_yrmcds_error, __e TSRMLS_CC, \
                                        (char*)yrmcds_strerror(__e));   \
            }                                                           \
            RETURN_FALSE;                                               \
        }                                                               \
    } while( 0 )
#define CHECK_YRMCDS_NORETURN(e)                                        \
    do {                                                                \
        int __e = (e);                                                  \
        if( __e != 0 ) {                                                \
            if( __e == YRMCDS_SYSTEM_ERROR ) {                          \
                zend_throw_exception_ex(ce_yrmcds_error, __e TSRMLS_CC, \
                                        (char*)sys_errlist[errno]);     \
            } else {                                                    \
                zend_throw_exception_ex(ce_yrmcds_error, __e TSRMLS_CC, \
                                        (char*)yrmcds_strerror(__e));   \
            }                                                           \
        }                                                               \
    } while( 0 )
#define PRINT_YRMCDS_ERROR(e) \
    do {                                                                \
        int __e = (e);                                                  \
        char __buf[256];                                                \
        if( __e == YRMCDS_SYSTEM_ERROR ) {                              \
            snprintf(__buf, sizeof(__buf), "yrmcds: %s",                \
                     sys_errlist[errno]);                               \
        } else {                                                        \
            snprintf(__buf, sizeof(__buf), "yrmcds: %s",                \
                     yrmcds_strerror(__e));                             \
        }                                                               \
        php_log_err(__buf TSRMLS_CC);                                   \
    } while( 0 )
#define DEF_YRMCDS_CONST(name, value)                    \
    REGISTER_NS_LONG_CONSTANT("yrmcds", name, (value),   \
                              CONST_CS|CONST_PERSISTENT)
#define YRMCDS_METHOD(cn, mn)                   \
    static PHP_METHOD(cn, mn)
#define AI(cn, mn) php_yrmcds_##cn##mn##_arg
#define YRMCDS_ME(cn, mn, flags)                \
    PHP_ME(cn, mn, AI(cn, mn), (flags))


/* True global resources - no need for thread safety here */
static int le_yrmcds;

static zend_object_handlers oh_yrmcds_client;

static zend_class_entry* ce_yrmcds_client;
static zend_class_entry* ce_yrmcds_error;
static zend_class_entry* ce_yrmcds_response;

static void
on_broken_connection_detected(php_yrmcds_t* conn, yrmcds_error err,
                              yrmcds_status status TSRMLS_DC) {
    if( err != YRMCDS_OK )
        PRINT_YRMCDS_ERROR(err);
    if( status != YRMCDS_STATUS_OK && status != YRMCDS_STATUS_UNKNOWNCOMMAND ) {
        char buf[256];
        snprintf(buf, sizeof(buf), "yrmcds: unexpected response (%d)", status);
        php_log_err(buf TSRMLS_CC);
    }
    php_log_err("yrmcds: broken persistent connection" TSRMLS_CC);
    if( conn->reference_count == 0 ) {
        // Since `conn` is the last user of this persistent connection,
        // we should clean up resources.
        char* hash_key;
        int hash_key_len = HASH_KEY(hash_key, conn->persist_id);
        zend_hash_del(&EG(persistent_list), hash_key, hash_key_len + 1);
        efree(hash_key);
    }
}

/* Resource destructors. */
static void php_yrmcds_resource_dtor(zend_rsrc_list_entry* rsrc TSRMLS_DC) {
    php_yrmcds_t* c = (php_yrmcds_t*)rsrc->ptr;

    c->reference_count -= 1;

    if( c->persist_id ) {
        uint32_t serial;
        yrmcds_set_timeout(&c->res, (int)YRMCDS_G(default_timeout));
        int e = yrmcds_unlockall(&c->res, 0, &serial);
        if( e != YRMCDS_OK ) {
            on_broken_connection_detected(c, e, YRMCDS_STATUS_OK TSRMLS_CC);
            return;
        }
        yrmcds_response r;
        do {
            e = yrmcds_recv(&c->res, &r);
            if( e != YRMCDS_OK ) {
                on_broken_connection_detected(c, e, YRMCDS_STATUS_OK TSRMLS_CC);
                return;
            }
        } while( r.serial != serial );
        if( r.status != YRMCDS_STATUS_OK &&
            // memcached does not support locking, so
            r.status != YRMCDS_STATUS_UNKNOWNCOMMAND ) {
            on_broken_connection_detected(c, e, r.status TSRMLS_CC);
            return;
        }
        return;
    }

    // Since `c` does not use persistent connection,
    // the resources of `c` is not shared with other clients.
    // Therefore, we can clean up resources here.
    yrmcds_close(&c->res);
    efree(c);
}

static void php_yrmcds_resource_pdtor(zend_rsrc_list_entry* rsrc TSRMLS_DC) {
    php_yrmcds_t* c = (php_yrmcds_t*)rsrc->ptr;
    if( ! c->persist_id )
        return;

    if( c->reference_count != 0 ) {
        char buf[256];
        snprintf(buf, sizeof(buf), "yrmcds: non-zero reference_count on pdtor: %zu", c->reference_count);
        php_log_err(buf TSRMLS_CC);
    }

    yrmcds_close(&c->res);
    pefree((void*)c->persist_id, 1);
    pefree(c, 1);
}

static yrmcds_error
check_persistent_connection(php_yrmcds_t* conn, yrmcds_status* status TSRMLS_DC) {
    yrmcds_error e;
    *status = YRMCDS_STATUS_OK;
    e = yrmcds_set_timeout(&conn->res, 1);
    if( e != YRMCDS_OK ) return e;
    uint32_t serial;
    e = yrmcds_noop(&conn->res, &serial);
    if( e != YRMCDS_OK ) return e;
    yrmcds_response r;
    do {
        e = yrmcds_recv(&conn->res, &r);
        if( e != YRMCDS_OK )
            return e;
    } while( r.serial != serial );
    *status = r.status;
    if( *status != YRMCDS_STATUS_OK )
        return e;
    return yrmcds_set_timeout(&conn->res, (int)YRMCDS_G(default_timeout));
}

typedef enum {
    UEPC_OK,
    UEPC_NOT_FOUND,
    UEPC_BROKEN,
    UEPC_BROKEN_AND_OCCUPIED,
} uepc_status;

static uepc_status
use_existing_persistent_connection(const char* hash_key, int hash_key_len,
                                   int* res, yrmcds_error* err,
                                   yrmcds_status* status TSRMLS_DC) {
    zend_rsrc_list_entry* existing_conn;

    if( zend_hash_find(&EG(persistent_list), hash_key, hash_key_len+1,
                       (void**)&existing_conn) != SUCCESS )
        return UEPC_NOT_FOUND;

    php_yrmcds_t* c = existing_conn->ptr;

    if( (zend_bool)YRMCDS_G(detect_stale_connection) ) {
        *err = check_persistent_connection(c, status TSRMLS_CC);
        if( *err != YRMCDS_OK || *status != YRMCDS_STATUS_OK ) {
            size_t refcount = c->reference_count;
            on_broken_connection_detected(c, *err, *status TSRMLS_CC);
            if( refcount != 0 )
                return UEPC_BROKEN_AND_OCCUPIED;
            return UEPC_BROKEN;
        }
    }

    c->reference_count += 1;
    *res = zend_list_insert(existing_conn->ptr, le_yrmcds TSRMLS_CC);
    return UEPC_OK;
}

// \yrmcds\Client::__construct
ZEND_BEGIN_ARG_INFO_EX(AI(Client, __construct), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "node")
    ZEND_ARG_INFO(0, "port")
    ZEND_ARG_INFO(0, "persistent_id")
    ZEND_ARG_INFO(0, "prefix")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, __construct) {
    char* node;
    int node_len;
    long port = 11211;
    char* persist_id = NULL;
    int persist_id_len = 0;
    char* prefix = NULL;
    int prefix_len = 0;

    zval* objptr = getThis();
    if( ! objptr ) {
        php_error(E_ERROR, "Constructor called statically!");
        RETURN_FALSE;
    }

    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|ls!s!",
                              &node, &node_len, &port,
                              &persist_id, &persist_id_len,
                              &prefix, &prefix_len) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }

    int res;
    if( persist_id_len > 0 ) {
        char* hash_key;
        int hash_key_len = HASH_KEY(hash_key, persist_id);

        yrmcds_error err = YRMCDS_OK;
        yrmcds_status status = YRMCDS_STATUS_OK;
        uepc_status s = use_existing_persistent_connection(hash_key, hash_key_len,
                                                           &res, &err,
                                                           &status TSRMLS_CC);
        if( s == UEPC_BROKEN_AND_OCCUPIED ) {
            // Since the persistent connection is broken and used by other client,
            // we cannot destruct the connection.
            // Therefore, we throw exception and return.
            CHECK_YRMCDS(err);
            zend_throw_exception_ex(ce_yrmcds_error,
                                    PHP_YRMCDS_UNEXPECTED_RESPONSE TSRMLS_CC,
                                    "yrmcds: unexpected response (%d)", status);
            RETURN_FALSE;
        }
        if( s == UEPC_NOT_FOUND || s == UEPC_BROKEN ) {
            // There are no persistent connections with the given ID, or
            // there is a broken persistent connection with the given ID and
            // no one use it.
            // Therefore, we create new persistent connection with the given ID.
            php_yrmcds_t* c = pemalloc(sizeof(php_yrmcds_t), 1);
            c->persist_id = pestrndup(persist_id, persist_id_len, 1);
            c->reference_count = 1;
            CHECK_YRMCDS( yrmcds_connect(&c->res, node, (uint16_t)port) );
            CHECK_YRMCDS( yrmcds_set_compression(
                              &c->res, (size_t)YRMCDS_G(compression_threshold)) );
            CHECK_YRMCDS( yrmcds_set_timeout(
                              &c->res, (int)YRMCDS_G(default_timeout)) );
            res = zend_list_insert(c, le_yrmcds TSRMLS_CC);
            zend_rsrc_list_entry le;
            le.type = le_yrmcds;
            le.ptr = c;
            zend_hash_update(&EG(persistent_list), hash_key, hash_key_len+1,
                             (void*)&le, sizeof(le), NULL);
        }
        // If s == UEPC_OK, we can use the returned presistent connection.
        efree(hash_key);
    } else {
        php_yrmcds_t* c = emalloc(sizeof(php_yrmcds_t));
        c->persist_id = NULL;
        c->reference_count = 1;
        CHECK_YRMCDS( yrmcds_connect(&c->res, node, (uint16_t)port) );
        CHECK_YRMCDS( yrmcds_set_compression(
                          &c->res, (size_t)YRMCDS_G(compression_threshold)) );
        CHECK_YRMCDS( yrmcds_set_timeout(
                          &c->res, (int)YRMCDS_G(default_timeout)) );
        res = zend_list_insert(c, le_yrmcds TSRMLS_CC);
    }
    add_property_resource(getThis(), "conn", res);
    if( prefix_len > 0 )
        add_property_stringl(getThis(), "prefix", prefix, prefix_len, 1);

    Z_OBJ_HT_P(objptr) = &oh_yrmcds_client;
}

// \yrmcds\Client::setTimeout
ZEND_BEGIN_ARG_INFO_EX(AI(Client, setTimeout), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "timeout")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, setTimeout) {
    long timeout;

    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l",
                              &timeout) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    int itimeout = (int)timeout;
    if( itimeout < 0 )
        itimeout = 0;

    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);
    CHECK_YRMCDS( yrmcds_set_timeout(&c->res, itimeout) );
    RETURN_TRUE;
}

// \yrmcds\Client::recv
ZEND_BEGIN_ARG_INFO_EX(AI(Client, recv), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, recv) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    yrmcds_response r;
    CHECK_YRMCDS( yrmcds_recv(&c->res, &r) );

    object_init_ex(return_value, ce_yrmcds_response);
    add_property_long(return_value, "serial", (long)r.serial);
    add_property_long(return_value, "length", (long)r.length);
    add_property_long(return_value, "status", (long)r.status);
    add_property_long(return_value, "command", (long)r.command);
    add_property_long(return_value, "cas_unique", (long)r.cas_unique);
    add_property_long(return_value, "flags", (long)r.flags);
    if( r.key_len > 0 )
        add_property_stringl(return_value, "key", r.key, (uint)r.key_len, 1);
    if( r.data_len > 0 )
        add_property_stringl(return_value, "data", r.data, (uint)r.data_len, 1);
    add_property_long(return_value, "value", (long)r.value);
}

// \yrmcds\Client::noop
ZEND_BEGIN_ARG_INFO_EX(AI(Client, noop), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, noop) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_noop(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::get
ZEND_BEGIN_ARG_INFO_EX(AI(Client, get), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, get) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_get(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_get(&c->res, full_key, full_key_len,
                                          quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::getk
ZEND_BEGIN_ARG_INFO_EX(AI(Client, getk), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, getk) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_getk(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_getk(&c->res, full_key, full_key_len,
                                           quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::getTouch
ZEND_BEGIN_ARG_INFO_EX(AI(Client, getTouch), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, getTouch) {
    char* key;
    int key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_get_touch(&c->res, key, key_len,
                                       (uint32_t)expire, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_get_touch(
                                   &c->res, full_key, full_key_len,
                                   (uint32_t)expire, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::getkTouch
ZEND_BEGIN_ARG_INFO_EX(AI(Client, getkTouch), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, getkTouch) {
    char* key;
    int key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_getk_touch(&c->res, key, key_len,
                                        (uint32_t)expire, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_getk_touch(
                                   &c->res, full_key, full_key_len,
                                   (uint32_t)expire, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lockGet
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lockGet), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lockGet) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS(
            yrmcds_lock_get(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_lock_get(&c->res, full_key, full_key_len,
                            quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lockGetk
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lockGetk), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lockGetk) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS(
            yrmcds_lock_getk(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_lock_getk(&c->res, full_key, full_key_len,
                             quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::touch
ZEND_BEGIN_ARG_INFO_EX(AI(Client, touch), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, touch) {
    char* key;
    int key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_touch(&c->res, key, key_len,
                                   (uint32_t)expire, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_touch(
                                   &c->res, full_key, full_key_len,
                                   (uint32_t)expire, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::set
ZEND_BEGIN_ARG_INFO_EX(AI(Client, set), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "flags")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "cas")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, set) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|lllb",
                              &key, &key_len, &data, &data_len,
                              &flags, &expire, &cas, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_set(&c->res, key, key_len,
                                 data, data_len, flags, expire,
                                 cas, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_set(&c->res, full_key, full_key_len,
                                          data, data_len, flags, expire,
                                          cas, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::replace
ZEND_BEGIN_ARG_INFO_EX(AI(Client, replace), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "flags")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "cas")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, replace) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|lllb",
                              &key, &key_len, &data, &data_len,
                              &flags, &expire, &cas, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_replace(&c->res, key, key_len,
                                     data, data_len, flags, expire,
                                     cas, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_replace(&c->res, full_key, full_key_len,
                                              data, data_len, flags, expire,
                                              cas, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::add
ZEND_BEGIN_ARG_INFO_EX(AI(Client, add), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "flags")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "cas")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, add) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|lllb",
                              &key, &key_len, &data, &data_len,
                              &flags, &expire, &cas, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_add(&c->res, key, key_len,
                                 data, data_len, flags, expire,
                                 cas, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_add(&c->res, full_key, full_key_len,
                                          data, data_len, flags, expire,
                                          cas, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::replaceUnlock
ZEND_BEGIN_ARG_INFO_EX(AI(Client, replaceUnlock), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "flags")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, replaceUnlock) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    long flags = 0;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|llb",
                              &key, &key_len, &data, &data_len,
                              &flags, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_replace_unlock(&c->res, key, key_len,
                                            data, data_len, flags, expire,
                                            quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_replace_unlock(&c->res, full_key, full_key_len,
                                  data, data_len, flags, expire,
                                  quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::incr
ZEND_BEGIN_ARG_INFO_EX(AI(Client, incr), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "value")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, incr) {
    char* key;
    int key_len;
    long value;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!l|b",
                              &key, &key_len, &value, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( value < 0 ) {
        php_error(E_ERROR, "Invalid value");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_incr(&c->res, key, key_len, value,
                                  quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_incr(&c->res, full_key, full_key_len, value,
                        quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::decr
ZEND_BEGIN_ARG_INFO_EX(AI(Client, decr), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "value")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, decr) {
    char* key;
    int key_len;
    long value;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!l|b",
                              &key, &key_len, &value, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( value < 0 ) {
        php_error(E_ERROR, "Invalid value");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_decr(&c->res, key, key_len, value,
                                  quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_decr(&c->res, full_key, full_key_len, value,
                        quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::incr2
ZEND_BEGIN_ARG_INFO_EX(AI(Client, incr2), 0, ZEND_RETURN_VALUE, 3)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "value")
    ZEND_ARG_INFO(0, "initial")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, incr2) {
    char* key;
    int key_len;
    long value;
    long initial;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!ll|lb",
                              &key, &key_len, &value, &initial,
                              &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( value < 0 ) {
        php_error(E_ERROR, "Invalid value");
        RETURN_FALSE;
    }
    if( initial < 0 ) {
        php_error(E_ERROR, "Invalid initial value");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_incr2(&c->res, key, key_len, value, initial,
                                   expire, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_incr2(&c->res, full_key, full_key_len, value, initial,
                         expire, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::decr2
ZEND_BEGIN_ARG_INFO_EX(AI(Client, decr2), 0, ZEND_RETURN_VALUE, 3)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "value")
    ZEND_ARG_INFO(0, "initial")
    ZEND_ARG_INFO(0, "expire")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, decr2) {
    char* key;
    int key_len;
    long value;
    long initial;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!ll|lb",
                              &key, &key_len, &value, &initial,
                              &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( value < 0 ) {
        php_error(E_ERROR, "Invalid value");
        RETURN_FALSE;
    }
    if( initial < 0 ) {
        php_error(E_ERROR, "Invalid initial value");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_decr2(&c->res, key, key_len, value, initial,
                                   expire, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_decr2(&c->res, full_key, full_key_len, value, initial,
                         expire, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::append
ZEND_BEGIN_ARG_INFO_EX(AI(Client, append), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, append) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|b",
                              &key, &key_len, &data, &data_len,
                              &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_append(&c->res, key, key_len,
                                    data, data_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_append(&c->res, full_key, full_key_len,
                                             data, data_len, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::prepend
ZEND_BEGIN_ARG_INFO_EX(AI(Client, prepend), 0, ZEND_RETURN_VALUE, 2)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "data")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, prepend) {
    char* key;
    int key_len;
    char* data;
    int data_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!s!|b",
                              &key, &key_len, &data, &data_len,
                              &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    if( data_len == 0 ) {
        php_error(E_ERROR, "Empty data is not allowed");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_prepend(&c->res, key, key_len,
                                     data, data_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN( yrmcds_prepend(&c->res, full_key, full_key_len,
                                              data, data_len, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::delete
ZEND_BEGIN_ARG_INFO_EX(AI(Client, delete), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, delete) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_remove(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_remove(&c->res, full_key, full_key_len, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lock
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lock), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lock) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_lock(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_lock(&c->res, full_key, full_key_len, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::unlock
ZEND_BEGIN_ARG_INFO_EX(AI(Client, unlock), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, unlock) {
    char* key;
    int key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    zval** zv_prefix_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "prefix", sizeof("prefix"),
                       (void**)&zv_prefix_p) == FAILURE ) {
        CHECK_YRMCDS( yrmcds_unlock(&c->res, key, key_len, quiet, &serial) );
    } else {
        size_t full_key_len = Z_STRLEN_PP(zv_prefix_p) + key_len;
        char* full_key = emalloc(full_key_len);
        memcpy(full_key, Z_STRVAL_PP(zv_prefix_p), Z_STRLEN_PP(zv_prefix_p));
        memcpy(full_key + Z_STRLEN_PP(zv_prefix_p), key, key_len);
        CHECK_YRMCDS_NORETURN(
            yrmcds_unlock(&c->res, full_key, full_key_len, quiet, &serial) );
        efree(full_key);
    }
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::unlockAll
ZEND_BEGIN_ARG_INFO_EX(AI(Client, unlockAll), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, unlockAll) {
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b",
                              &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_unlockall(&c->res, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::flush
ZEND_BEGIN_ARG_INFO_EX(AI(Client, flush), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "delay")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, flush) {
    long delay = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|lb",
                              &delay, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( delay < 0 )
        delay = 0;
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_flush(&c->res, delay, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statGeneral
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statGeneral), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statGeneral) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_general(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statSettings
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statSettings), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statSettings) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_settings(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statItems
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statItems), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statItems) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_items(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statSizes
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statSizes), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statSizes) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_sizes(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::version
ZEND_BEGIN_ARG_INFO_EX(AI(Client, version), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, version) {
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_version(&c->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::quit
ZEND_BEGIN_ARG_INFO_EX(AI(Client, quit), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, quit) {
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|b",
                              &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    zval** zv_conn_p;
    if( zend_hash_find(Z_OBJPROP_P(getThis()), "conn", sizeof("conn"),
                       (void**)&zv_conn_p) == FAILURE ) {
        php_error(E_ERROR, "Property \"conn\" was lost!");
        RETURN_FALSE;
    }
    php_yrmcds_t* c;
    ZEND_FETCH_RESOURCE(c, php_yrmcds_t*, zv_conn_p, -1, "yrmcds", le_yrmcds);

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_quit(&c->res, quiet, &serial) );
    RETURN_LONG((long)serial);
}

static const zend_function_entry php_yrmcds_client_functions[] = {
    YRMCDS_ME(Client, __construct, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
    YRMCDS_ME(Client, setTimeout, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, recv, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, noop, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, get, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, getk, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, getTouch, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, getkTouch, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, lockGet, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, lockGetk, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, touch, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, set, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, replace, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, add, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, replaceUnlock, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, incr, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, decr, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, incr2, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, decr2, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, append, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, prepend, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, delete, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, lock, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, unlock, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, unlockAll, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, flush, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, statGeneral, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, statSettings, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, statItems, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, statSizes, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, version, ZEND_ACC_PUBLIC)
    YRMCDS_ME(Client, quit, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* {{{ PHP_INI
 */
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("yrmcds.compression_threshold",
                      "16384", PHP_INI_SYSTEM, OnUpdateLong,
                      compression_threshold,
                      zend_yrmcds_globals, yrmcds_globals)
    STD_PHP_INI_ENTRY("yrmcds.default_timeout",
                      "5", PHP_INI_SYSTEM, OnUpdateLong,
                      default_timeout,
                      zend_yrmcds_globals, yrmcds_globals)
    STD_PHP_INI_ENTRY("yrmcds.detect_stale_connection",
                      "1", PHP_INI_SYSTEM, OnUpdateBool,
                      detect_stale_connection,
                      zend_yrmcds_globals, yrmcds_globals)
PHP_INI_END()
/* }}} */

static PHP_GINIT_FUNCTION(yrmcds)
{
    yrmcds_globals->compression_threshold = 16384;
    yrmcds_globals->default_timeout = 5;
}

static PHP_MINIT_FUNCTION(yrmcds)
{
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Client", php_yrmcds_client_functions);
    memcpy(&oh_yrmcds_client, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    oh_yrmcds_client.clone_obj = NULL;
    oh_yrmcds_client.write_property = NULL;
    oh_yrmcds_client.unset_property = NULL;
    ce_yrmcds_client = zend_register_internal_class(&ce TSRMLS_CC);

    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Error", NULL);
#ifdef HAVE_SPL
    ce_yrmcds_error = zend_register_internal_class_ex(
        &ce, spl_ce_RuntimeException, NULL TSRMLS_CC);
#else
    ce_yrmcds_error = zend_register_internal_class_ex(
        &ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
#endif

    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Response", NULL);
    ce_yrmcds_response = zend_register_internal_class(&ce TSRMLS_CC);

    le_yrmcds = zend_register_list_destructors_ex(
        php_yrmcds_resource_dtor, php_yrmcds_resource_pdtor,
        "yrmcds", module_number);

#define DEF_YRMCDS_STATUS(st) DEF_YRMCDS_CONST("STATUS_" #st, YRMCDS_STATUS_##st)
    DEF_YRMCDS_STATUS(OK);
    DEF_YRMCDS_STATUS(NOTFOUND);
    DEF_YRMCDS_STATUS(EXISTS);
    DEF_YRMCDS_STATUS(TOOLARGEVALUE);
    DEF_YRMCDS_STATUS(INVALID);
    DEF_YRMCDS_STATUS(NOTSTORED);
    DEF_YRMCDS_STATUS(NONNUMERIC);
    DEF_YRMCDS_STATUS(LOCKED);
    DEF_YRMCDS_STATUS(NOTLOCKED);
    DEF_YRMCDS_STATUS(UNKNOWNCOMMAND);
    DEF_YRMCDS_STATUS(OUTOFMEMORY);

#define DEF_YRMCDS_COMMAND(cmd) DEF_YRMCDS_CONST("CMD_" #cmd, YRMCDS_CMD_##cmd)
    DEF_YRMCDS_COMMAND(GET);
    DEF_YRMCDS_COMMAND(SET);
    DEF_YRMCDS_COMMAND(ADD);
    DEF_YRMCDS_COMMAND(REPLACE);
    DEF_YRMCDS_COMMAND(DELETE);
    DEF_YRMCDS_COMMAND(INCREMENT);
    DEF_YRMCDS_COMMAND(DECREMENT);
    DEF_YRMCDS_COMMAND(QUIT);
    DEF_YRMCDS_COMMAND(FLUSH);
    DEF_YRMCDS_COMMAND(GETQ);
    DEF_YRMCDS_COMMAND(NOOP);
    DEF_YRMCDS_COMMAND(VERSION);
    DEF_YRMCDS_COMMAND(GETK);
    DEF_YRMCDS_COMMAND(GETKQ);
    DEF_YRMCDS_COMMAND(APPEND);
    DEF_YRMCDS_COMMAND(PREPEND);
    DEF_YRMCDS_COMMAND(STAT);
    DEF_YRMCDS_COMMAND(SETQ);
    DEF_YRMCDS_COMMAND(ADDQ);
    DEF_YRMCDS_COMMAND(REPLACEQ);
    DEF_YRMCDS_COMMAND(DELETEQ);
    DEF_YRMCDS_COMMAND(INCREMENTQ);
    DEF_YRMCDS_COMMAND(DECREMENTQ);
    DEF_YRMCDS_COMMAND(QUITQ);
    DEF_YRMCDS_COMMAND(FLUSHQ);
    DEF_YRMCDS_COMMAND(APPENDQ);
    DEF_YRMCDS_COMMAND(PREPENDQ);
    DEF_YRMCDS_COMMAND(TOUCH);
    DEF_YRMCDS_COMMAND(GAT);
    DEF_YRMCDS_COMMAND(GATQ);
    DEF_YRMCDS_COMMAND(GATK);
    DEF_YRMCDS_COMMAND(GATKQ);
    DEF_YRMCDS_COMMAND(LOCK);
    DEF_YRMCDS_COMMAND(LOCKQ);
    DEF_YRMCDS_COMMAND(UNLOCK);
    DEF_YRMCDS_COMMAND(UNLOCKQ);
    DEF_YRMCDS_COMMAND(UNLOCKALL);
    DEF_YRMCDS_COMMAND(UNLOCKALLQ);
    DEF_YRMCDS_COMMAND(LAG);
    DEF_YRMCDS_COMMAND(LAGQ);
    DEF_YRMCDS_COMMAND(LAGK);
    DEF_YRMCDS_COMMAND(LAGKQ);
    DEF_YRMCDS_COMMAND(RAU);
    DEF_YRMCDS_COMMAND(RAUQ);

    REGISTER_INI_ENTRIES();
    return SUCCESS;
}

static PHP_MSHUTDOWN_FUNCTION(yrmcds)
{
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}

static PHP_MINFO_FUNCTION(yrmcds)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "yrmcds support", "enabled");
    php_info_print_table_row(2, "version", PHP_YRMCDS_VERSION);
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}

zend_module_entry yrmcds_module_entry = {
    STANDARD_MODULE_HEADER,
    "yrmcds",
    NULL,
    PHP_MINIT(yrmcds), PHP_MSHUTDOWN(yrmcds),
    NULL, NULL,
    PHP_MINFO(yrmcds),
    PHP_YRMCDS_VERSION,
    PHP_MODULE_GLOBALS(yrmcds),
    PHP_GINIT(yrmcds),
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_YRMCDS
ZEND_GET_MODULE(yrmcds)
#endif
