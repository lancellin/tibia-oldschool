<?php
/**
 * Captcha image endpoint (GET /captcha).
 *
 * @package   MyAAC
 */

use MyAAC\Captcha;

defined('MYAAC') or die('Direct access not allowed!');

Captcha::render();
exit;
