--TEST--
Check for \yrmcds\Client for expiration.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();
$serial = $c->set($key, 'abc', 0, 1);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

sleep(2);

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_NOTFOUND ) die('status is not ok');
echo "get failed (expected)\n";

$key = gen_key();
$serial = $c->set($key, 'abc', 0, 2);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->touch($key, 0);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_TOUCH ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "touch ok\n";

sleep(3);

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo 'get ok';
--EXPECT--
set ok
get failed (expected)
set ok
touch ok
get ok
