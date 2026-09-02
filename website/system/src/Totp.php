<?php
/**
 * TOTP (RFC 6238) implementation: SHA-1, 30s period, 6 digits, window +-1.
 * Server-side only; codes are verified with constant-time comparison.
 *
 * @package   MyAAC
 */

namespace MyAAC;

defined('MYAAC') or die('Direct access not allowed!');

class Totp
{
	const PERIOD = 30;
	const DIGITS = 6;
	const WINDOW = 1;
	const BASE32 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';

	public static function generateSecret(int $bytes = 20): string
	{
		return self::base32Encode(random_bytes($bytes));
	}

	public static function otpauthUri(string $secret, string $accountName, string $issuer): string
	{
		return 'otpauth://totp/' . rawurlencode($issuer) . ':' . rawurlencode($accountName)
			. '?secret=' . $secret
			. '&issuer=' . rawurlencode($issuer)
			. '&algorithm=SHA1&digits=' . self::DIGITS . '&period=' . self::PERIOD;
	}

	public static function verify(string $secret, string $code, ?int $time = null): bool
	{
		$code = preg_replace('/\s+/', '', $code);
		if (!preg_match('/^\d{' . self::DIGITS . '}$/', $code)) {
			return false;
		}

		$time = $time ?? time();
		$counter = intdiv($time, self::PERIOD);

		for ($i = -self::WINDOW; $i <= self::WINDOW; $i++) {
			$expected = self::code($secret, $counter + $i);
			if ($expected !== null && hash_equals($expected, $code)) {
				return true;
			}
		}

		return false;
	}

	public static function code(string $secret, int $counter): ?string
	{
		$key = self::base32Decode($secret);
		if ($key === null || $key === '') {
			return null;
		}

		$hash = hash_hmac('sha1', pack('J', $counter), $key, true);
		$offset = ord(substr($hash, -1)) & 0x0F;
		$value = ((ord($hash[$offset]) & 0x7F) << 24)
			| ((ord($hash[$offset + 1]) & 0xFF) << 16)
			| ((ord($hash[$offset + 2]) & 0xFF) << 8)
			| (ord($hash[$offset + 3]) & 0xFF);

		return str_pad((string) ($value % (10 ** self::DIGITS)), self::DIGITS, '0', STR_PAD_LEFT);
	}

	public static function base32Encode(string $data): string
	{
		$bits = '';
		foreach (str_split($data) as $ch) {
			$bits .= str_pad(decbin(ord($ch)), 8, '0', STR_PAD_LEFT);
		}

		$out = '';
		foreach (str_split($bits, 5) as $chunk) {
			$out .= self::BASE32[bindec(str_pad($chunk, 5, '0'))];
		}

		return $out;
	}

	public static function base32Decode(string $b32): ?string
	{
		$b32 = strtoupper(preg_replace('/[^A-Za-z2-7]/', '', $b32));
		if ($b32 === '') {
			return null;
		}

		$bits = '';
		for ($i = 0; $i < strlen($b32); $i++) {
			$pos = strpos(self::BASE32, $b32[$i]);
			if ($pos === false) {
				return null;
			}
			$bits .= str_pad(decbin($pos), 5, '0', STR_PAD_LEFT);
		}

		$out = '';
		foreach (str_split($bits, 8) as $chunk) {
			if (strlen($chunk) === 8) {
				$out .= chr(bindec($chunk));
			}
		}

		return $out;
	}
}
