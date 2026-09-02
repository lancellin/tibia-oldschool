<?php
/**
 * 2FA support: encrypted TOTP secret at rest, trusted-device tokens and
 * the short-lived "pending" login state used between step 1 and step 2.
 *
 * Security model:
 * - The TOTP secret is stored encrypted (AES-256-GCM) with a server key that
 *   lives in config.local.php, never in the database and never sent to the
 *   client after activation.
 * - Trusted devices hold only a SHA-256 hash of a random 256-bit token; the
 *   raw token exists only in the HttpOnly cookie. The DB leak does not yield
 *   usable cookies.
 * - The cookie is HttpOnly + SameSite=Lax (+ Secure when served over HTTPS).
 * - IP / User-Agent are never used as authentication factors (UA is stored
 *   only as a cosmetic label).
 *
 * @package   MyAAC
 */

namespace MyAAC;

defined('MYAAC') or die('Direct access not allowed!');

class TwoFactor
{
	const COOKIE = 'myaac_trusted_device';
	const DEVICE_TTL = 2592000; // 30 days
	const PENDING_TTL = 300;    // 5 minutes

	// ------------------------------------------------------------------
	// server key (config.local.php: $config['totp_key'] = 64 hex chars)
	// ------------------------------------------------------------------
	public static function key(): ?string
	{
		global $config;
		$hex = $config['totp_key'] ?? null;
		if (!is_string($hex) || $hex === '') {
			return null;
		}

		$raw = hex2bin($hex);
		return ($raw !== false && strlen($raw) === 32) ? $raw : null;
	}

	public static function available(): bool
	{
		return self::key() !== null;
	}

	// ------------------------------------------------------------------
	// secret at rest
	// ------------------------------------------------------------------
	public static function encryptSecret(string $plain): ?string
	{
		$key = self::key();
		if ($key === null) {
			return null;
		}

		$nonce = random_bytes(12);
		$tag = '';
		$ct = openssl_encrypt($plain, 'aes-256-gcm', $key, OPENSSL_RAW_DATA, $nonce, $tag);
		if ($ct === false) {
			return null;
		}

		return base64_encode($nonce . $tag . $ct);
	}

	public static function decryptSecret(string $blob): ?string
	{
		$key = self::key();
		if ($key === null) {
			return null;
		}

		$raw = base64_decode($blob, true);
		if ($raw === false || strlen($raw) < 29) {
			return null;
		}

		$nonce = substr($raw, 0, 12);
		$tag = substr($raw, 12, 16);
		$ct = substr($raw, 28);

		$plain = openssl_decrypt($ct, 'aes-256-gcm', $key, OPENSSL_RAW_DATA, $nonce, $tag);
		return ($plain === false) ? null : $plain;
	}

	/**
	 * Returns the decrypted TOTP secret for the account, or null when 2FA
	 * is not enabled (or the column/schema is missing).
	 *
	 * @param object|int $account OTS_Account or account id
	 */
	public static function secretFor($account): ?string
	{
		global $db;

		if (!$db->hasColumn('accounts', 'totp_secret')) {
			return null;
		}

		$id = is_object($account) ? $account->getId() : (int) $account;
		$res = $db->query('SELECT `totp_secret` FROM `accounts` WHERE `id` = ' . $id);
		if (!$res || $res->rowCount() === 0) {
			return null;
		}

		$row = $res->fetch();
		if (empty($row['totp_secret'])) {
			return null;
		}

		return self::decryptSecret($row['totp_secret']);
	}

	public static function setSecret($account, string $secretBase32): bool
	{
		global $db;
		$blob = self::encryptSecret($secretBase32);
		if ($blob === null) {
			return false;
		}

		$id = is_object($account) ? $account->getId() : (int) $account;
		$db->query('UPDATE `accounts` SET `totp_secret` = ' . $db->quote($blob) . ' WHERE `id` = ' . $id);
		return true;
	}

	public static function clearSecret($account): void
	{
		global $db;
		$id = is_object($account) ? $account->getId() : (int) $account;
		$db->query('UPDATE `accounts` SET `totp_secret` = NULL WHERE `id` = ' . $id);
	}

	// ------------------------------------------------------------------
	// pending login state (between step 1 and step 2)
	// ------------------------------------------------------------------
	public static function setPending(int $accountId, bool $remember, bool $wantAdmin, string $pwhash): void
	{
		setSession('2fa_account', $accountId);
		setSession('2fa_time', time());
		setSession('2fa_remember', $remember ? 1 : 0);
		setSession('2fa_admin', $wantAdmin ? 1 : 0);
		setSession('2fa_pwhash', $pwhash);
	}

	public static function getPending(): ?array
	{
		$id = (int) getSession('2fa_account');
		$time = (int) getSession('2fa_time');
		$pwhash = getSession('2fa_pwhash');
		if ($id <= 0 || $time <= 0 || empty($pwhash) || (time() - $time) > self::PENDING_TTL) {
			self::clearPending();
			return null;
		}

		return [
			'account' => $id,
			'remember' => (bool) getSession('2fa_remember'),
			'admin' => (bool) getSession('2fa_admin'),
			'pwhash' => $pwhash,
		];
	}

	public static function clearPending(): void
	{
		unsetSession('2fa_account');
		unsetSession('2fa_time');
		unsetSession('2fa_remember');
		unsetSession('2fa_admin');
		unsetSession('2fa_pwhash');
	}

	// ------------------------------------------------------------------
	// trusted devices
	// ------------------------------------------------------------------
	public static function verifyTrustedCookie(int $accountId): bool
	{
		global $db;

		$token = $_COOKIE[self::COOKIE] ?? '';
		if ($token === '' || !$db->hasTable('myaac_trusted_devices')) {
			return false;
		}

		$hash = hash('sha256', $token);
		$res = $db->query('SELECT `id`, `expires_at` FROM `myaac_trusted_devices` WHERE `token_hash` = ' . $db->quote($hash) . ' AND `account_id` = ' . $accountId);
		if (!$res || $res->rowCount() === 0) {
			return false;
		}

		$row = $res->fetch();
		if ((int) $row['expires_at'] < time()) {
			self::revokeDevice($accountId, (int) $row['id']);
			self::clearCookie();
			return false;
		}

		$db->query('UPDATE `myaac_trusted_devices` SET `last_used_at` = ' . time() . ' WHERE `id` = ' . (int) $row['id']);
		return true;
	}

	public static function trustDevice(int $accountId, string $label): void
	{
		global $db;

		$token = bin2hex(random_bytes(32));
		$now = time();
		$label = mb_substr(trim($label) !== '' ? trim($label) : 'device', 0, 120);

		$db->query('INSERT INTO `myaac_trusted_devices` (`account_id`, `token_hash`, `label`, `created_at`, `expires_at`, `last_used_at`) '
			. 'VALUES (' . $accountId . ', ' . $db->quote(hash('sha256', $token)) . ', ' . $db->quote($label) . ', ' . $now . ', ' . ($now + self::DEVICE_TTL) . ', ' . $now . ')');

		self::setCookie($token, $now + self::DEVICE_TTL);
	}

	public static function revokeDevice(int $accountId, int $deviceId): void
	{
		global $db;
		$db->query('DELETE FROM `myaac_trusted_devices` WHERE `id` = ' . (int) $deviceId . ' AND `account_id` = ' . $accountId);
	}

	public static function revokeAll(int $accountId): void
	{
		global $db;
		$db->query('DELETE FROM `myaac_trusted_devices` WHERE `account_id` = ' . $accountId);
	}

	public static function listDevices(int $accountId): array
	{
		global $db;
		$out = [];
		$res = $db->query('SELECT `id`, `label`, `created_at`, `expires_at`, `last_used_at` FROM `myaac_trusted_devices` WHERE `account_id` = ' . $accountId . ' ORDER BY `created_at` DESC');
		if ($res) {
			foreach ($res as $row) {
				$out[] = $row;
			}
		}

		return $out;
	}

	// ------------------------------------------------------------------
	// cookie
	// ------------------------------------------------------------------
	protected static function setCookie(string $token, int $expires): void
	{
		$https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off');
		setcookie(self::COOKIE, $token, [
			'expires' => $expires,
			'path' => '/',
			'httponly' => true,
			'samesite' => 'Lax',
			'secure' => $https,
		]);
	}

	public static function clearCookie(): void
	{
		$https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off');
		setcookie(self::COOKIE, '', [
			'expires' => time() - 3600,
			'path' => '/',
			'httponly' => true,
			'samesite' => 'Lax',
			'secure' => $https,
		]);
	}
}
