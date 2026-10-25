#include "g_flyingrespawn.h"
#include "sqlite3.h"

#include <boost/test/unit_test.hpp>

struct FlyingHistory {
    flyingRespawnState_t state = {};
    FlyingHistory() { G_FlyingRespawn_InitClient(&state); }
    ~FlyingHistory() { G_FlyingRespawn_FreeClient(&state); }
};

// Catches selecting the newest state instead of the last state preceding rewind.
BOOST_AUTO_TEST_CASE(flying_respawn_history_selects_by_server_time)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 0));
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time : {1000, 1100, 1200, 1300}) {
        movement.commandTime = time;
        movement.origin[0] = static_cast<float>(time);
        cmd.serverTime = time + 7; // selection uses authoritative server capture time
        cmd.forwardmove = 37;
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
        G_FlyingRespawn_RecordRawCmd(&h.state, time, &cmd);
    }
    flyingRespawnSelection_t selected = {};
    BOOST_REQUIRE(G_FlyingRespawn_SelectHistory(&h.state, 1250, 1300, &selected));
    BOOST_CHECK_EQUAL(selected.snapshot.serverTime, 1200);
    BOOST_CHECK_EQUAL(selected.snapshot.movement.origin[0], 1200);
    BOOST_CHECK_EQUAL(selected.commandCount, 1);
    flyingRespawnRawCmd_t raw = {};
    BOOST_REQUIRE(G_FlyingRespawn_GetSelectedCmd(&h.state, &selected, 0, &raw));
    BOOST_CHECK_EQUAL(raw.serverTime, 1300);
    BOOST_CHECK_EQUAL(raw.cmd.serverTime, 1307);
    BOOST_CHECK_EQUAL(raw.cmd.forwardmove, 37);
    BOOST_CHECK(!G_FlyingRespawn_SelectHistory(&h.state, 999, 1300, &selected));
}

// Catches ordinary play retaining a test-length segment or test mode truncating it.
BOOST_AUTO_TEST_CASE(flying_respawn_history_retention_and_test_replay)
{
    FlyingHistory ordinary, test;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&ordinary.state, 1500, 0, 0));
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&test.state, 1500, 0, 1));
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time = 0; time <= 31000; ++time) {
        cmd.serverTime = time;
        for (auto* s : {&ordinary.state, &test.state}) {
            G_FlyingRespawn_RecordRawCmd(s, time, &cmd);
            G_FlyingRespawn_RecordExecutionCmd(s, time, &cmd);
            G_FlyingRespawn_RecordState(s, time, &movement);
        }
    }
    flyingRespawnSelection_t selected = {};
    BOOST_CHECK(!G_FlyingRespawn_SelectHistory(&ordinary.state, 1000, 31000, &selected));
    BOOST_REQUIRE(G_FlyingRespawn_SelectHistory(&test.state, 1000, 31000, &selected));
    BOOST_CHECK_EQUAL(selected.commandCount, 30000);
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&ordinary.state, 31000, 1500));
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&test.state, 31000, 30001));
    BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&test.state, 31000, 30000));
    BOOST_CHECK(test.state.replay.active);
    BOOST_CHECK(test.state.replay.testReplay);
    BOOST_CHECK(test.state.replay.selection.executionHistory);
    BOOST_CHECK_EQUAL(test.state.replay.selection.commandCount, 30000);
    BOOST_CHECK_EQUAL(test.state.respawnCount, 0);
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&test.state, 1500, 0, 0));
    BOOST_CHECK(!test.state.replay.active);
    BOOST_CHECK(!G_FlyingRespawn_SelectHistory(&test.state, 1000, 31000, &selected));
}

// Catches accepting negative offsets, or losing valid configuration on rejection.
BOOST_AUTO_TEST_CASE(flying_respawn_history_rejects_negative_configuration)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 0));
    BOOST_CHECK(!G_FlyingRespawn_Configure(&h.state, -1, 0, 0));
    BOOST_CHECK(!G_FlyingRespawn_Configure(&h.state, 1500, -1, 0));
    BOOST_CHECK_EQUAL(h.state.rewindMs, 1500);
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 0, 0, 0));
}

// Catches mixing the secondary clock with race stats or failing to reset a checkpoint segment.
BOOST_AUTO_TEST_CASE(flying_respawn_history_secondary_clock_and_reset)
{
    FlyingHistory h;
    G_FlyingRespawn_ResetRun(&h.state, 1000);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 1250), 250);
    G_FlyingRespawn_ResetSecondarySegment(&h.state, 2000, 150);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 2100), 250);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 1900), 150);
    h.state.respawnCount = 4;
    h.state.runInvalid = qtrue;
    h.state.manualSave.valid = qtrue;
    h.state.automaticCheckpoint.valid = qtrue;
    BOOST_CHECK(G_FlyingRespawn_IsRunInvalid(&h.state));
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetRespawnCount(&h.state), 4);
    G_FlyingRespawn_ResetRun(&h.state, 3000);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 3050), 50);
    BOOST_CHECK(!G_FlyingRespawn_IsRunInvalid(&h.state));
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetRespawnCount(&h.state), 0);
    BOOST_CHECK(h.state.manualSave.valid); // separate manual owner survives run restart
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
}

struct FlyingDatabase {
    sqlite3* db = nullptr;
    FlyingDatabase() { BOOST_REQUIRE_EQUAL(sqlite3_open(":memory:", &db), SQLITE_OK); }
    ~FlyingDatabase() { sqlite3_close(db); }
};

// Catches a migration that rewrites historical invalid flags or fails on its second run.
BOOST_AUTO_TEST_CASE(flying_respawn_history_schema_migrates_historical_rows)
{
    FlyingDatabase f;
    BOOST_REQUIRE_EQUAL(sqlite3_exec(f.db, "CREATE TABLE LocalRun(id INTEGER PRIMARY KEY, invalid INTEGER, respawn_count_old INTEGER); INSERT INTO LocalRun VALUES(1, 2, 8)", nullptr, nullptr, nullptr), SQLITE_OK);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_HasRespawnCountColumn(f.db), 0);
    BOOST_REQUIRE_EQUAL(G_FlyingRespawn_MigrateLocalRun(f.db), SQLITE_OK);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_HasRespawnCountColumn(f.db), 1);
    BOOST_REQUIRE_EQUAL(G_FlyingRespawn_MigrateLocalRun(f.db), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    BOOST_REQUIRE_EQUAL(sqlite3_prepare_v2(f.db, "SELECT respawn_count, invalid FROM LocalRun WHERE id=1", -1, &stmt, nullptr), SQLITE_OK);
    BOOST_REQUIRE_EQUAL(sqlite3_step(stmt), SQLITE_ROW);
    BOOST_CHECK_EQUAL(sqlite3_column_int(stmt, 0), 0);
    BOOST_CHECK_EQUAL(sqlite3_column_int(stmt, 1), 2);
    sqlite3_finalize(stmt);
}

// Catches schema inspection accepting similarly named columns or erasing existing metadata.
BOOST_AUTO_TEST_CASE(flying_respawn_history_schema_preserves_existing_column)
{
    FlyingDatabase f;
    BOOST_REQUIRE_EQUAL(sqlite3_exec(f.db, "CREATE TABLE LocalRun(respawn_count INTEGER NOT NULL DEFAULT 0); INSERT INTO LocalRun VALUES(7)", nullptr, nullptr, nullptr), SQLITE_OK);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_HasRespawnCountColumn(f.db), 1);
    BOOST_REQUIRE_EQUAL(G_FlyingRespawn_MigrateLocalRun(f.db), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    BOOST_REQUIRE_EQUAL(sqlite3_prepare_v2(f.db, "SELECT respawn_count FROM LocalRun", -1, &stmt, nullptr), SQLITE_OK);
    BOOST_REQUIRE_EQUAL(sqlite3_step(stmt), SQLITE_ROW);
    BOOST_CHECK_EQUAL(sqlite3_column_int(stmt, 0), 7);
    sqlite3_finalize(stmt);
}

// Catches pruning commands between a sampled predecessor and the rewind boundary.
BOOST_AUTO_TEST_CASE(flying_respawn_history_keeps_commands_for_sampled_predecessor)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 0));
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time = 0; time <= 3050; ++time) {
        cmd.serverTime = time;
        G_FlyingRespawn_RecordRawCmd(&h.state, time, &cmd);
        if (time % 100 == 0) G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    flyingRespawnSelection_t selected = {};
    BOOST_REQUIRE(G_FlyingRespawn_SelectHistory(&h.state, 1550, 3050, &selected));
    BOOST_CHECK_EQUAL(selected.snapshot.serverTime, 1500);
    BOOST_CHECK_EQUAL(selected.commandCount, 1550);
}

// Catches manual bookmarks borrowing rolling storage or changing live run state.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_freezes_input_without_live_side_effects)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time : {1000, 1050, 1100}) {
        movement.commandTime = cmd.serverTime = time;
        movement.origin[0] = static_cast<float>(time);
        cmd.forwardmove = static_cast<signed char>((time - 1000) / 10);
        G_FlyingRespawn_RecordRawCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    h.state.respawnCount = 3;
    G_FlyingRespawn_ResetSecondarySegment(&h.state, 1000, 42);
    const auto generation = h.state.historyGeneration;
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_CHECK_EQUAL(h.state.historyGeneration, generation);
    BOOST_CHECK_EQUAL(h.state.commandCount, 3);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 3);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 1100), 142);
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!h.state.runInvalid);
    BOOST_CHECK_EQUAL(movement.origin[0], 1100);
    for (int time = 1101; time <= 3500; ++time) {
        cmd.serverTime = movement.commandTime = time;
        cmd.forwardmove = 99;
        G_FlyingRespawn_RecordRawCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    BOOST_CHECK(!G_FlyingRespawn_BeginManualReplay(&h.state, 4000, qfalse));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 4000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 1000);
    flyingRespawnRawCmd_t raw = {};
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 4049, &raw));
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 4050, &raw));
    BOOST_CHECK_EQUAL(raw.cmd.serverTime, 1050);
    BOOST_CHECK_EQUAL(raw.cmd.forwardmove, 5);
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 4100, &raw));
    BOOST_CHECK_EQUAL(raw.cmd.forwardmove, 10);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 3);
}

// Catches restoring the practice timer early, repeatedly, or from non-manual playback.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_handoff_is_practice_only_and_once)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = movement.commandTime = 1100;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1100, &cmd);
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    BOOST_CHECK(!G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qtrue)); // command must be simulated first
    flyingRespawnRawCmd_t raw = {};
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2100, &raw));
    BOOST_CHECK(!G_FlyingRespawn_FinishManualReplay(&h.state, 2099, qtrue));
    BOOST_CHECK(!G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qfalse));
    BOOST_REQUIRE(G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qtrue));
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qtrue));
}

// Catches discarding earlier commands received within the same server tick.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_keeps_same_tick_commands)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    for (int commandTime : {1010, 1020, 1030}) {
        cmd.serverTime = movement.commandTime = commandTime;
        G_FlyingRespawn_RecordRawCmd(&h.state, 1050, &cmd);
        G_FlyingRespawn_RecordExecutionCmd(&h.state, 1050, &cmd);
        G_FlyingRespawn_RecordState(&h.state, 1050, &movement);
    }
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    flyingRespawnRawCmd_t raw = {};
    for (int expected : {1010, 1020, 1030}) {
        BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2050, &raw));
        BOOST_CHECK_EQUAL(raw.cmd.serverTime, expected);
    }
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 2100, &raw));
}

// Catches incomplete movement/Force restoration or accidental official HUD timer writes.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_movement_roundtrip_keeps_timer_untouched)
{
    playerState_t original = {}, restored = {};
    original.origin[0] = 12;
    original.velocity[2] = 345;
    original.viewangles[1] = 90;
    original.delta_angles[1] = 42;
    original.commandTime = 1000;
    original.pm_flags = 17;
    original.pm_time = 25;
    original.groundEntityNum = 1023;
    original.fd.forcePower = 37;
    original.fd.forcePowersActive = 1 << FP_SPEED;
    original.fd.forcePowerDuration[FP_SPEED] = 1500;
    original.fd.forcePowerDebounce[FP_LEVITATION] = 1200;
    original.fd.forcePowerRegenDebounceTime = 1300;
    original.fd.forceJumpZStart = 91;
    original.fd.forceJumpCharge = 7;
    original.legsTimer = 23;
    original.lastOnGround = 900;
    original.duelTime = 123;
    restored.duelTime = 777;
    flyingRespawnMovement_t movement = {};
    G_FlyingRespawn_ReadMovement(&movement, &original, 4, 5, 6);
    G_FlyingRespawn_WriteMovement(&restored, &movement, 2000);
    BOOST_CHECK_EQUAL(restored.origin[0], 12);
    BOOST_CHECK_EQUAL(restored.velocity[2], 345);
    BOOST_CHECK_EQUAL(restored.viewangles[1], 90);
    BOOST_CHECK_EQUAL(restored.delta_angles[1], 42);
    BOOST_CHECK_EQUAL(restored.commandTime, 3000);
    BOOST_CHECK_EQUAL(restored.pm_flags, 17);
    BOOST_CHECK_EQUAL(restored.pm_time, 25);
    BOOST_CHECK_EQUAL(restored.groundEntityNum, 1023);
    BOOST_CHECK_EQUAL(restored.fd.forcePower, 37);
    BOOST_CHECK_EQUAL(restored.fd.forcePowersActive, 1 << FP_SPEED);
    BOOST_CHECK_EQUAL(restored.fd.forcePowerDuration[FP_SPEED], 3500);
    BOOST_CHECK_EQUAL(restored.fd.forcePowerDebounce[FP_LEVITATION], 3200);
    BOOST_CHECK_EQUAL(restored.fd.forcePowerRegenDebounceTime, 3300);
    BOOST_CHECK_EQUAL(restored.fd.forceJumpZStart, 91);
    BOOST_CHECK_EQUAL(restored.fd.forceJumpCharge, 7);
    BOOST_CHECK_EQUAL(restored.lastOnGround, 2900);
    BOOST_CHECK_EQUAL(restored.duelTime, 777);
    BOOST_CHECK_EQUAL(movement.movementStyle, 4);
    BOOST_CHECK_EQUAL(movement.checkpoints, 5);
    BOOST_CHECK_EQUAL(movement.courseID, 6);
}

// Catches savepos becoming unavailable immediately after spawn or with rewind zero.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_without_history_uses_saved_pose)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.origin[0] = 42;
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1000, &movement));
    G_FlyingRespawn_ResetRun(&h.state, 2000);
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 3000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 42);
    BOOST_CHECK(G_FlyingRespawn_FinishManualReplay(&h.state, 3000, qtrue));
}

// Catches positive-release captures losing their approach when the rolling ring expires.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_shared_capture_owns_pending_automatic_input)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = movement.commandTime = 1100;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1100, &cmd);
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureAutomaticStart(&h.state, 1100, &movement, 100, 2000));
    BOOST_CHECK(h.state.automaticCheckpoint.pending);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    for (int time = 1101; time <= 3100; ++time) {
        cmd.serverTime = movement.commandTime = time;
        G_FlyingRespawn_RecordRawCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    BOOST_REQUIRE(G_FlyingRespawn_CaptureAutomaticHandoff(&h.state, 3100, &movement));
    BOOST_CHECK(h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.start.serverTime, 1000);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.commandCount, 2001);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.commands[0].cmd.serverTime, 1100);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.commands[2000].cmd.serverTime, 3100);
    G_FlyingRespawn_ResetRun(&h.state, 4000);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(h.state.automaticCheckpoint.commands == nullptr);
}

// Catches stale pre-save movement replacing the saved pose when rewind is zero.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_zero_rewind_restores_exact_saved_pose)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 0, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.origin[0] = 10;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    movement.origin[0] = 42; // a map action since the last completed command
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1000, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 42);
    BOOST_CHECK(G_FlyingRespawn_FinishManualReplay(&h.state, 2000, qtrue));
}

// Catches reconfiguration discarding bookmarks, replay contaminating live history, or double frees.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_owner_survives_configuration_and_disposes_safely)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = movement.commandTime = 1100;
    cmd.forwardmove = 17;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1100, &cmd);
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 1));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    const auto generation = h.state.historyGeneration;
    cmd.forwardmove = 127;
    G_FlyingRespawn_RecordRawCmd(&h.state, 2100, &cmd);
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 2100, &cmd);
    G_FlyingRespawn_RecordState(&h.state, 2100, &movement);
    BOOST_CHECK_EQUAL(h.state.commandCount, 0);
    BOOST_CHECK_EQUAL(h.state.executionCount, 0);
    BOOST_CHECK_EQUAL(h.state.stateCount, 0);
    BOOST_CHECK_EQUAL(h.state.historyGeneration, generation);
    BOOST_CHECK(!G_FlyingRespawn_CaptureManual(&h.state, 2100, &movement));
    flyingRespawnRawCmd_t raw = {};
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2100, &raw));
    BOOST_CHECK_EQUAL(raw.cmd.forwardmove, 17);
    G_FlyingRespawn_FreeClient(&h.state);
    BOOST_CHECK(!h.state.manualSave.valid);
    BOOST_CHECK(h.state.manualSave.commands == nullptr);
    BOOST_CHECK(h.state.rawCommands == nullptr);
    BOOST_CHECK(h.state.executionCommands == nullptr);
    G_FlyingRespawn_FreeClient(&h.state);
}

// Catches losing movement generated by ForceClientUpdate when no packet arrives.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_replays_forced_execution_without_fabricating_receipts)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 400, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = 1107;
    cmd.forwardmove = 37;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1100, &cmd);
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    movement.commandTime = 1107;
    G_FlyingRespawn_RecordState(&h.state, 1100, &movement);
    cmd.serverTime = 1400; // ForceClientUpdate advances the last held input without a receipt
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1400, &cmd);
    movement.commandTime = 1400;
    G_FlyingRespawn_RecordState(&h.state, 1400, &movement);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1400, &movement));
    BOOST_CHECK_EQUAL(h.state.commandCount, 1);
    BOOST_CHECK_EQUAL(h.state.rawCommands[h.state.commandHead].cmd.serverTime, 1107);
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    flyingRespawnRawCmd_t step = {};
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2100, &step));
    BOOST_CHECK_EQUAL(step.cmd.serverTime, 1107);
    BOOST_CHECK_EQUAL(step.cmd.forwardmove, 37);
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 2399, &step));
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2400, &step));
    BOOST_CHECK_EQUAL(step.serverTime, 1400);
    BOOST_CHECK_EQUAL(step.cmd.serverTime, 1400);
    BOOST_CHECK_EQUAL(step.cmd.forwardmove, 37);
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 2400, &step));
    BOOST_CHECK(G_FlyingRespawn_FinishManualReplay(&h.state, 2400, qtrue));
}

// Catches replaying all synchronous receipts instead of the single coalesced server step.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_replays_only_coalesced_synchronous_execution)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, 0));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = 1048;
    cmd.forwardmove = 127;
    cmd.upmove = 127;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1050, &cmd);
    cmd.serverTime = 1068;
    cmd.forwardmove = -32;
    cmd.upmove = 0;
    cmd.angles[YAW] = 12345;
    G_FlyingRespawn_RecordRawCmd(&h.state, 1070, &cmd);
    cmd.serverTime = 1100; // G_RunClient uses the latest input at server frame time
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    movement.commandTime = 1100;
    G_FlyingRespawn_RecordState(&h.state, 1100, &movement);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_CHECK_EQUAL(h.state.commandCount, 2);
    BOOST_CHECK_EQUAL(h.state.rawCommands[h.state.commandHead].cmd.serverTime, 1048);
    BOOST_CHECK_EQUAL(h.state.rawCommands[h.state.commandHead + 1].cmd.serverTime, 1068);
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    flyingRespawnRawCmd_t step = {};
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 2099, &step));
    BOOST_REQUIRE(G_FlyingRespawn_NextManualCmd(&h.state, 2100, &step));
    BOOST_CHECK_EQUAL(step.cmd.serverTime, 1100);
    BOOST_CHECK_EQUAL(step.cmd.forwardmove, -32);
    BOOST_CHECK_EQUAL(step.cmd.upmove, 0);
    BOOST_CHECK_EQUAL(step.cmd.angles[YAW], 12345);
    BOOST_CHECK(!G_FlyingRespawn_NextManualCmd(&h.state, 2100, &step));
    BOOST_CHECK(G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qtrue));
}

// Catches treating absent/invalid mapper values as zero, or permitting negative release.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_mapper_settings)
{
    flyingRespawnCheckpointConfig_t config = {};
    G_FlyingRespawn_ParseCheckpointConfig(&config, nullptr, nullptr, nullptr);
    BOOST_CHECK(!config.disabled);
    BOOST_CHECK(!config.hasRewind);
    BOOST_CHECK(!config.hasRelease);
    G_FlyingRespawn_ParseCheckpointConfig(&config, "1", "2500", "375");
    BOOST_CHECK(config.disabled);
    BOOST_CHECK(config.hasRewind);
    BOOST_CHECK_EQUAL(config.rewindMs, 2500);
    BOOST_CHECK(config.hasRelease);
    BOOST_CHECK_EQUAL(config.releaseMs, 375);
    G_FlyingRespawn_ParseCheckpointConfig(&config, "0", "-1", "-400");
    BOOST_CHECK(!config.disabled);
    BOOST_CHECK(!config.hasRewind);
    BOOST_CHECK(config.hasRelease);
    BOOST_CHECK_EQUAL(config.releaseMs, 0);
    for (const char* invalid : {"", "12ms", "2147483648", "no"}) {
        G_FlyingRespawn_ParseCheckpointConfig(&config, nullptr, invalid, invalid);
        BOOST_CHECK(!config.hasRewind);
        BOOST_CHECK(!config.hasRelease);
    }
}

// Catches map overrides being pruned by shorter defaults and timer restart keeping old owners.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_run_defaults_and_map_retention)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 100, 50, 0, 400));
    BOOST_CHECK_EQUAL(h.state.rewindMs, 100);
    BOOST_CHECK_EQUAL(h.state.releaseOffsetMs, 50);
    BOOST_CHECK_EQUAL(h.state.retentionMs, 400);
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time : {1000, 1100, 1200, 1300, 1400, 1500}) {
        cmd.serverTime = movement.commandTime = time;
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1500, 20, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.start.serverTime, 1400);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 1550);
    G_FlyingRespawn_ParseCheckpointConfig(&config, "0", "400", "0");
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1500, 21, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.start.serverTime, 1100);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1500, &movement));
    h.state.respawnCount = 4;
    h.state.runInvalid = qtrue;
    h.state.replay.active = qtrue;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 2000, 200, 0, 0, 400));
    BOOST_CHECK_EQUAL(h.state.rewindMs, 200);
    BOOST_CHECK_EQUAL(h.state.releaseOffsetMs, 0);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    BOOST_CHECK(h.state.automaticCheckpoint.commands == nullptr);
    BOOST_CHECK(h.state.manualSave.valid);
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!h.state.runInvalid);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 0);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 2050), 50);
}

// Catches disabled, pending, or uncapturable later checkpoints falling back to older targets.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_later_touch_invalidates_without_fallback)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 0, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 20, &config, &movement));
    BOOST_CHECK(h.state.automaticCheckpoint.valid);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.checkpointEntityNum, 20);
    config.disabled = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 1200, 21, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    config.disabled = qfalse;
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1300, 22, &config, &movement));
    G_FlyingRespawn_ParseCheckpointConfig(&config, "0", "0", "100");
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1400, 23, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(h.state.automaticCheckpoint.pending);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1400);
    BOOST_CHECK(!G_FlyingRespawn_CaptureAutomaticHandoff(&h.state, 1499, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_CaptureAutomaticHandoff(&h.state, 1500, &movement));
    BOOST_CHECK(h.state.automaticCheckpoint.valid);
    G_FlyingRespawn_ParseCheckpointConfig(&config, "0", "500", "0");
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 1600, 24, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
}

// Catches replay capture/secondary recommits and applying pre-contact instead of post-contact progress.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_expected_contact_restores_only_progress)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 0, 0, 0));
    flyingRespawnCheckpointConfig_t config = {};
    flyingRespawnMovement_t movement = {};
    movement.checkpoints = 7;
    movement.courseID = 42;
    movement.origin[0] = 100;
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1200, 20, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.secondaryElapsedMs, 200);
    h.state.replay.active = qtrue;
    BOOST_CHECK(G_FlyingRespawn_IsExpectedCheckpointTouch(&h.state, 20));
    BOOST_CHECK(!G_FlyingRespawn_IsExpectedCheckpointTouch(&h.state, 21));
    h.state.replay.manualReplay = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_IsExpectedCheckpointTouch(&h.state, 20));
    h.state.replay.manualReplay = qfalse;
    h.state.replay.testReplay = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_IsExpectedCheckpointTouch(&h.state, 20));
    h.state.replay.testReplay = qfalse;
    movement.checkpoints = 0;
    movement.courseID = 0;
    movement.origin[0] = 999; // generic map interactions may change physical state
    BOOST_REQUIRE(G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &movement));
    BOOST_CHECK_EQUAL(movement.checkpoints, 7);
    BOOST_CHECK_EQUAL(movement.courseID, 42);
    BOOST_CHECK_EQUAL(movement.origin[0], 999);
    BOOST_CHECK(h.state.replay.checkpointTouched);
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 2200, 21, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1200);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.secondaryElapsedMs, 200);
    // Static second-respawn uses the same progress operation without activating replay.
    h.state.replay.active = qfalse;
    movement.checkpoints = 0;
    BOOST_REQUIRE(G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &movement));
    BOOST_CHECK_EQUAL(movement.checkpoints, 7);
}

// Catches positive-release captures restarting forever during continuous brush contact.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_continuous_touch_and_reentry)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 500, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 20, &config, &movement));
    for (int time : {1150, 1300, 1550}) {
        BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, time, 20, &config, &movement));
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 1600);
    }
    BOOST_REQUIRE(G_FlyingRespawn_CaptureAutomaticHandoff(&h.state, 1600, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1801, 20, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 2301);
    config.disabled = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 1802, 21, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
}

// Catches the first live frame after replay replacing the immutable checkpoint owner.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_replay_contact_debounces_live_handoff)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 0, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1200, 20, &config, &movement));
    h.state.replay.active = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5000, 20, &config, &movement));
    h.state.replay.active = qfalse;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5050, 20, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1200);
}
