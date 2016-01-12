--TEST--
Check for \yrmcds\Client for locking.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$c2 = get_client();
$key = gen_key();

$serial = $c->set($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->lock($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_LOCK ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "lock ok\n";

$serial = $c2->lockGet($key);
$r = $c2->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_LAG ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_LOCKED ) die('status is not ok');
echo "lockGet failed (expected)\n";

$serial = $c->replaceUnlock($key, 'xyz');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_RAU ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "replaceUnlock ok\n";

$serial = $c2->lockGetk($key);
$r = $c2->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_LAGK ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->key !== $key ) die('key mismatch');
if( $r->data !== 'xyz' ) die('data mismatch');
echo "lockGetk ok\n";

$serial = $c2->unlock($key);
$r = $c2->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_UNLOCK ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "unlock ok\n";
--EXPECT--
set ok
lock ok
lockGet failed (expected)
replaceUnlock ok
lockGetk ok
unlock ok