--TEST--
Check for \yrmcds\Client for fetching keys.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
$c = get_client();
$key = gen_key();

$c->flush(0, TRUE);
sleep(2);

$serial = $c->set($key, 'abc');
$r = $c->recv();
if( $r->serial !== $serial ) die('serial mismatch');
if( $r->command !== \yrmcds\CMD_SET ) die('command mismatch');
if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
echo "set ok\n";

$serial = $c->keys();
while( TRUE ) {
    $r = $c->recv();
    if( $r->serial !== $serial ) die('serial mismatch');
    if( $r->command !== \yrmcds\CMD_KEYS ) die('command mismatch');
    if( $r->status !== \yrmcds\STATUS_OK ) die('status is not ok');
    if( is_null($r->key) ) break;
    echo("KEY: {$r->key}\n");
}
--EXPECTF--
set ok
KEY: %s
