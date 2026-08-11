local function sendLine(player, message)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, message)
end

local pendingCleanSaveEvent = nil
local pendingCleanSaveAt = 0
local pendingCleanSaveFloorReset = false

local function runCleanSave(resetFloorSnapshots)
	pendingCleanSaveEvent = nil
	pendingCleanSaveAt = 0
	pendingCleanSaveFloorReset = false
	if not Game.beginFloorPersistenceCleanSave(resetFloorSnapshots == true) then
		print(string.format(
			"[Error - FloorSnapshot] Scheduled %s could not be started or committed.",
			resetFloorSnapshots and "weekly floor reset" or "clean save"
		))
	end
end

local function boolText(value)
	return value and "yes" or "no"
end

local function trim(value)
	return value:match("^%s*(.-)%s*$")
end

local function cleanSaveRecoveryBlock()
	local plan = Game.getFloorRecoveryPlan()
	if plan.mode == "RECOVERY_BLOCKED" then
		return string.format(
			"Clean save is blocked while floor recovery mode is %s. Recovery must be applied and confirmed first.",
			plan.mode
		)
	end
	if plan.mode == "CRASH_RECOVERY" and not plan.confirmationCompleted then
		return "Clean save is blocked until the applied crash recovery receives a durable stage 5.6 confirmation."
	end
	if plan.mode == "CLEAN_RESTART" and not plan.applyCompleted then
		return "Clean save is blocked because the automatic clean restart floor replay did not complete."
	end
	if plan.dryRunEvaluated and not plan.dryRunReady then
		return "Clean save is blocked because the stage 5 recovery dry-run found an unsafe recovery plan."
	end
	return nil
end

local function parsePosition(player, value)
	value = trim(value):lower()
	if value == "" or value == "front" then
		local position = player:getPosition()
		position:getNextPosition(player:getDirection())
		return position
	end
	if value == "here" then
		return player:getPosition()
	end

	local x, y, z = value:match("^(%d+)%s*[, ]%s*(%d+)%s*[, ]%s*(%d+)$")
	if not x then
		return nil
	end
	return Position(tonumber(x), tonumber(y), tonumber(z))
end

local function timeText(timestamp)
	if not timestamp or timestamp == 0 then
		return "never"
	end
	return os.date("%d/%m/%Y %H:%M:%S", timestamp)
end

local function showStatus(player)
	local stats = Game.getFloorSnapshotStats()
	sendLine(player, string.format(
		"Floor snapshots stage=3 shadow=%s world=%d generation=%d policy=%d dirty=%d in_flight=%d groups=%d. Replay disabled.",
		boolText(stats.enabled), stats.worldId, stats.generationId, stats.policyVersion,
		stats.dirtyTiles, stats.inFlight, stats.checkpointGroups
	))
	sendLine(player, string.format(
		"Runtime queued=%d succeeded=%d failed=%d serialize_failed=%d stale=%d last_serialize=%dus last_success=%s.",
		stats.queued, stats.succeeded, stats.failed, stats.serializationFailed,
		stats.staleCompletions, stats.lastSerializationMicros, timeText(stats.lastSuccessAt)
	))
	sendLine(player, string.format(
		"Coordinated created=%d merged=%d committed=%d failed=%d stuck_alerts=%d players_saved=%d houses_saved=%d tiles_saved=%d session=%.0f/%s.",
		stats.checkpointGroupsCreated, stats.checkpointGroupsMerged,
		stats.checkpointGroupsSucceeded, stats.checkpointGroupsFailed,
		stats.checkpointStuckAlerts,
		stats.checkpointPlayersSaved, stats.checkpointHousesSaved, stats.checkpointTilesSaved,
		stats.saveSessionId, stats.saveSessionState
	))
	sendLine(player, string.format(
		"Database available=%s rows=%d bytes=%d last_update=%s simulated_failures=%d error=%s",
		boolText(stats.databaseAvailable), stats.databaseRows, stats.databaseBytes,
		timeText(stats.databaseLastUpdatedAt), stats.simulatedFailuresRemaining,
		stats.lastError ~= "" and stats.lastError or (stats.databaseError ~= "" and stats.databaseError or "-")
	))
end

local function showRecovery(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=4 mode=%s evaluated=%s database=%s source=%.0f/%s newest_before_start=%.0f ignored_empty=%d replay=%s apply=%s.",
		plan.mode, boolText(plan.evaluated), boolText(plan.databaseAvailable),
		plan.sourceSessionId, plan.sourceState, plan.newestSessionId, plan.ignoredEmptySessions,
		boolText(plan.replayEnabled), boolText(plan.applyEnabled)
	))
	sendLine(player, string.format(
		"Source players=%d tiles=%d snapshot_rows=%d checkpoints=%d committed=%d clean=%d/%d committed_at=%s.",
		plan.sourcePlayerCount, plan.sourceTileCount, plan.sourceSessionSnapshotRows,
		plan.sourceCheckpointCount, plan.sourceCommittedCheckpointCount,
		plan.sourceCleanCheckpointCount, plan.sourceCleanCheckpointTileCount,
		timeText(plan.sourceCommittedAt)
	))
	sendLine(player, string.format(
		"Materialized rows=%d valid=%d invalid=%d items=%d top=%d bytes=%d city_filtered=%d death_bundle=%d corpses=%d validation=%dus.",
		plan.snapshotRows, plan.validRows, plan.invalidRows, plan.itemCount,
		plan.topItemCount, plan.serializedBytes, plan.cityFilteredRows,
		plan.deathBundleCount, plan.playerCorpseCount, plan.validationMicros
	))
	sendLine(player, string.format(
		"Validation format=%d policy=%d size=%d checksum=%d blob=%d counters=%d identity_rows=%d missing=%d invalid=%d.",
		plan.formatMismatchRows, plan.policyMismatchRows, plan.sizeMismatchRows,
		plan.checksumMismatchRows, plan.blobInvalidRows, plan.counterMismatchRows,
		plan.identityProblemRows, plan.identityMissingCount, plan.identityInvalidCount
	))
	sendLine(player, "Decision: " .. (plan.reason ~= "" and plan.reason or "-") )
	if plan.sourceError ~= "" then
		sendLine(player, "Source error: " .. plan.sourceError)
	end
	if plan.validationError ~= "" then
		sendLine(player, "Validation error: " .. plan.validationError)
	end
end

local function showDryRun(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=5 dry_run evaluated=%s ready=%s mode=%s source=%.0f rows=%d/%d decode=%dus automatic_apply=no explicit_available=%s.",
		boolText(plan.dryRunEvaluated), boolText(plan.dryRunReady), plan.mode,
		plan.sourceSessionId, plan.dryRunRows, plan.snapshotRows, plan.dryRunMicros,
		boolText(plan.applyEnabled)
	))
	sendLine(player, string.format(
		"Decision items=%d top=%d restore=%d (top=%d) quarantine=%d (top=%d) rejected=%d (top=%d).",
		plan.dryRunItemCount, plan.dryRunTopItemCount,
		plan.dryRunRestoreItemCount, plan.dryRunRestoreTopItemCount,
		plan.dryRunQuarantineItemCount, plan.dryRunQuarantineTopItemCount,
		plan.dryRunRejectedItemCount, plan.dryRunRejectedTopItemCount
	))
	sendLine(player, string.format(
		"Policy always=%d food=%d clean_only=%d death_bundle=%d containers=%d max_depth=%d identities=%d duplicates=%d.",
		plan.dryRunPersistAlwaysCount, plan.dryRunPersistFoodCount,
		plan.dryRunPersistCleanOnlyCount, plan.dryRunDeathBundleCount,
		plan.dryRunContainerCount, plan.dryRunMaxDepth,
		plan.dryRunIdentityCount, plan.dryRunDuplicateIdentityCount
	))
	if plan.applyCompleted then
		sendLine(player, "The dry-run itself is read-only; the explicit stage 5.5 map apply has since completed.")
	else
		sendLine(player, "No item was placed on the map. Pending quarantine materialization is reported separately by /floorsnapshot quarantine.")
	end
	if plan.dryRunError ~= "" then
		sendLine(player, "Dry-run error: " .. plan.dryRunError)
	end
end

local function showReconciliation(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=5.3 reconciliation evaluated=%s ready=%s candidates=%d decoded=%d invalid=%d false_positive=%d scan=%dus.",
		boolText(plan.reconciliationEvaluated), boolText(plan.reconciliationReady),
		plan.reconciliationCandidateRows, plan.reconciliationDecodedRows,
		plan.reconciliationInvalidRows, plan.reconciliationFalsePositiveRows,
		plan.reconciliationMicros
	))
	sendLine(player, string.format(
		"Player identities total=%d unique=%d duplicates=%d inventory=%d locker=%d depot=%d inbox=%d store_inbox=%d.",
		plan.playerIdentityCount, plan.playerUniqueIdentityCount,
		plan.playerDuplicateIdentityCount, plan.inventoryIdentityCount,
		plan.depotLockerIdentityCount, plan.depotIdentityCount,
		plan.inboxIdentityCount, plan.storeInboxIdentityCount
	))
	sendLine(player, string.format(
		"Floor identities=%d floor_only=%d player_matches=%d ambiguous=%d suppress_on_apply=%d.",
		plan.dryRunIdentityCount, plan.floorOnlyIdentityCount,
		plan.floorPlayerIdentityMatchCount, plan.floorPlayerAmbiguousIdentityCount,
		plan.floorPlayerIdentityMatchCount
	))
	sendLine(player, "Read-only: player blobs were not changed. Houses and market are outside this reconciliation policy.")
	if plan.reconciliationFirstMatch ~= "" then
		sendLine(player, "First match: " .. plan.reconciliationFirstMatch)
	end
	if plan.reconciliationError ~= "" then
		sendLine(player, "Reconciliation error: " .. plan.reconciliationError)
	end
end

local function showQuarantine(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=5.4 quarantine evaluated=%s ready=%s mode=%s source=%.0f planned_rows=%d persisted_rows=%d materialize=%dus.",
		boolText(plan.quarantineEvaluated), boolText(plan.quarantineReady), plan.mode,
		plan.sourceSessionId, plan.quarantinePlannedRows,
		plan.quarantinePersistedRows, plan.quarantineMicros
	))
	sendLine(player, string.format(
		"Planned stackables=%d player_matches=%d source_snapshot_items=%d bytes=%d.",
		plan.quarantineStackableItemCount, plan.quarantinePlayerMatchItemCount,
		plan.quarantineSnapshotItemCount, plan.quarantineSerializedBytes
	))
	sendLine(player, string.format(
		"Persisted active stackables=%d player_matches=%d bytes=%d state=PENDING.",
		plan.quarantinePersistedStackableItems,
		plan.quarantinePersistedPlayerMatches,
		plan.quarantinePersistedBytes
	))
	sendLine(player, "Reasons: bit 1=crash stackable; bit 2=player-storage identity match. Full source tile blobs are retained for context.")
	sendLine(player, "Quarantine materialization itself did not remove or restore any item; map apply requires a separate explicit confirmation.")
	if plan.quarantineError ~= "" then
		sendLine(player, "Quarantine error: " .. plan.quarantineError)
	end
end

local function showApply(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=5.5 apply available=%s evaluated=%s ready=%s completed=%s source=%.0f rows=%d tiles=%d preflight_apply=%dus.",
		boolText(plan.applyEnabled), boolText(plan.applyEvaluated),
		boolText(plan.applyReady), boolText(plan.applyCompleted),
		plan.sourceSessionId, plan.applyRows, plan.applyTargetTiles, plan.applyMicros
	))
	sendLine(player, string.format(
		"Policy restore=%d (top=%d); map restored=%d (top=%d); quarantine=%d; suppressed=%d (top=%d, direct_identity=%d).",
		plan.applyPolicyRestoreItemCount, plan.applyPolicyRestoreTopItemCount,
		plan.applyRestoredItemCount, plan.applyRestoredTopItemCount,
		plan.applyQuarantineItemCount, plan.applySuppressedItemCount,
		plan.applySuppressedTopItemCount, plan.applyDirectSuppressedIdentityCount
	))
	sendLine(player, string.format(
		"Crash recovery decay paused=%d item(s). Only recovered player corpses receive +50 minutes after confirmation.",
		plan.applyHeldDecayItemCount
	))
	if plan.applyCompleted then
		if plan.mode == "CLEAN_RESTART" then
			sendLine(player, "The clean restart snapshot was applied automatically before login. No recovery confirmation is required.")
		elseif plan.confirmationCompleted then
			sendLine(player, "Recovered items were added beside OTBM items and the recovery is durably confirmed. Common-player login is allowed.")
		else
			sendLine(player, "Recovered items were added beside OTBM items without movement scripts or player dirty events. Common-player login remains blocked.")
		end
	else
		sendLine(player, string.format(
			"No map item was applied by stage 5.5. To apply the selected source explicitly, use /floorsnapshot apply confirm %.0f.",
			plan.sourceSessionId
		))
	end
	if plan.applyError ~= "" then
		sendLine(player, "Apply error: " .. plan.applyError)
	end
end

local function showConfirmation(player)
	local plan = Game.getFloorRecoveryPlan()
	sendLine(player, string.format(
		"Floor recovery stage=5.6 confirmation evaluated=%s ready=%s completed=%s source=%.0f apply_session=%.0f record=%.0f validation=%dus.",
		boolText(plan.confirmationEvaluated), boolText(plan.confirmationReady),
		boolText(plan.confirmationCompleted), plan.confirmationSourceSessionId,
		plan.confirmationApplySessionId, plan.confirmationRecordId,
		plan.confirmationMicros
	))
	sendLine(player, string.format(
		"Settled quarantine rows=%d items=%d player_matches=%d confirmed_by=%s/%d at=%s.",
		plan.confirmationPendingQuarantineRows,
		plan.confirmationPendingQuarantineItems,
		plan.confirmationPendingPlayerMatches,
		plan.confirmationPlayerName ~= "" and plan.confirmationPlayerName or "-",
		plan.confirmationPlayerGuid, timeText(plan.confirmationConfirmedAt)
	))
	sendLine(player, string.format(
		"Recovery decay resumed=%d; player_corpses_extended_50m=%d; removed_while_paused=%d.",
		plan.confirmationResumedDecayItemCount,
		plan.confirmationExtendedPlayerCorpseCount,
		plan.confirmationRemovedHeldDecayItemCount
	))
	if plan.confirmationCompleted then
		sendLine(player, "The database confirmation committed successfully. Common-player login and clean save are allowed in this server process.")
	elseif plan.mode == "CLEAN_RESTART" and plan.applyCompleted then
		sendLine(player, "The clean restart replay completed automatically. Stage 5.6 confirmation applies only to crash recovery.")
	elseif plan.mode == "CLEAN_RESTART" then
		sendLine(player, "The automatic clean restart replay did not complete. Common-player login and clean save remain blocked.")
	elseif plan.applyCompleted then
		sendLine(player, string.format(
			"Map apply is complete but still blocked. After inspecting the result, use /floorsnapshot recoveryconfirm %.0f.",
			plan.sourceSessionId
		))
	else
		sendLine(player, "Nothing was confirmed. Stage 5.5 map apply must complete in this same server process first.")
	end
	if plan.confirmationError ~= "" then
		sendLine(player, "Confirmation error: " .. plan.confirmationError)
	end
end

local function showPosition(player, position)
	local result = Game.verifyFloorSnapshot(position)
	sendLine(player, string.format(
		"Snapshot %d,%d,%d row=%s dirty=%s in_flight=%s stored_version=%.0f dirty_version=%.0f updated=%s.",
		position.x, position.y, position.z, boolText(result.rowFound), boolText(result.dirty),
		boolText(result.inFlight), result.storedTileVersion, result.dirtyTileVersion,
		timeText(result.storedUpdatedAt)
	))
	if not result.rowFound then
		sendLine(player, "No stored row is available for this world/generation. " .. (result.error or ""))
		return
	end
	sendLine(player, string.format(
		"Stored items=%d top=%d bytes=%d checksum=%s blob=%s; live items=%d top=%d bytes=%d valid=%s match=%s.",
		result.storedItemCount, result.storedTopItemCount, result.storedBytes,
		boolText(result.storedChecksumValid), boolText(result.storedBlobValid),
		result.liveItemCount, result.liveTopItemCount, result.liveBytes,
		boolText(result.liveSnapshotValid), boolText(result.matchesLive)
	))
	sendLine(player, string.format(
		"Checksums stored=%s live=%s error=%s",
		result.storedChecksum ~= "" and result.storedChecksum or "-",
		result.liveChecksum ~= "" and result.liveChecksum or "-",
		result.error ~= "" and result.error or "-"
	))
end

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end
	if player:getAccountType() < ACCOUNT_TYPE_GOD then
		return false
	end

	local action = trim(param):lower()
	if action == "" or action == "status" then
		showStatus(player)
		return false
	end

	if action == "recovery" then
		showRecovery(player)
		return false
	end

	if action == "dryrun" then
		showDryRun(player)
		return false
	end

	if action == "reconcile" then
		showReconciliation(player)
		return false
	end

	if action == "quarantine" then
		showQuarantine(player)
		return false
	end

	if action == "apply" then
		showApply(player)
		return false
	end

	if action == "confirmation" then
		showConfirmation(player)
		return false
	end

	local applySource = action:match("^apply%s+confirm%s+(%d+)$")
	if applySource then
		applySource = tonumber(applySource)
		sendLine(player, string.format(
			"Refreshing recovery validation and applying source %.0f. This command cannot be repeated in the same server process.",
			applySource
		))
		local applied = Game.applyFloorRecovery(applySource)
		if applied then
			local appliedPlan = Game.getFloorRecoveryPlan()
			if appliedPlan.mode == "CLEAN_RESTART" then
				sendLine(player, "Clean restart map apply completed. Login and clean save are allowed without recovery confirmation.")
			else
				sendLine(player, "Floor recovery map apply completed. Login and clean save remain blocked until a later confirmation stage.")
			end
		else
			sendLine(player, "Floor recovery map apply was refused. Review the apply status and error below.")
		end
		showApply(player)
		return false
	end

	local confirmationSource = action:match("^recoveryconfirm%s+(%d+)$")
	if confirmationSource then
		confirmationSource = tonumber(confirmationSource)
		sendLine(player, string.format(
			"Confirming inspected recovery source %.0f. The decision will be committed to the database before access is released.",
			confirmationSource
		))
		local confirmed = Game.confirmFloorRecovery(
			confirmationSource, player:getGuid(), player:getName())
		if confirmed then
			sendLine(player, "Floor recovery confirmation committed. Common-player login and clean save are now allowed.")
		else
			sendLine(player, "Floor recovery confirmation was refused. Access remains blocked.")
		end
		showConfirmation(player)
		return false
	end

	if action == "flush" then
		local queued = Game.flushFloorSnapshots()
		sendLine(player, string.format(
			"Forced one protected snapshot batch: tiles=%d. Coordinated groups commit synchronously; isolated tiles remain asynchronous.",
			queued
		))
		return false
	end

	if action == "cleansave status" then
		if not pendingCleanSaveEvent then
			sendLine(player, "No delayed clean save is scheduled.")
		else
			local remaining = math.max(0, pendingCleanSaveAt - os.time())
			sendLine(player, string.format(
				"A %s is scheduled in approximately %d second(s).",
				pendingCleanSaveFloorReset and "weekly floor reset" or "clean save", remaining
			))
		end
		return false
	end

	if action == "cleansave cancel" then
		if not pendingCleanSaveEvent then
			sendLine(player, "No delayed clean save is scheduled.")
		else
			stopEvent(pendingCleanSaveEvent)
			pendingCleanSaveEvent = nil
			pendingCleanSaveAt = 0
			pendingCleanSaveFloorReset = false
			sendLine(player, "The delayed clean save was cancelled.")
		end
		return false
	end

	local weeklyResetDelay = action:match("^weeklyreset%s+(%d+)%s+confirm$")
	if weeklyResetDelay then
		local recoveryBlock = cleanSaveRecoveryBlock()
		if recoveryBlock then
			sendLine(player, recoveryBlock)
			return false
		end
		weeklyResetDelay = tonumber(weeklyResetDelay)
		if weeklyResetDelay < 5 or weeklyResetDelay > 3600 then
			sendLine(player, "The weekly floor reset delay must be between 5 and 3600 seconds.")
			return false
		end
		if pendingCleanSaveEvent then
			sendLine(player, "A delayed clean save is already scheduled. Use 'cleansave status' or 'cleansave cancel'.")
			return false
		end

		pendingCleanSaveAt = os.time() + weeklyResetDelay
		pendingCleanSaveFloorReset = true
		pendingCleanSaveEvent = addEvent(runCleanSave, weeklyResetDelay * 1000, true)
		Game.broadcastMessage(string.format(
			"A weekly floor reset has been scheduled in %d second(s). All persisted floor items will be removed. Please logout.",
			weeklyResetDelay
		), MESSAGE_STATUS_WARNING)
		sendLine(player, string.format(
			"Weekly floor reset scheduled in %d second(s). This will atomically empty every persisted non-house floor snapshot.",
			weeklyResetDelay
		))
		return false
	end

	local cleanSaveDelay = action:match("^cleansave%s+(%d+)$")
	if cleanSaveDelay then
		local recoveryBlock = cleanSaveRecoveryBlock()
		if recoveryBlock then
			sendLine(player, recoveryBlock)
			return false
		end
		cleanSaveDelay = tonumber(cleanSaveDelay)
		if cleanSaveDelay < 5 or cleanSaveDelay > 3600 then
			sendLine(player, "The clean save delay must be between 5 and 3600 seconds.")
			return false
		end
		if pendingCleanSaveEvent then
			sendLine(player, "A delayed clean save is already scheduled. Use 'cleansave status' or 'cleansave cancel'.")
			return false
		end

		pendingCleanSaveAt = os.time() + cleanSaveDelay
		pendingCleanSaveFloorReset = false
		pendingCleanSaveEvent = addEvent(runCleanSave, cleanSaveDelay * 1000, false)
		Game.broadcastMessage(string.format(
			"A clean server save has been scheduled in %d second(s). Please logout.", cleanSaveDelay
		), MESSAGE_STATUS_WARNING)
		sendLine(player, string.format("Clean save scheduled in %d second(s).", cleanSaveDelay))
		return false
	end

	if action == "cleansave" then
		local recoveryBlock = cleanSaveRecoveryBlock()
		if recoveryBlock then
			sendLine(player, recoveryBlock)
			return false
		end
		if pendingCleanSaveEvent then
			sendLine(player, "A delayed clean save is already scheduled. Cancel it before starting an immediate clean save.")
			return false
		end
		sendLine(player, "Starting the clean coordinated save. Everyone will be disconnected and login stays blocked until restart.")
		-- Run after the talk action returns, because the operation also removes
		-- the GOD who issued the command.
		addEvent(runCleanSave, 100, false)
		return false
	end

	local failureCount = action:match("^failnext%s*(%d*)$")
	if failureCount then
		failureCount = tonumber(failureCount) or 1
		local armed = Game.simulateFloorSnapshotFailures(failureCount)
		sendLine(player, string.format(
			"The next %d snapshot write(s) will fail before reaching the database, exercising retry logic.",
			armed
		))
		return false
	end

	local position = parsePosition(player, action)
	if position then
		showPosition(player, position)
		return false
	end

	sendLine(player, "Use /floorsnapshot status, recovery, dryrun, reconcile, quarantine, apply, apply confirm <source>, confirmation, recoveryconfirm <source>, front, here, x,y,z, flush, failnext [1..100], cleansave [5..3600], weeklyreset [5..3600] confirm, cleansave status or cleansave cancel.")
	return false
end
