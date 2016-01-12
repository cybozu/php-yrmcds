--TEST--
Check for \yrmcds\Client for decr.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();

$serial = $c->decr($key, 2);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_DECREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_NOTFOUND ) die('status is not ok');
echo "decr failed (expected)\n";

$serial = $c->decr2($key, 10, 100);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_DECREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "decr2 ok\n";

$serial = $c->decr($key, 2);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_DECREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "decr ok\n";

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( ((int)$r->data) !== 98 ) die('data mismatch');
echo "get ok\n";
--EXPECT--
decr failed (expected)
decr2 ok
decr ok
get ok
