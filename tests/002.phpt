--TEST--
Check for yrmcds binary command constants.
--SKIPIF--
<?php if( (! extension_loaded('yrmcds')) and (! dl('yrmcds.so')) )
          print "skip"; ?>
--FILE--
<?php
echo \yrmcds\CMD_LAGKQ;
?>
--EXPECT--
73
