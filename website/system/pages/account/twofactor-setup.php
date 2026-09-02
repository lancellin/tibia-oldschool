<?php
/**
 * Two-factor authentication enrollment and trusted-device management.
 * Logged-in area. The TOTP secret is shown only during setup (session-held)
 * and only activated after a valid code is confirmed.
 *
 * @package   MyAAC
 */

use MyAAC\Totp;
use MyAAC\TwoFactor;

defined('MYAAC') or die('Direct access not allowed!');

$title = 'Two-Factor Authentication Setup';
require PAGES . 'account/base.php';

if (!$logged) {
	return;
}

csrfProtect();

$errors = [];
$notice = '';

$account_id = $account_logged->getId();
$enabled = TwoFactor::secretFor($account_id) !== null;

$action = $_POST['action'] ?? '';
$code = (string) ($_POST['code'] ?? '');

if ($action === 'begin') {
	if ($enabled) {
		$errors[] = 'Two-factor authentication is already enabled.';
	}
	elseif (!TwoFactor::available()) {
		$errors[] = 'The server is not configured for two-factor authentication. Contact the administrator.';
	}
	else {
		setSession('2fa_setup_secret', Totp::generateSecret());
		header('Location: ' . getLink('account/twofactor-setup'));
		exit;
	}
}
elseif ($action === 'cancel_setup') {
	unsetSession('2fa_setup_secret');
}
elseif ($action === 'confirm') {
	$setup_secret = getSession('2fa_setup_secret');
	if (!$setup_secret) {
		$errors[] = 'Setup session expired. Please start again.';
	}
	elseif (!Totp::verify($setup_secret, $code)) {
		$errors[] = 'Invalid code. Enter the current 6-digit code shown in your authenticator app.';
	}
	elseif (!TwoFactor::setSecret($account_id, $setup_secret)) {
		$errors[] = 'Could not enable two-factor authentication. Contact the administrator.';
	}
	else {
		unsetSession('2fa_setup_secret');
		TwoFactor::revokeAll($account_id);
		$enabled = true;
		$notice = 'Two-factor authentication is now enabled.';
	}
}
elseif ($action === 'disable') {
	if (!$enabled) {
		$errors[] = 'Two-factor authentication is not enabled.';
	}
	elseif (!Totp::verify(TwoFactor::secretFor($account_id) ?? '', $code)) {
		$errors[] = 'Invalid code. Enter the current 6-digit code shown in your authenticator app.';
	}
	else {
		TwoFactor::clearSecret($account_id);
		TwoFactor::revokeAll($account_id);
		TwoFactor::clearCookie();
		$enabled = false;
		$notice = 'Two-factor authentication has been disabled.';
	}
}
elseif ($action === 'revoke') {
	TwoFactor::revokeDevice($account_id, (int) ($_POST['device'] ?? 0));
	$notice = 'Trusted device removed.';
}
elseif ($action === 'revoke_all') {
	if ($enabled && !Totp::verify(TwoFactor::secretFor($account_id) ?? '', $code)) {
		$errors[] = 'Invalid code. Enter the current 6-digit code shown in your authenticator app.';
	}
	else {
		TwoFactor::revokeAll($account_id);
		TwoFactor::clearCookie();
		$notice = 'All trusted devices were removed.';
	}
}

$setup = null;
$setup_secret = getSession('2fa_setup_secret');
if ($setup_secret && !$enabled) {
	$setup = array(
		'secret' => $setup_secret,
		'uri' => Totp::otpauthUri($setup_secret, (string) $account_logged->getName(), configLua('serverName')),
	);
}

$devices = $enabled ? TwoFactor::listDevices($account_id) : array();

$twig->display('account.twofactor.setup.html.twig', array(
	'errors' => $errors,
	'notice' => $notice,
	'enabled' => $enabled,
	'setup' => $setup,
	'devices' => $devices,
));
