<?php

/**
 * Session handler using yrmcds extension.
 *
 * @author Yamamoto, Hirotaka
 * @version 1.0.2
 * @copyright (C) 2013 Cybozu.
 * @license 2-clause BSD
 * @package session
 */
namespace yrmcds;

/**
 * This exception will be thrown if \yrmcds\SessionHandler
 * cannot acquire the session lock.
 */
class TimeoutException extends \RuntimeException {}

/**
 * A session handler class based on \yrmcds\Client .
 *
 * This class requires PHP5.4+ because this implements
 * \SessionHandlerInterface available since PHP5.4.
 *
 * This class only works with yrmcds because server-side locking
 * is not implemented in memcached.
 *
 * To set the session handler, pass an instace of this class
 * to `session_set_save_handler()`.
 *
 * @see http://www.php.net/manual/en/function.session-set-save-handler.php
 */
class SessionHandler implements \SessionHandlerInterface {
    /**
     * @internal
     */
    private $client;
    /**
     * @internal
     */
    private $expire;
    /**
     * @internal
     */
    private $timeout;
    /**
     * @internal
     */
    private $locked;

    /**
     * Constructor.
     *
     * Inactive sessions will expire in $expire seconds.
     *
     * If $timeout is 0, sessions will not be locked.
     *
     * If $timeout > 0, starting a session may throw \yrmcds\TimeoutException
     * if the client cannot acquire the lock within $timeout seconds.
     *
     * @param \yrmcds\Client  $client   Connection to session storage.
     * @param int             $expire   Session expiration time.
     * @param int             $timeout  Lock wait timeout seconds.
     */
     public function __construct(\yrmcds\Client $client,
                                 $expire = 3600, $timeout = 6) {
        $this->client = $client;
        $this->expire = $expire;
        $this->timeout = $timeout;
        $this->locked = FALSE;
    }

    /**
     * @internal
     */
    public function open($savePath, $sessionName) {
        return TRUE;
    }

    /**
     * @internal
     */
    public function close() {
        // \yrmcds\Client will automatically unlock session objects.
        return TRUE;
    }

    /**
     * @internal
     */
    private function wait_lock($id) {
        $start = time();
        while( TRUE ) {
            $serial = $this->client->lockGet($id);
            while( TRUE ) {
                $r = $this->client->recv();
                if( $r->serial == $serial )
                    break;
            }
            if( $r->status == \yrmcds\STATUS_LOCKED ) {
                if( (time() - $start) > $this->timeout )
                    throw new TimeoutException("Session lock timeout");
                time_nanosleep(0, 50000000); // 50 msec
                continue;
            }
            return $r;
        }
    }

    /**
     * @internal
     */
    public function read($id) {
        if( $this->timeout != 0 ) {
            $r = $this->wait_lock($id);
            if( $r->status == \yrmcds\STATUS_OK ) {
                $this->locked = TRUE;
                $this->client->touch($id, $this->expire);
                $this->client->recv();
            }
        } else {
            $serial = $this->client->getTouch($id, $this->expire);
            while( TRUE ) {
                $r = $this->client->recv();
                if( $r->serial == $serial )
                    break;
            }
        }
        return (string)$r->data;
    }

    /**
     * @internal
     */
    public function write($id, $data) {
        try {
            if( strlen($data) == 0 ) {
                $this->client->delete($id, TRUE);
                return TRUE;
            }
            if( $this->locked ) {
                $this->client->replaceUnlock(
                    $id, $data, 0, $this->expire, TRUE);
            } else {
                $this->client->set($id, $data, 0, $this->expire, 0, TRUE);
            }
            return TRUE;
        } catch( \Exception $e ) {
            return FALSE;
        }
    }

    /**
     * @internal
     */
    public function destroy($id) {
        $this->client->delete($id, TRUE);
        return TRUE;
    }

    /**
     * @internal
     */
    public function gc($maxlifetime) {
        return TRUE;
    }
}
