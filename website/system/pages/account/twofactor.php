<?php
/**
 * Two-factor authentication, second step.
 * Only reachable right after a valid first step (pending session flag);
 * direct access redirects to the login/manage page.
 *
 * @package   MyAAC
 */

use MyAAC\RateLimit;
use MyAAC\Totp;
use MyAAC\TwoFactor;

defined('MYAAC') or die('Direct access not allowed!');

require __DIR__ . '/login_functions.php';

$title = 'Two-Factor Authentication';

$pending = TwoFactor::getPending();
if ($pending === null) {
	header('Location: ' . getLink('account/manage'));
	exit;
}

csrfProtect();

$errors = [];
if (isset($_POST['code'])) {
	$limiter = new RateLimit('twofactor', 5, 15);
	$limiter->enabled = true;
	$limiter->load();
	$ip = get_browser_real_ip();

	if ($limiter->exceeded($ip)) {
		$errors[] = 'Too many attempts. Please try again in a few minutes.';
	}
	else {
		$secret = TwoFactor::secretFor($pending['account']);
		if ($secret !== null && Totp::verify($secret, (string) $_POST['code'])) {
			$account = new OTS_Account();
			$account->load($pending['account']);
			if ($account->isLoaded()) {
				if (!empty($_POST['trust'])) {
					TwoFactor::trustDevice($pending['account'], $_SERVER['HTTP_USER_AGENT'] ?? '');
				}

				TwoFactor::clearPending();
				if (myaac_complete_login($account, $pending['pwhash'], $pending['remember'], $pending['admin'])) {
					header('Location: ' . getLink($pending['admin'] ? 'admin' : 'account/manage'));
				}
				else {
					header('Location: ' . getLink('account/manage'));
				}
				exit;
			}
		}

		// generic message: do not leak whether the account or the code failed
		$limiter->increment($ip);
		$errors[] = 'Invalid or expired code.';
	}
}

$twig->display('account.twofactor.html.twig', array('errors' => $errors));
