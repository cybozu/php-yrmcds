--TEST--
Check for \yrmcds\Client for incr.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();

$serial = $c->incr($key, 2);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_INCREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_NOTFOUND ) die('status is not ok');
echo "incr failed (expected)\n";

$serial = $c->incr2($key, 10, 100);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_INCREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "incr2 ok\n";

$serial = $c->incr($key, 2);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_INCREMENT ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "incr ok\n";

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( ((int)$r->data) !== 102 ) die('data mismatch');
echo "get ok\n";
--EXPECT--
incr failed (expected)
incr2 ok
incr ok
get ok
