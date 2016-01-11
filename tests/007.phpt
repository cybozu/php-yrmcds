--TEST--
Check for \yrmcds\Client for auto prefixing.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$prefix = 'prefix-';
$c = get_client(NULL, $prefix);
$key = gen_key();
$serial = $c->set($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->getk($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GETK ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->key !== $prefix.$key ) die('key mismatch');
if( $r->data !== 'abc' ) die('data mismatch');
echo 'get ok';
--EXPECT--
set ok
get ok
