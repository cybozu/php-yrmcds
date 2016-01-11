--TEST--
Check for \yrmcds\Client for deletion.
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

$serial = $c->delete($key);
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_DELETE ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "delete ok\n";

$c->get($key, TRUE);  // ignore missing key.
$serial = $c->noop(); // ack the server a reply.
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_NOOP ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo 'get ignores missing key';
--EXPECT--
set ok
delete ok
get ignores missing key
