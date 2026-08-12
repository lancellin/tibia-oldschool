function onUpdateDatabase()
	print("> Updating database to version 40 (Argon2id password storage)")
	db.query([[ALTER TABLE `accounts` MODIFY `password` VARCHAR(255) NOT NULL]])
	return true
end
