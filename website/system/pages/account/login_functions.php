<?php
/**
 * Shared login completion used by the regular login flow and by the 2FA
 * second step, so both paths behave identically (session, flags, hooks).
 *
 * @package   MyAAC
 */

defined('MYAAC') or die('Direct access not allowed!');

/**
 * @param object $account    OTS_Account
 * @param string $pwhash     encrypt()-ed password hash (same value stored in accounts.password)
 * @param bool   $remember_me
 * @param bool   $want_admin whether the login form asked for the admin panel
 * @return bool false when the account requested admin but has no admin flag
 */
function myaac_complete_login($account, string $pwhash, bool $remember_me, bool $want_admin): bool
{
	global $logged, $logged_flags, $hooks;

	session_regenerate_id();
	setSession('account', $account->getId());
	setSession('password', $pwhash);
	if ($remember_me) {
		setSession('remember_me', true);
	}

	$logged = true;
	$logged_flags = $account->getWebFlags();

	if ($want_admin && !admin()) {
		unsetSession('account');
		unsetSession('password');
		unsetSession('remember_me');
		$logged = false;
		return false;
	}

	$account->setCustomField('web_lastlogin', time());
	$hooks->trigger(HOOK_LOGIN, array('account' => $account, 'remember_me' => $remember_me));
	return true;
}
