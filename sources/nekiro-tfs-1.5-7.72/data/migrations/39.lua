function onUpdateDatabase()
	print("> Updating database to version 39 (Alchemy skill rate 2.5x)")
	db.query([[
		UPDATE `players`
		SET `alchemy_tries` = LEAST(
			FLOOR((50 * POW(1.1, GREATEST(`alchemy_level`, 10) - 10)) / 2.5) - 1,
			FLOOR(
				`alchemy_tries` *
				FLOOR((50 * POW(1.1, GREATEST(`alchemy_level`, 10) - 10)) / 2.5) /
				FLOOR(50 * POW(1.1, GREATEST(`alchemy_level`, 10) - 10))
			)
		)
		WHERE `alchemy_tries` > 0;
	]])
	return true
end
