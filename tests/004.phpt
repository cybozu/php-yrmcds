--TEST--
Check for \yrmcds\Client construction (persistent connection).
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
get_client('hoge');
echo 'ok';
--EXPECT--
ok
