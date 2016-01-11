--TEST--
Check for \yrmcds\Client with SET and GET.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();
$serial = $c->set($key, 'abc', 11);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->data !== 'abc' ) die('data mismatch');
if( $r->flags != 11 ) die('flags mismatch');
echo "get ok\n";

$serial = $c->getk($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GETK ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->key !== $key ) die('key mismatch');
if( $r->data !== 'abc' ) die('data mismatch');
if( $r->flags != 11 ) die('flags mismatch');
echo 'getk ok';
--EXPECT--
set ok
get ok
getk ok
