--TEST--
Check for \yrmcds\Client for add and replace.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();
$serial = $c->replace($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_REPLACE ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_NOTFOUND ) die('status is not ok');
echo "failed to replace (expected)\n";

$serial = $c->add($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_ADD ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "add ok\n";

$serial = $c->add($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_ADD ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_EXISTS ) die('status is not ok');
echo "failed to add (expected)\n";

$serial = $c->replace($key, 'def');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_REPLACE ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "replace ok\n";

$serial = $c->get($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_GET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
if( $r->data !== 'def' ) die('data mismatch');
echo 'get ok';
--EXPECT--
failed to replace (expected)
add ok
failed to add (expected)
replace ok
get ok
