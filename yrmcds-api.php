<?php

// This is a dummy script to generte API documents using phpdoc.

/**
 * A full-featured memcached / yrmcds client extension.
 *
 * This extension wraps libyrmcds, a memcached/yrmcds client library.
 *
 * @author Yamamoto, Hirotaka
 * @version 1.4.0
 * @copyright (C) 2013-2016 Cybozu.
 * @license 2-clause BSD
 * @package extension
 * @link http://cybozu.github.io/libyrmcds/
 * @link http://cybozu.github.io/yrmcds/
 */
namespace yrmcds;


/**
 * Server response status indicating the command was successfully executed.
 */
const STATUS_OK = 0;

/**
 * Server response status indicating the object is not found.
 */
const STATUS_NOTFOUND = 0x0001;

/**
 * Server response status indicating compare-and-swap failure.
 */
const STATUS_EXISTS = 0x0002;

/**
 * Server response status indicating too large values.
 */
const STATUS_TOOLARGEVALUE = 0x0003;

/**
 * Server response status indicating the request was invalid.
 */
const STATUS_INVALID = 0x0004;

/**
 * Server response status indicating the object is not created nor modified.
 */
const STATUS_NOTSTORED = 0x0005;

/**
 * Server response status indicating the object is not numeric.
 */
const STATUS_NONNUMERIC = 0x0006;

/**
 * Server response status indicating the object is locked.
 */
const STATUS_LOCKED = 0x0010;

/**
 * Server response status indicating the object is _not_ locked.
 */
const STATUS_NOTLOCKED = 0x0011;

/**
 * Server response status indicating the command is not supported.
 */
const STATUS_UNKNOWNCOMMAND = 0x0081;

/**
 * Server response status indicating not enough memory.
 */
const STATUS_OUTOFMEMORY = 0x0082;

/**
 * Binary command 'Get'.
 */
const CMD_GET = 0x00;

/**
 * Binary command 'Set'.
 */
const CMD_SET = 0x01;

/**
 * Binary command 'Add'.
 */
const CMD_ADD = 0x02;

/**
 * Binary command 'Replace'.
 */
const CMD_REPLACE = 0x03;

/**
 * Binary command 'Delete'.
 */
const CMD_DELETE = 0x04;

/**
 * Binary command 'Increment'.
 */
const CMD_INCREMENT = 0x05;

/**
 * Binary command 'Decrement'.
 */
const CMD_DECREMENT = 0x06;

/**
 * Binary command 'Quit'.
 */
const CMD_QUIT = 0x07;

/**
 * Binary command 'Flush'.
 */
const CMD_FLUSH = 0x08;

/**
 * Binary command 'GetQ'.
 */
const CMD_GETQ = 0x09;

/**
 * Binary command 'Noop'.
 */
const CMD_NOOP = 0x0a;

/**
 * Binary command 'Version'.
 */
const CMD_VERSION = 0x0b;

/**
 * Binary command 'GetK'.
 */
const CMD_GETK = 0x0c;

/**
 * Binary command 'GetKQ'.
 */
const CMD_GETKQ = 0x0d;

/**
 * Binary command 'Append'.
 */
const CMD_APPEND = 0x0e;

/**
 * Binary command 'Prepend'.
 */
const CMD_PREPEND = 0x0f;

/**
 * Binary command 'Stat'.
 */
const CMD_STAT = 0x10;

/**
 * Binary command 'SetQ'.
 */
const CMD_SETQ = 0x11;

/**
 * Binary command 'AddQ'.
 */
const CMD_ADDQ = 0x12;

/**
 * Binary command 'ReplaceQ'.
 */
const CMD_REPLACEQ = 0x13;

/**
 * Binary command 'DeleteQ'.
 */
const CMD_DELETEQ = 0x14;

/**
 * Binary command 'IncrementQ'.
 */
const CMD_INCREMENTQ = 0x15;

/**
 * Binary command 'DecrementQ'.
 */
const CMD_DECREMENTQ = 0x16;

/**
 * Binary command 'QuitQ'.
 */
const CMD_QUITQ = 0x17;

/**
 * Binary command 'FlushQ'.
 */
const CMD_FLUSHQ = 0x18;

/**
 * Binary command 'AppendQ'.
 */
const CMD_APPENDQ = 0x19;

/**
 * Binary command 'PrependQ'.
 */
const CMD_PREPENDQ = 0x1a;

/**
 * Binary command 'Touch'.
 */
const CMD_TOUCH = 0x1c;

/**
 * Binary command 'GaT' (get and touch).
 */
const CMD_GAT = 0x1d;

/**
 * Binary command 'GaTQ' (get and touch quietly).
 */
const CMD_GATQ = 0x1e;

/**
 * Binary command 'GaTK' (get and touch with key).
 */
const CMD_GATK = 0x23;

/**
 * Binary command 'GaTKQ' (get and touch with key quietly).
 */
const CMD_GATKQ = 0x24;

/**
 * Binary command 'Lock'.
 */
const CMD_LOCK = 0x40;

/**
 * Binary command 'LockQ'.
 */
const CMD_LOCKQ = 0x41;

/**
 * Binary command 'Unlock'.
 */
const CMD_UNLOCK = 0x42;

/**
 * Binary command 'UnlockQ'.
 */
const CMD_UNLOCKQ = 0x43;

/**
 * Binary command 'UnlockAll'.
 */
const CMD_UNLOCKALL = 0x44;

/**
 * Binary command 'UnlockAllQ'.
 */
const CMD_UNLOCKALLQ = 0x45;

/**
 * Binary command 'LaG' (lock and get).
 */
const CMD_LAG = 0x46;

/**
 * Binary command 'LaGQ' (lock and get quietly).
 */
const CMD_LAGQ = 0x47;

/**
 * Binary command 'LaGK' (lock and get with key).
 */
const CMD_LAGK = 0x48;

/**
 * Binary command 'LaGKQ' (lock and get with key quietly).
 */
const CMD_LAGKQ = 0x49;

/**
 * Binary command 'RaU' (replace and unlock).
 */
const CMD_RAU = 0x4a;

/**
 * Binary command 'RaUQ' (replace and unlock quietly).
 */
const CMD_RAUQ = 0x4b;

/**
 * Binary command 'Keys'.
 */
const CMD_KEYS = 0x50;


/**
 * Memcached client class.
 *
 * Instances of this class represents a connection to a memcached/yrmcds
 * server.  Most methods may throw \yrmcds\Error when a
 * non-recoverable error happens.
 */
class Client {

    /**
     * Constructor.
     *
     * If $persist_id is NULL, a new connection is established.
     * Otherwise, a persistent connection may be reused.
     *
     * If $prefix is not NULL, keys will automatically be prefixed
     * with the specified prefix string.
     *
     * @param string  $node        The hostname or IP address of the server.
     * @param int     $port        The port number.
     * @param string  $persist_id  Persistent connection ID.
     * @param string  $prefix      Auto-prefix for keys.
     */
    function __construct($node, $port = 11211,
                         $persist_id = NULL, $prefix = NULL) {}

    /**
     * Set socket timeouts.
     *
     * Normally, socket operations may time out as specified in
     * `yrmcds.default_timeout` ini parameter.  This method can change
     * timeout temporarily during a request.
     *
     * @param int  $timeout  The timeout seconds.  0 disables timeout.
     * @return void
     */
    public function setTimeout($timeout) {}

    /**
     * Receive a command response.
     *
     * This method receives a command response.  Be warned that commands
     * issued quietly may return a response to tell errors.  Also note
     * that statistics commands such as `statGeneral()` returns a series
     * of response packets.
     *
     * @return \yrmcds\Response
     */
    public function recv() {}

    /**
     * Send `Noop` command.
     *
     * This command can finalize a series of get request issued quietly
     * (i.e. $quiet = TRUE) because the server must send a response for
     * this.
     *
     * @return int  The serial number of the request.
     */
    public function noop() {}

    /**
     * Send `Get` or `GetQ` command to retrieve an object.
     *
     * This command sends a request to retrieve the object data associated
     * with `key`.  If $quiet is TRUE, the server will _not_ return a
     * response if the object is not found.
     *
     * The successful response will not contain the key.  To include the
     * requested key in the response, use `getk()`.
     *
     * @param string  $key  The key string.
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function get($key, $quiet = FALSE) {}

    /**
     * Send `GetK` or `GetKQ` command to retrieve an object.
     *
     * Like `get()`, but the successful response contains the requested
     * key.
     *
     * @param string  $key  The key string.
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function getk($key, $quiet = FALSE) {}

    /**
     * Send `GaT` or `GaTQ` command to retrieve and touch an object.
     *
     * Like `get()`, but this command resets the object's expiration time.
     *
     * @param string  $key   The key string.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function getTouch($key, $expire, $quiet = FALSE) {}

    /**
     * Send `GaTK` or `GaTKQ` command to retrieve and touch an object.
     *
     * Like `getTouch()`, but the successful response contains the requested
     * key.
     *
     * @param string  $key   The key string.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function getkTouch($key, $expire, $quiet = FALSE) {}

    /**
     * Send `LaG` or `LaGQ` command to lock and retrieve an object.
     *
     * Like `get()`, but this command first locks the object.
     *
     * @param string  $key  The key string.
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function lockGet($key, $quiet = FALSE) {}

    /**
     * Send `LaGK` or `LaGKQ` command to lock and retrieve an object.
     *
     * Like `lockGet()`, but the successful response contains the requested
     * key.
     *
     * @param string  $key  The key string.
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function lockGetk($key, $quiet = FALSE) {}

    /**
     * Send `Touch` command to retrieve and touch an object.
     *
     * This command resets the object's expiration time.
     *
     * @param string  $key  The key string.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param bool  $quiet   Ignored (reserved).
     * @return int  The serial number of the request.
     */
    public function touch($key, $expire, $quiet = FALSE) {}

    /**
     * Send `Set` or `SetQ` command to store an object.
     *
     * This command adds or replaces an object.
     *
     * $key and $data must not be an empty string.
     *
     * Lower 30 bits of $flags can be used freely.
     * Other bits are reserved for the underlying library routines.
     *
     * If $cas is not 0, the operation will succeed only when the CAS
     * value of the object is the same as $cas.
     *
     * @param string     $key    The key string.
     * @param int|string $data   Data to be stored.
     * @param int   $flags   Flags stored along with the data.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param int   $cas     Non-zero enables compare-and-swap.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function set($key, $data, $flags = 0, $expire = 0, $cas = 0,
                        $quiet = FALSE) {}

    /**
     * Send `Replace` or `ReplaceQ` command to replace an existing object.
     *
     * Like `set()`, but this method only replaces an existing object.
     * If there is not an object for $key, an error response will be returned.
     *
     * @param string     $key    The key string.
     * @param int|string $data   Data to be stored.
     * @param int   $flags   Flags stored along with the data.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param int   $cas     Non-zero enables compare-and-swap.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function replace($key, $data, $flags = 0, $expire = 0, $cas = 0,
                            $quiet = FALSE) {}

    /**
     * Send `Add` or `AddQ` command to replace an existing object.
     *
     * Like `set()`, but this method only stores a new object.
     * If there is already an object for $key, an error response will
     * be returned.
     *
     * @param string     $key    The key string.
     * @param int|string $data   Data to be stored.
     * @param int   $flags   Flags stored along with the data.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param int   $cas     Non-zero enables compare-and-swap.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function add($key, $data, $flags = 0, $expire = 0, $cas = 0,
                        $quiet = FALSE) {}

    /**
     * Send `RaU` or `RaUQ` command to replace and unlock an existing object.
     *
     * This command will fail unless the object has been locked by this client.
     *
     * Locked object will neither be expired nor evicted, ensuring this command
     * will succeed.  After replacing the data, the object will be unlocked.
     *
     * @param string     $key    The key string.
     * @param int|string $data   Data to be stored.
     * @param int   $flags   Flags stored along with the data.
     * @param int   $expire  Expiration time in seconds.  0 disables expiration.
     * @param bool  $quiet   TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function replaceUnlock($key, $data, $flags = 0, $expire = 0,
                                  $quiet = FALSE) {}

    /**
     * Send `Increment` or `IncrementQ` command to increment an object's value.
     *
     * Increments an object's value.  If the object data is not numeric,
     * this command will fail.
     *
     * @param string  $key    The key string.
     * @param int     $value  Amount to increment.
     * @param bool    $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function incr($key, $value, $quiet = FALSE) {}

    /**
     * Send `Increment` or `IncrementQ` command to increment an object's value.
     *
     * Like `incr()`, but this can create an object if no object for $key
     * exists.
     *
     * @param string  $key      The key string.
     * @param int     $value    Amount to increment.
     * @param int     $initial  The initial object value for creation.
     * @param int     $expire   Expiration time for creation.
     * @param bool    $quiet    TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function incr2($key, $value, $initial, $expire = 0, $quiet = FALSE) {}

    /**
     * Send `Decrement` or `DecrementQ` command to decrement an object's value.
     *
     * Decrements an object's value.  If the object data is not numeric,
     * this command will fail.
     *
     * @param string  $key    The key string.
     * @param int     $value  Amount to decrement.
     * @param bool    $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function decr($key, $value, $quiet = FALSE) {}

    /**
     * Send `Decrement` or `DecrementQ` command to decrement an object's value.
     *
     * Like `decr()`, but this can create an object if no object for $key
     * exists.
     *
     * @param string  $key      The key string.
     * @param int     $value    Amount to decrement.
     * @param int     $initial  The initial object value for creation.
     * @param int     $expire   Expiration time for creation.
     * @param bool    $quiet    TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function decr2($key, $value, $initial, $expire = 0, $quiet = FALSE) {}

    /**
     * Send `Append` or `AppendQ` command to append data.
     *
     * @param string      $key    The key string.
     * @param int|string  $data   Data to be appended.
     * @param bool        $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function append($key, $data, $quiet = FALSE) {}

    /**
     * Send `Prepend` or `PrependQ` command to append data.
     *
     * @param string      $key    The key string.
     * @param int|string  $data   Data to be prepended.
     * @param bool        $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function prepend($key, $data, $quiet = FALSE) {}

    /**
     * Send `Delete` or `DeleteQ` command to delete an object.
     *
     * @param string  $key    The key string.
     * @param bool    $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function delete($key, $quiet = FALSE) {}

    /**
     * Send `Lock` or `LockQ` command to lock an object.
     *
     * This command tries to acquire lock for the object.
     * If another client has already locked the object, this command
     * will immediately fail and send an error response back.
     *
     * @param string  $key    The key string.
     * @param bool    $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function lock($key, $quiet = FALSE) {}

    /**
     * Send `Unlock` or `UnlockQ` command to unlock an object.
     *
     * @param string  $key    The key string.
     * @param bool    $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function unlock($key, $quiet = FALSE) {}

    /**
     * Send `UnlockAll` or `UnlockAllQ` command to unlock all locks.
     *
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function unlockAll($quiet = FALSE) {}

    /**
     * Send `Flush` or `FlushQ` command to flush all objects.
     *
     * If non-zero $delay is specified, flush will delay for that seconds.
     *
     * @param int   $delay  Delay in seconds.
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function flush($delay = 0, $quiet = FALSE) {}

    /**
     * Send `Stat` to retrieve general statictics of the server.
     *
     * The server will return a series of response packets.
     * The series ends with a response data with null `key` property.
     *
     * @return int  The serial number of the request.
     */
    public function statGeneral() {}

    /**
     * Send `Stat` to retrieve settings of the server.
     *
     * The server will return a series of response packets.
     * The series ends with a response data with null `key` property.
     *
     * @return int  The serial number of the request.
     */
    public function statSettings() {}

    /**
     * Send `Stat` to retrieve item statistics of the server.
     *
     * The server will return a series of response packets.
     * The series ends with a response data with null `key` property.
     *
     * @return int  The serial number of the request.
     */
    public function statItems() {}

    /**
     * Send `Stat` to retrieve size statistics of the server.
     *
     * The server will return a series of response packets.
     * The series ends with a response data with null `key` property.
     *
     * @return int  The serial number of the request.
     */
    public function statSizes() {}

    /**
     * Send `Keys` to retrieve matching keys.
     *
     * *The keys extension is only available for yrmcds server!*.
     * memcached does not support the extension so far.
     *
     * The server will return a series of response packets.
     * The series ends with a response data with null `key` property.
     *
     * @see https://github.com/cybozu/yrmcds/blob/master/docs/keys.md
     * @param string  $prefix  If not NULL, only keys with the given prefix will be fetched.
     * @return int  The serial number of the request.
     */
    public function keys($prefix = NULL) {}

    /**
     * Send `Version` to retrieve the server version.
     *
     * @return int  The serial number of the request.
     */
    public function version() {}

    /**
     * Send `Quit` or `QuitQ` to ask the server closes the connection.
     *
     * This command effectively invalidates the connection even though
     * it is persistent.
     *
     * @param bool  $quiet  TRUE to suppress a successful response.
     * @return int  The serial number of the request.
     */
    public function quit($quiet = FALSE) {}
}


/**
 * Exception for \yrmcds\Client.
 *
 * Instances bring a meaningful message and an error code.
 *
 * @see http://www.php.net/manual/en/class.runtimeexception.php
 */
class Error extends \RuntimeException {}


/**
 * Response from memcached / yrmcds server.
 *
 * This class represents a command response from the server.
 *
 * These properties are always set:
 *
 * * $status
 * * $serial
 * * $length
 * * $command
 *
 * Other properties may or may not be set depending on the
 * command and status.
 */
class Response {
    /**
     * The server response status code.
     *
     * The value should be one of `\yrmcds\STATUS_*` constant.
     * Values other than `\yrmcds\STATUS_OK` indicate an error.
     *
     * @var int
     */
    public $status;

    /**
     * The serial number of the request.
     *
     * @var int
     */
    public $serial;

    /**
     * The response packet size in bytes.
     *
     * @var int
     */
    public $length;

    /**
     * The requested command code.
     *
     * The value should be one of `\yrmcds\CMD_*` constant.
     *
     * @var int
     */
    public $command;

    /**
     * An object's current CAS value.
     *
     * This property is available only for successfull get/set commands.
     *
     * @var int
     */
    public $cas_unique;

    /**
     * An object's flags.
     *
     * This property is available only for successfull get commands.
     *
     * @var int
     */
    public $flags;

    /**
     * Key.
     *
     * This property is available only for successfull getk commands
     * and statistics commands.
     *
     * @var string
     */
    public $key;

    /**
     * Raw byte data.
     *
     * This property is available only for successfull get commands
     * and statistics commands.
     *
     * @var string
     */
    public $data;

    /**
     * Numeric object value.
     *
     * This property is available only for successful incr/decr commands.
     *
     * @var int
     */
    public $value;
}
