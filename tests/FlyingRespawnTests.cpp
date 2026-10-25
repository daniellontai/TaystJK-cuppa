#include "g_flyingrespawn.h"
#include "sqlite3.h"

#include <boost/test/unit_test.hpp>

struct FlyingHistory {
    flyingRespawnState_t state = {};
    FlyingHistory() { G_FlyingRespawn_InitClient(&state); }
    ~FlyingHistory() { G_FlyingRespawn_FreeHistory(&state); }
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
