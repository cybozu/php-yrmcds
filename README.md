memcached / yrmcds extension for PHP5
=====================================

This PHP extension provides an object-oriented interface of [libyrmcds][],
a full-featured [memcached][] / [yrmcds][] client library.

[yrmcds][] is a complete rewrite of memcached that supports replication
and the [server-side locking][locking]. [libyrmcds][] is a companion
client library for yrmcds.

Thanks to yrmcds' server-side locking, this extension can provide a
stable object locking mechanism when combined with [yrmcds][].

Features
--------

* Access to all memcached / yrmcds binary protocol commands.
* Persistent connections.
* Auto prefix keys.
* Transparent LZ4 compression for large objects.
* Asynchronous operations.

As a bonus, a [session handler](yrmcds_session.php) utilizing yrmcds'
server-side locking is also available.

Requirements
------------

* PHP 5.4 or better.
* Linux.

Install
-------

Follow [the common steps][phpize]:

    $ cd php-yrmcds
    $ phpize
    $ ./configure --with-php-config=/path/to/your/php-config
    $ make
    $ sudo make install

Usage
-----

See [USAGE.md](USAGE.md).

[libyrmcds]: http://cybozu.github.io/libyrmcds/
[memcached]: http://memcached.org/
[yrmcds]: http://cybozu.github.io/yrmcds/
[locking]: https://github.com/cybozu/yrmcds/blob/master/docs/locking.md
[phpize]: http://www.php.net/manual/en/install.pecl.phpize.php
