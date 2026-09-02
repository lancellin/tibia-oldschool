<?php
/**
 * Self-hosted captcha (GD). Validation is always server-side, one-shot,
 * with 5 minute expiry. The code lives only in the PHP session.
 *
 * @package   MyAAC
 */

namespace MyAAC;

defined('MYAAC') or die('Direct access not allowed!');

class Captcha
{
	const CODE_KEY = 'captcha_code';
	const TIME_KEY = 'captcha_time';
	const TTL = 300; // seconds
	const LENGTH = 5;
	// no ambiguous glyphs (0/O, 1/I/L)
	const CHARSET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';

	public static function generate(): string
	{
		$code = '';
		$max = strlen(self::CHARSET) - 1;
		for($i = 0; $i < self::LENGTH; $i++) {
			$code .= self::CHARSET[random_int(0, $max)];
		}

		setSession(self::CODE_KEY, $code);
		setSession(self::TIME_KEY, time());
		return $code;
	}

	/**
	 * One-shot verification: the stored code is consumed on every call,
	 * so a captcha cannot be replayed across attempts.
	 */
	public static function verify(string $input): bool
	{
		$code = getSession(self::CODE_KEY);
		$time = (int) getSession(self::TIME_KEY);

		unsetSession(self::CODE_KEY);
		unsetSession(self::TIME_KEY);

		if(empty($code)) {
			return false;
		}

		if(time() - $time > self::TTL) {
			return false;
		}

		return strtoupper(trim($input)) === $code;
	}

	public static function render(): void
	{
		$code = self::generate();

		$width = 150;
		$height = 50;
		$img = imagecreatetruecolor($width, $height);
		$bg = imagecolorallocate($img, 245, 245, 240);
		imagefill($img, 0, 0, $bg);

		for($i = 0; $i < 5; $i++) {
			$color = imagecolorallocate($img, random_int(140, 200), random_int(140, 200), random_int(140, 200));
			imageline($img, random_int(0, $width), random_int(0, $height), random_int(0, $width), random_int(0, $height), $color);
		}

		for($i = 0; $i < 120; $i++) {
			$color = imagecolorallocate($img, random_int(120, 220), random_int(120, 220), random_int(120, 220));
			imagesetpixel($img, random_int(0, $width - 1), random_int(0, $height - 1), $color);
		}

		$x = 12;
		for($i = 0; $i < strlen($code); $i++) {
			$color = imagecolorallocate($img, random_int(20, 110), random_int(20, 110), random_int(20, 110));
			imagechar($img, 5, $x, random_int(8, 20), $code[$i], $color);
			$x += 26;
		}

		header('Content-Type: image/png');
		header('Cache-Control: no-store, no-cache, must-revalidate');
		header('Pragma: no-cache');
		imagepng($img);
		imagedestroy($img);
	}
}
