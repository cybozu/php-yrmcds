// (C) 2013-2016 Cybozu.

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

#include <alloca.h>
#include <errno.h>
#include <string.h>

ZEND_DECLARE_MODULE_GLOBALS(yrmcds)


/* Macro functions */
#define CHECK_YRMCDS(e)                                                 \
    do {                                                                \
        int __e = (e);                                                  \
        if( __e != 0 ) {                                                \
            if( __e == YRMCDS_SYSTEM_ERROR ) {                          \
                zend_throw_exception_ex(ce_yrmcds_error, __e,           \
                                        strerror(errno));               \
            } else {                                                    \
                zend_throw_exception_ex(ce_yrmcds_error, __e,           \
                                        (char*)yrmcds_strerror(__e));   \
            }                                                           \
            RETURN_FALSE;                                               \
        }                                                               \
    } while( 0 )

#define PRINT_YRMCDS_ERROR(e) \
    do {                                                                \
        int __e = (e);                                                  \
        char __buf[256];                                                \
        if( __e == YRMCDS_SYSTEM_ERROR ) {                              \
            snprintf(__buf, sizeof(__buf), "yrmcds: %s",                \
                     strerror(errno));                                  \
        } else {                                                        \
            snprintf(__buf, sizeof(__buf), "yrmcds: %s",                \
                     yrmcds_strerror(__e));                             \
        }                                                               \
        php_log_err(__buf);                                             \
    } while( 0 )

#define DEF_YRMCDS_CONST(name, value)                    \
    REGISTER_NS_LONG_CONSTANT("yrmcds", name, (value),   \
                              CONST_CS|CONST_PERSISTENT)
#define YRMCDS_METHOD(cn, mn)                   \
    static PHP_METHOD(cn, mn)
#define AI(cn, mn) php_yrmcds_##cn##mn##_arg
#define YRMCDS_ME(cn, mn, flags)                \
    PHP_ME(cn, mn, AI(cn, mn), (flags))


/* Connection resource */
typedef struct {
    char* pkey;
    size_t pkey_len;
    size_t reference_count;
    yrmcds res;
} php_yrmcds_t;

static void destruct_conn(php_yrmcds_t* c);

/* Custom object for yrmcds Client */
typedef struct yrmcds_client_object {
    php_yrmcds_t* conn;
    zend_string* prefix;
    zend_object std;
} yrmcds_client_object;

static zend_object_handlers oh_yrmcds_client;

static inline
yrmcds_client_object* fetch_yrmcds_client_object(zend_object *obj) {
    return (yrmcds_client_object*)((char*)obj - XtOffsetOf(yrmcds_client_object, std));
}

#define YRMCDS_CLIENT_OBJECT_P(zv) fetch_yrmcds_client_object(Z_OBJ_P((zv)))
#define YRMCDS_CLIENT_EXPLODE(zv)                               \
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(zv);     \
    if( obj->prefix ) {                                         \
        size_t full_key_len = obj->prefix->len + key_len;       \
        char* full_key = alloca(full_key_len);                  \
        memcpy(full_key, obj->prefix->val, obj->prefix->len);   \
        memcpy(full_key + obj->prefix->len, key, key_len);      \
        key_len = full_key_len;                                 \
        key = full_key;                                         \
    }

static zend_object* yrmcds_client_new(zend_class_entry *ce) {
    yrmcds_client_object* o;

    o = ecalloc(1, sizeof(yrmcds_client_object) + zend_object_properties_size(ce));
    zend_object_std_init(&o->std, ce);  // upcall the default
    object_properties_init(&o->std, ce);
    o->std.handlers = &oh_yrmcds_client;

    return &o->std;
}

static void yrmcds_client_delete(zend_object *std) {
    yrmcds_client_object* o = fetch_yrmcds_client_object(std);
    if( o->conn ) {
        destruct_conn(o->conn);
        o->conn = NULL;
    }
    if( o->prefix ) {
        zend_string_release(o->prefix);
        o->prefix = NULL;
    }
    zend_object_std_dtor(std);  // upcall the default
}


/* True global resources - no need for thread safety here */
static int le_yrmcds;

static zend_class_entry* ce_yrmcds_client;
static zend_class_entry* ce_yrmcds_error;
static zend_class_entry* ce_yrmcds_response;

static void
on_broken_connection_detected(php_yrmcds_t* conn, yrmcds_error err,
                              yrmcds_status status) {
    if( err != YRMCDS_OK )
        PRINT_YRMCDS_ERROR(err);
    if( status != YRMCDS_STATUS_OK && status != YRMCDS_STATUS_UNKNOWNCOMMAND ) {
        char buf[256];
        snprintf(buf, sizeof(buf), "yrmcds: unexpected response (%d)", status);
        php_log_err(buf);
    }
    php_log_err("yrmcds: broken persistent connection");
    if( conn->reference_count == 0 ) {
        // Since `conn` is the last user of this persistent connection,
        // we should clean up resources.
        zend_hash_str_del(&EG(persistent_list), conn->pkey, conn->pkey_len);
    }
}

/* Resource destructors. */
static void destruct_conn(php_yrmcds_t* c) {
    c->reference_count -= 1;

    if( c->pkey ) {
        uint32_t serial;
        yrmcds_set_timeout(&c->res, (int)YRMCDS_G(default_timeout));
        int e = yrmcds_unlockall(&c->res, 0, &serial);
        if( e != YRMCDS_OK ) {
            on_broken_connection_detected(c, e, YRMCDS_STATUS_OK);
            return;
        }
        yrmcds_response r;
        do {
            e = yrmcds_recv(&c->res, &r);
            if( e != YRMCDS_OK ) {
                on_broken_connection_detected(c, e, YRMCDS_STATUS_OK);
                return;
            }
        } while( r.serial != serial );
        if( r.status != YRMCDS_STATUS_OK &&
            // memcached does not support locking, so
            r.status != YRMCDS_STATUS_UNKNOWNCOMMAND ) {
            on_broken_connection_detected(c, e, r.status);
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

static ZEND_RSRC_DTOR_FUNC(php_yrmcds_resource_pdtor) {
    if( res->ptr == NULL )
        return;
    php_yrmcds_t* c = (php_yrmcds_t*)res->ptr;
    if( c->reference_count != 0 ) {
        char buf[256];
        snprintf(buf, sizeof(buf), "yrmcds: non-zero reference_count on pdtor: %zu", c->reference_count);
        php_log_err(buf);
    }

    yrmcds_close(&c->res);
    pefree((void*)c->pkey, 1);
    pefree(c, 1);
    res->ptr = NULL;
}

static yrmcds_error
check_persistent_connection(php_yrmcds_t* conn, yrmcds_status* status) {
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
                                   php_yrmcds_t** res, yrmcds_error* err,
                                   yrmcds_status* status) {
    zend_resource* le;
    if( (le = zend_hash_str_find_ptr(&EG(persistent_list), hash_key, hash_key_len)) == NULL )
        return UEPC_NOT_FOUND;
    if( le->type != le_yrmcds )
        return UEPC_NOT_FOUND;

    php_yrmcds_t* c = le->ptr;
    if( (zend_bool)YRMCDS_G(detect_stale_connection) ) {
        *err = check_persistent_connection(c, status);
        if( *err != YRMCDS_OK || *status != YRMCDS_STATUS_OK ) {
            size_t refcount = c->reference_count;
            on_broken_connection_detected(c, *err, *status);
            return refcount == 0 ? UEPC_BROKEN : UEPC_BROKEN_AND_OCCUPIED;
        }
    }
    c->reference_count += 1;
    if( c->reference_count > 1 )
        php_log_err("yrmcds: clients share the same persistent connection.");
    *res = c;
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
    size_t node_len;
    long port = 11211;
    char* persist_id = NULL;
    size_t persist_id_len = 0;
    char* prefix = NULL;
    size_t prefix_len = 0;

    zval* objptr = getThis();
    if( ! objptr ) {
        php_error(E_ERROR, "Constructor called statically!");
        RETURN_FALSE;
    }

    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s|ls!s!",
                              &node, &node_len, &port,
                              &persist_id, &persist_id_len,
                              &prefix, &prefix_len) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(objptr);
    if( persist_id_len > 0 ) {
        size_t hash_key_len = 7 + persist_id_len;
        char* hash_key = alloca(hash_key_len);
        memcpy(hash_key, "yrmcds:", 7);
        memcpy(hash_key+7, persist_id, persist_id_len);

        yrmcds_error err = YRMCDS_OK;
        yrmcds_status status = YRMCDS_STATUS_OK;
        uepc_status s = use_existing_persistent_connection(
            hash_key, hash_key_len, &obj->conn, &err, &status);
        if( s == UEPC_BROKEN_AND_OCCUPIED ) {
            // Since the persistent connection is broken and used by other client,
            // we cannot destruct the connection.
            // Therefore, we throw exception and return.
            CHECK_YRMCDS(err);
            zend_throw_exception_ex(ce_yrmcds_error,
                                    PHP_YRMCDS_UNEXPECTED_RESPONSE,
                                    "yrmcds: unexpected response (%d)", status);
            RETURN_FALSE;
        }
        if( s == UEPC_NOT_FOUND || s == UEPC_BROKEN ) {
            // There are no persistent connections with the given ID, or
            // there is a broken persistent connection with the given ID and
            // no one use it.
            // Therefore, we create new persistent connection with the given ID.
            php_yrmcds_t* c = pemalloc(sizeof(php_yrmcds_t), 1);
            c->pkey = pemalloc(hash_key_len, 1);
            c->pkey_len = hash_key_len;
            memcpy(c->pkey, hash_key, hash_key_len);
            c->reference_count = 1;
            CHECK_YRMCDS( yrmcds_connect(&c->res, node, (uint16_t)port) );
            CHECK_YRMCDS( yrmcds_set_compression(
                              &c->res, (size_t)YRMCDS_G(compression_threshold)) );
            CHECK_YRMCDS( yrmcds_set_timeout(
                              &c->res, (int)YRMCDS_G(default_timeout)) );
            obj->conn = c;
            zend_resource le;
            le.type = le_yrmcds;
            le.ptr = c;
            GC_SET_REFCOUNT(&le, 1);
            zend_hash_str_update_mem(&EG(persistent_list),
                                     hash_key, hash_key_len, &le, sizeof(le));
            // There is no need to free the return value of the above function
            // as plist_entry_destructor defined in Zend/zend_list.c does
            // it automatically.
        }
        // If s == UEPC_OK, we can use the returned presistent connection.
    } else {
        php_yrmcds_t* c = ecalloc(1, sizeof(php_yrmcds_t));
        c->reference_count = 1;
        CHECK_YRMCDS( yrmcds_connect(&c->res, node, (uint16_t)port) );
        CHECK_YRMCDS( yrmcds_set_compression(
                          &c->res, (size_t)YRMCDS_G(compression_threshold)) );
        CHECK_YRMCDS( yrmcds_set_timeout(
                          &c->res, (int)YRMCDS_G(default_timeout)) );
        obj->conn = c;
    }
    if( prefix_len > 0 )
        obj->prefix = zend_string_init(prefix, prefix_len, 0);
}

// \yrmcds\Client::setTimeout
ZEND_BEGIN_ARG_INFO_EX(AI(Client, setTimeout), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "timeout")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, setTimeout) {
    long timeout;

    if( zend_parse_parameters(ZEND_NUM_ARGS(), "l", &timeout) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    int itimeout = (int)timeout;
    if( itimeout < 0 )
        itimeout = 0;

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());
    CHECK_YRMCDS( yrmcds_set_timeout(&obj->conn->res, itimeout) );
    RETURN_TRUE;
}

// \yrmcds\Client::recv
ZEND_BEGIN_ARG_INFO_EX(AI(Client, recv), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, recv) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    yrmcds_response r;
    CHECK_YRMCDS( yrmcds_recv(&obj->conn->res, &r) );

    object_init_ex(return_value, ce_yrmcds_response);
#if PHP_MAJOR_VERSION >= 8
#  define UPDATE_PROP_LONG(name, value) \
    zend_update_property_long(ce_yrmcds_response, Z_OBJ_P(return_value), ZEND_STRL(name), (long)value)
#else
#  define UPDATE_PROP_LONG(name, value) \
    zend_update_property_long(ce_yrmcds_response, return_value, ZEND_STRL(name), (long)value)
#endif
    UPDATE_PROP_LONG("serial", r.serial);
    UPDATE_PROP_LONG("length", r.length);
    UPDATE_PROP_LONG("status", r.status);
    UPDATE_PROP_LONG("command", r.command);
    UPDATE_PROP_LONG("cas_unique", r.cas_unique);
    UPDATE_PROP_LONG("flags", r.flags);
    if( r.key_len > 0 ) {
#if PHP_MAJOR_VERSION >= 8
        zend_update_property_stringl(ce_yrmcds_response, Z_OBJ_P(return_value),
                                     ZEND_STRL("key"), r.key, r.key_len);
#else
        zend_update_property_stringl(ce_yrmcds_response, return_value,
                                     ZEND_STRL("key"), r.key, r.key_len);
#endif

    }

    if( r.data_len > 0 ) {
#if PHP_MAJOR_VERSION >= 8
        zend_update_property_stringl(ce_yrmcds_response, Z_OBJ_P(return_value),
                                     ZEND_STRL("data"), r.data, r.data_len);
#else
        zend_update_property_stringl(ce_yrmcds_response, return_value,
                                     ZEND_STRL("data"), r.data, r.data_len);
#endif
    }
    UPDATE_PROP_LONG("value", r.value);
#undef UPDATE_PROP_LONG
}

// \yrmcds\Client::noop
ZEND_BEGIN_ARG_INFO_EX(AI(Client, noop), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, noop) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_noop(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::get
ZEND_BEGIN_ARG_INFO_EX(AI(Client, get), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, get) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_get(&obj->conn->res, key, key_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::getk
ZEND_BEGIN_ARG_INFO_EX(AI(Client, getk), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, getk) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_getk(&obj->conn->res, key, key_len, quiet, &serial) );
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
    size_t key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_get_touch(&obj->conn->res, key, key_len,
                                   (uint32_t)expire, quiet, &serial) );
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
    size_t key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_getk_touch(&obj->conn->res, key, key_len,
                                    (uint32_t)expire, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lockGet
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lockGet), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lockGet) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS(
            yrmcds_lock_get(&obj->conn->res, key, key_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lockGetk
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lockGetk), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lockGetk) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS(
        yrmcds_lock_getk(&obj->conn->res, key, key_len, quiet, &serial) );
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
    size_t key_len;
    long expire;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!l|b",
                              &key, &key_len, &expire, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_touch(&obj->conn->res, key, key_len,
                               (uint32_t)expire, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|lllb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_set(&obj->conn->res, key, key_len,
                             data, data_len, flags, expire,
                             cas, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|lllb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_replace(&obj->conn->res, key, key_len,
                                 data, data_len, flags, expire,
                                 cas, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    long flags = 0;
    long expire = 0;
    long cas = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|lllb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_add(&obj->conn->res, key, key_len,
                             data, data_len, flags, expire,
                             cas, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    long flags = 0;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|llb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_replace_unlock(&obj->conn->res, key, key_len,
                                        data, data_len, flags, expire,
                                        quiet, &serial) );
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
    size_t key_len;
    long value;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!l|b",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_incr(&obj->conn->res, key, key_len, value,
                              quiet, &serial) );
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
    size_t key_len;
    long value;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!l|b",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_decr(&obj->conn->res, key, key_len, value,
                              quiet, &serial) );
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
    size_t key_len;
    long value;
    long initial;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!ll|lb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_incr2(&obj->conn->res, key, key_len, value, initial,
                               expire, quiet, &serial) );
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
    size_t key_len;
    long value;
    long initial;
    long expire = 0;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!ll|lb",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_decr2(&obj->conn->res, key, key_len, value, initial,
                               expire, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|b",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_append(&obj->conn->res, key, key_len,
                                data, data_len, quiet, &serial) );
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
    size_t key_len;
    char* data;
    size_t data_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!s!|b",
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

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_prepend(&obj->conn->res, key, key_len,
                                 data, data_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::delete
ZEND_BEGIN_ARG_INFO_EX(AI(Client, delete), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, delete) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_remove(&obj->conn->res, key, key_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::lock
ZEND_BEGIN_ARG_INFO_EX(AI(Client, lock), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, lock) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_lock(&obj->conn->res, key, key_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::unlock
ZEND_BEGIN_ARG_INFO_EX(AI(Client, unlock), 0, ZEND_RETURN_VALUE, 1)
    ZEND_ARG_INFO(0, "key")
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, unlock) {
    char* key;
    size_t key_len;
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "s!|b",
                              &key, &key_len, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( key_len == 0 ) {
        php_error(E_ERROR, "Invalid key");
        RETURN_FALSE;
    }

    YRMCDS_CLIENT_EXPLODE(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_unlock(&obj->conn->res, key, key_len, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::unlockAll
ZEND_BEGIN_ARG_INFO_EX(AI(Client, unlockAll), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, unlockAll) {
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_unlockall(&obj->conn->res, quiet, &serial) );
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
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "|lb",
                              &delay, &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( delay < 0 )
        delay = 0;

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_flush(&obj->conn->res, delay, quiet, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statGeneral
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statGeneral), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statGeneral) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_general(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statSettings
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statSettings), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statSettings) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_settings(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statItems
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statItems), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statItems) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_items(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::statSizes
ZEND_BEGIN_ARG_INFO_EX(AI(Client, statSizes), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, statSizes) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_stat_sizes(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::keys
ZEND_BEGIN_ARG_INFO_EX(AI(Client, keys), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "prefix")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, keys) {
    char* prefix = NULL;
    size_t prefix_len = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "|s",
                              &prefix, &prefix_len) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }
    if( prefix_len == 0 )
        prefix = NULL;

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_keys(&obj->conn->res, prefix, prefix_len, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::version
ZEND_BEGIN_ARG_INFO_EX(AI(Client, version), 0, ZEND_RETURN_VALUE, 0)
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, version) {
    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_version(&obj->conn->res, &serial) );
    RETURN_LONG((long)serial);
}

// \yrmcds\Client::quit
ZEND_BEGIN_ARG_INFO_EX(AI(Client, quit), 0, ZEND_RETURN_VALUE, 0)
    ZEND_ARG_INFO(0, "quiet")
ZEND_END_ARG_INFO()

YRMCDS_METHOD(Client, quit) {
    zend_bool quiet = 0;
    if( zend_parse_parameters(ZEND_NUM_ARGS(), "|b", &quiet) == FAILURE ) {
        php_error(E_ERROR, "Invalid argument");
        RETURN_FALSE;
    }

    yrmcds_client_object* obj = YRMCDS_CLIENT_OBJECT_P(getThis());

    uint32_t serial;
    CHECK_YRMCDS( yrmcds_quit(&obj->conn->res, quiet, &serial) );
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
    YRMCDS_ME(Client, keys, ZEND_ACC_PUBLIC)
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
#if defined(COMPILE_DL_YRMCDS) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    yrmcds_globals->compression_threshold = 16384;
    yrmcds_globals->default_timeout = 5;
}

static PHP_MINIT_FUNCTION(yrmcds)
{
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Client", php_yrmcds_client_functions);
    ce_yrmcds_client = zend_register_internal_class(&ce);
    ce_yrmcds_client->create_object = yrmcds_client_new;
    memcpy(&oh_yrmcds_client, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    oh_yrmcds_client.offset = XtOffsetOf(yrmcds_client_object, std);
    oh_yrmcds_client.free_obj = yrmcds_client_delete;
    oh_yrmcds_client.clone_obj = NULL;

    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Error", NULL);
#ifdef HAVE_SPL
    ce_yrmcds_error = zend_register_internal_class_ex(
        &ce, spl_ce_RuntimeException);
#else
    ce_yrmcds_error = zend_register_internal_class_ex(
        &ce, zend_exception_get_default());
#endif

    INIT_NS_CLASS_ENTRY(ce, "yrmcds", "Response", NULL);
    ce_yrmcds_response = zend_register_internal_class(&ce);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("status"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("serial"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("length"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("command"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("cas_unique"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_long(ce_yrmcds_response, ZEND_STRL("flags"),
                               0, ZEND_ACC_PUBLIC);
    zend_declare_property_null(ce_yrmcds_response, ZEND_STRL("key"),
                               ZEND_ACC_PUBLIC);
    zend_declare_property_null(ce_yrmcds_response, ZEND_STRL("data"),
                               ZEND_ACC_PUBLIC);
    zend_declare_property_null(ce_yrmcds_response, ZEND_STRL("value"),
                               ZEND_ACC_PUBLIC);

    le_yrmcds = zend_register_list_destructors_ex(
        NULL, php_yrmcds_resource_pdtor, "yrmcds", module_number);

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
    DEF_YRMCDS_COMMAND(KEYS);

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
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
ZEND_GET_MODULE(yrmcds)
#endif
