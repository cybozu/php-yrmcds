--TEST--
Check for \yrmcds\Client construction.
--SKIPIF--
<?php
require('skipif.inc');
--FILE--
<?php
require('test.inc');
get_client();
echo 'ok';
--EXPECT--
ok
