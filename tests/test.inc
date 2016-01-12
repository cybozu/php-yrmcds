<?php // -*- mode: php -*-

function get_client($persist_id = NULL, $prefix = NULL) {
    return new \yrmcds\Client(getenv('YRMCDS_SERVER'), 11211,
                              $persist_id, $prefix);
}

function gen_key() {
    return uniqid('', TRUE);
}
