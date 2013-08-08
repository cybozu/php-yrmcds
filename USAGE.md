Usage
=====

Create a \yrmcds\Client instance
--------------------------------

First of all, you must create a client object as follows:

```php
$client = new \yrmcds\Client("server", 11211);
```

The above code creates a client connected to memcached or yrmcds
running on "server" on port 11211.  The connection is not persistent;
it will be closed when this object is deleted.

Create a client with persistent connection
------------------------------------------

To keep the connection between requests, specify a _persistent ID_
in the third parameter of the constructor:

```php
$client = new \yrmcds\Client('server', 11211, 'server');
```

As shown, using the hostname as the persistent ID is recommended.

Although a persistent connection will not be closed at a request end,
these finalizations are done to make it stable:

* All server-side locks acquired by this client are released.
* Floating response packets are received and discarded.
* The socket timeout value is reset to the default.

Auto prefixing keys
-------------------

In many cases, a memcached client need to prefix a constant string
to all keys.  By specifying a non-empty string in the fourth parameter
of the constructor, the client will automatically prefix that string
to keys.

For instance, the following code will store an object with `prefix:foo` key:

```php
$client = new \yrmcds\Client('server', 11211, NULL, 'prefix:');
$client->set('foo', 'some data');
```

Request serial number
---------------------

Every command sending method in `\yrmcds\Client` does _not_ wait the
response from the server.  Instead, it returns a serial number of the
issued request.

To receive a response for a command, use `\yrmcds\Client::recv()`:

```php
$serial = $client->set('foo', 'some data');
echo $serial, PHP_EOL; // prints the serial number of the set command.

$r = $client->recv();
var_dump( $r );        // $r is an instance of \yrmcds\Response
```

Quiet commands
--------------

Most commands have _quiet_ mutations.  For instance, the quiet mutation
of `get` command can be issued by passing `TRUE` to `$quiet` parameter:

```php
$client->get($key, TRUE);
```

The server does not send a response for these quiet mutations when the
command succeeds, i.e. when the response status code would be
`\yrmcds\STATUS_OK`.

In addition, the server does not send a response for `get` commands
when the response code would be `\yrmcds\STATUS_NOTFOUND`, i.e. when the
named object does not exist in the server.

Multi get
---------

Combining the asynchronous commands and quiet mutations, multi-get
can be implemented efficiently:

```php
$client->get($key1, TRUE);
$client->get($key2, TRUE);
$client->get($key3, TRUE);
...
$client->get($keyn, TRUE);
$serial = $client->noop();  // noop is guaranteed to return a response.

while( TRUE ) {
    $r = $client->recv();
    if( $r->serial == $serial )
        break;              // got the response for noop.

    var_dump( $r );         // dump retrieved objects.
}
```

Retrieving statistics
---------------------

For `statGeneral()`, `statSettings()`, `statItems()` and `statSizes()`
commands, the server returns a series of response packets.  The series
ends with a response with no `key` property.

This code shows the general server statistics:

```php
$client->statGeneral();
while( TRUE ) {
    $r = $client->recv();
    if( ! property_exists($r, 'key') )
        break;
    echo $r->key, ': ', $r->data, PHP_EOL;
}
```

Errors
------

Errors are notified in these 3 ways:

1. Fatal PHP error

    is triggered when an API is used illegally.

2. `\yrmcds\Error` exception

    is thrown when a network problem happens.

3. The response status code in a `\yrmcds\Response` instance

    brings the result of an issued command.

For instance, `lock()` command may fail with `\yrmcds\STATUS_LOCKED`
status code if the object is already locked by another client:

```php
$client->lock($key);

$r = $client->recv();
if( $r->status == \yrmcds\STATUS_LOCKED ) {
    // the object has been locked by another client.
}
```

Session handler
---------------

`yrmcds_session.php` provides a session handler class utilizing the
server-side locking mechanism of yrmcds.

Compared to the [widely used client-side locking technique][1], the
server-side locking is far more robust.

Anyways, this is how to use the session handler:

```php
$client = new \yrmcds\Client('server');
$handler = new \yrmcds\SessionHandler($client);
session_set_save_handler( $handler );

session_start();
```

The expiration time of inactive sessions and the timeout of acquiring
locks can be configured by specifying the second and the third parameter
of `\yrmcds\SessionHandler` constructor:

```php
// inactive sessions will expire in 600 seconds.
$handler = new \yrmcds\SessionHandler($client, 600);

// lock timeout is set 30 seconds.
$handler = new \yrmcds\SessionHandler($client, 3600, 30);

// session lock is disabled.
$handler = new \yrmcds\SessionHandler($client, 3600, 0);
```

`\yrmcds\SessionHandler` requires PHP 5.4 or better and, of course, yrmcds.

[1]: http://www.regexprn.com/2010/05/using-memcached-as-distributed-locking.html
