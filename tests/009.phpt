--TEST--
Check for \yrmcds\Client for append and prepend.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();
$serial = $c->set($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->append($key, 'xyz');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_APPEND ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "append ok\n";

$serial = $c->prepend($key, '012');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_PREPEND ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "prepend ok\n";

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->data !== '012abcxyz' ) die('data mismatch');
echo 'get ok';
--EXPECT--
set ok
append ok
prepend ok
get ok
