--TEST--
Check for \yrmcds\Client with NOOP.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$serial = $c->noop();
if( !is_int($serial) ) die('Non integer return value');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_NOOP ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo 'ok';
--EXPECT--
ok
