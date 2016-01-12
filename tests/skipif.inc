<?php // -*- mode: php -*-

if( !extension_loaded('yrmcds') ) die("skip");
if( getenv('YRMCDS_SERVER') === FALSE )
    die("skip no YRMCDS_SERVER environment variable");
