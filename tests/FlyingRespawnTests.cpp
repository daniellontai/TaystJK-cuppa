#include "g_flyingrespawn.h"

#include <boost/test/unit_test.hpp>

struct FlyingHistory {
    flyingRespawnState_t state = {};
    FlyingHistory() { G_FlyingRespawn_InitClient(&state); }
    ~FlyingHistory() { G_FlyingRespawn_FreeClient(&state); }
};

struct FlyingPlayback : FlyingHistory {
    FlyingPlayback() {
        BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&state, 1000, 100, 100, 1, 100));
        flyingRespawnMovement_t m = {};
        usercmd_t cmd = {};
        m.commandTime = 1100;
        m.origin[0] = 10;
        G_FlyingRespawn_RecordState(&state, 1100, &m);
        m.commandTime = cmd.serverTime = 1200;
        m.origin[0] = 20; m.velocity[0] = 100; m.viewangles[YAW] = 45;
        m.forcePower = 80; m.checkpoints = 3; m.courseID = 7;
        cmd.forwardmove = 37;
        G_FlyingRespawn_RecordExecutionCmd(&state, 1200, &cmd);
        G_FlyingRespawn_RecordState(&state, 1200, &m);
        flyingRespawnCheckpointConfig_t config = {};
        BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&state, 1200, 20, &config, &m));
        m.commandTime = cmd.serverTime = 1300;
        m.origin[0] = 30; m.velocity[0] = 120; m.forcePower = 75; m.viewangles[YAW] = 90;
        cmd.upmove = 127;
        G_FlyingRespawn_RecordExecutionCmd(&state, 1300, &cmd);
        G_FlyingRespawn_RecordState(&state, 1300, &m);
        BOOST_REQUIRE(G_FlyingRespawn_CaptureAutomaticHandoff(&state, 1300, &m));
    }
};

BOOST_AUTO_TEST_CASE(flying_respawn_jump_context_roundtrip)
{
    playerState_t ps = {};
    flyingRespawnMovement_t m = {};
    ps.fd.forcePowerLevel[FP_LEVITATION] = 1;
    G_FlyingRespawn_ReadMovement(&m, &ps, 0, 0, 0);
    ps.fd.forcePowerLevel[FP_LEVITATION] = 3;
    G_FlyingRespawn_WriteMovement(&ps, &m, 900);
    BOOST_CHECK_EQUAL(ps.fd.forcePowerLevel[FP_LEVITATION], 1);
}

// A later checkpoint must never pair the failed route's pose with post-GO input.
BOOST_AUTO_TEST_CASE(flying_respawn_successive_checkpoint_rewinds_only_continuous_history)
{
    for (int stationary = 0; stationary < 2; ++stationary) {
        FlyingPlayback h;
        flyingRespawnMovement_t failed = {};
        failed.origin[0] = 999;
        failed.commandTime = 2050;
        G_FlyingRespawn_RecordState(&h.state, 2050, &failed);
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2100, qtrue));
        usercmd_t cmd = {};
        flyingRespawnMovement_t actual = h.state.automaticCheckpoint.handoff.movement;
        actual.commandTime = 2300;
        if (stationary) {
            flyingRespawnSnapshot_t contact = {};
            BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2150, &contact));
            actual = contact.movement;
        } else {
            while (G_FlyingRespawn_OverrideCmd(&h.state, 2300, &cmd)) {}
            G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &actual);
        }
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2300, &actual, &cmd));
    G_FlyingRespawn_RecordHandoff(&h.state, 2300, &actual);
        const float goOrigin = actual.origin[0];
        actual.origin[0] += 5;
        actual.commandTime = cmd.serverTime = 2350;
        cmd.forwardmove = 41;
        G_FlyingRespawn_RecordExecutionCmd(&h.state, 2350, &cmd);
        G_FlyingRespawn_RecordState(&h.state, 2350, &actual);
        flyingRespawnCheckpointConfig_t config = {};
        config.hasRelease = qtrue;
        BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 2350, 21, &config, &actual));
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.start.serverTime, 2300);
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.start.movement.origin[0], goOrigin);
        BOOST_REQUIRE_EQUAL(h.state.automaticCheckpoint.commandCount, 1);
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.commands[0].cmd.forwardmove, 41);
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 3000, qtrue));
        BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 3050);
        BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 3050, &cmd));
        actual.commandTime = 3050;
        G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &actual);
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 3050, &actual, &cmd));
        BOOST_CHECK(!h.state.mismatch);
        BOOST_CHECK_EQUAL(h.state.respawnCount, 2);
    }
}

// A checkpoint inside the first GO command must include that same-tick movement.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_on_first_go_step_keeps_that_command)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2100, qtrue));
    usercmd_t cmd = {};
    while (G_FlyingRespawn_OverrideCmd(&h.state, 2300, &cmd)) {}
    flyingRespawnMovement_t actual = h.state.automaticCheckpoint.handoff.movement;
    actual.commandTime = 2300;
    G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &actual);
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2300, &actual, &cmd));
    G_FlyingRespawn_RecordHandoff(&h.state, 2300, &actual);
    // The finish of the first live movement touches a checkpoint before post-movement recording.
    cmd.serverTime = actual.commandTime = 2301;
    cmd.forwardmove = 53;
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 2300, &cmd);
    actual.origin[0] = 31;
    flyingRespawnCheckpointConfig_t config = {};
    config.hasRelease = qtrue;
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 2300, 21, &config, &actual));
    BOOST_REQUIRE_EQUAL(h.state.automaticCheckpoint.commandCount, 1);
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 3000, qtrue));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 3000, &cmd));
    BOOST_CHECK_EQUAL(cmd.forwardmove, 53);
    actual.commandTime = 3001;
    G_FlyingRespawn_ApplyExpectedCheckpointProgress(&h.state, &actual);
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 3000, &actual, &cmd));
    BOOST_CHECK(!h.state.mismatch);
}

// Diagnostic static playback uses its own endpoint, never an unrelated map capture.
BOOST_AUTO_TEST_CASE(flying_respawn_test_second_press_uses_synthetic_contact)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&h.state, 1300, 200));
    flyingRespawnSnapshot_t contact = {};
    BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 1350, &contact));
    BOOST_CHECK_EQUAL(contact.movement.origin[0], 30);
    BOOST_CHECK_EQUAL(contact.movement.velocity[0], 0);
    BOOST_CHECK_EQUAL(contact.movement.forcePower, 75);
    BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 1500);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.movement.origin[0], 20);
    usercmd_t live = {};
    live.forwardmove = live.upmove = 127;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &live);
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 1500, &contact.movement, &live));
    BOOST_CHECK_EQUAL(live.upmove, 127);
    BOOST_CHECK_EQUAL(live.forwardmove, 127);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 0);
    BOOST_CHECK(h.state.testReplayUsed);
    BOOST_CHECK(!h.state.mismatch);
}

// Catches bypassing the enable/pending gate, replaying live input, or releasing before all execution steps.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_gate_commands_and_exact_handoff)
{
    FlyingPlayback h;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qfalse));
    h.state.automaticCheckpoint.pending = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    h.state.automaticCheckpoint.pending = qfalse;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 10);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 1);
    BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 2200);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 2199), 200);
    usercmd_t live = {}, command = {};
    live.forwardmove = live.upmove = 127; live.angles[YAW] = 12345;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &live);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2099, &command));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &command));
    BOOST_CHECK_EQUAL(command.serverTime, 2100);
    BOOST_CHECK_EQUAL(command.forwardmove, 37);
    BOOST_CHECK_EQUAL(command.upmove, 0);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2199, &command));
    flyingRespawnMovement_t actual = h.state.automaticCheckpoint.handoff.movement;
    actual.commandTime = 2200;
    h.state.replay.checkpointTouched = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_Advance(&h.state, 2200, &actual, &command));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2200, &command));
    BOOST_CHECK_EQUAL(command.upmove, 127);
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2200, &actual, &command));
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!h.state.runInvalid);
    BOOST_CHECK_EQUAL(command.forwardmove, 127);
    BOOST_CHECK_EQUAL(command.upmove, 127);
    BOOST_CHECK_EQUAL(command.angles[YAW], 12345);
    BOOST_CHECK_EQUAL(command.serverTime, 2201);
    BOOST_CHECK_EQUAL(h.state.secondaryBaseMs, 200);
    BOOST_CHECK_EQUAL(h.state.secondarySegmentStart, 2200);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 2250), 250);
    BOOST_CHECK(!G_FlyingRespawn_Advance(&h.state, 2200, &actual, &command));
}

// Catches restoring post-release state for static mode, moving the deadline/count, or losing contact debounce.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_second_press_uses_static_contact)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    flyingRespawnSnapshot_t contact = {};
    BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2050, &contact));
    BOOST_CHECK_EQUAL(contact.movement.origin[0], 20);
    BOOST_CHECK_EQUAL(contact.movement.velocity[0], 0);
    BOOST_CHECK_EQUAL(contact.movement.forcePower, 80);
    BOOST_CHECK_EQUAL(contact.movement.viewangles[YAW], 45);
    BOOST_CHECK_EQUAL(contact.movement.commandTime, 2050);
    BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 2200);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 1);
    BOOST_CHECK_EQUAL(h.state.checkpointContactSeen[20], 1);
    BOOST_CHECK_EQUAL(h.state.checkpointTouchTimes[20], 2050);
    usercmd_t command = {};
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2200, &command));
    BOOST_CHECK(!G_FlyingRespawn_HandleSecondPress(&h.state, 2100, &contact));
    BOOST_CHECK(!G_FlyingRespawn_Advance(&h.state, 2199, &contact.movement, &command));
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2200, &contact.movement, &command));
    BOOST_CHECK(!h.state.runInvalid);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 2200), 200);
    BOOST_CHECK_EQUAL(h.state.checkpointTouchTimes[20], 2200);
}

// Catches omitted angle/Force/progress/clock/contact validation or preventing control after a mismatch.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_mismatch_invalidates_but_hands_off)
{
    for (int field = 0; field < 7; ++field) {
        FlyingPlayback h;
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
        usercmd_t command = {};
        while (G_FlyingRespawn_OverrideCmd(&h.state, 2200, &command)) {}
        flyingRespawnMovement_t actual = h.state.automaticCheckpoint.handoff.movement;
        actual.commandTime = 2200;
        h.state.replay.checkpointTouched = qtrue;
        if (field == 0) actual.origin[0] += 5;
        if (field == 1) actual.velocity[0] += 5;
        if (field == 2) actual.viewangles[YAW] += 5;
        if (field == 3) actual.forcePower--;
        if (field == 4) actual.checkpoints--;
        if (field == 5) h.state.secondaryBaseMs++;
        if (field == 6) h.state.replay.checkpointTouched = qfalse;
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2200, &actual, &command));
        BOOST_CHECK(h.state.mismatch);
        BOOST_CHECK(G_FlyingRespawn_IsRunInvalid(&h.state));
        BOOST_CHECK(!h.state.replay.active);
    }
}

// Catches test replay using receipt commands, failing to finish, changing captures/count, or remaining submittable.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_test_mode_has_no_run_result)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 0, 0, 1));
    flyingRespawnMovement_t actual = {};
    usercmd_t command = {};
    G_FlyingRespawn_RecordState(&h.state, 1000, &actual);
    command.serverTime = actual.commandTime = 31000;
    command.forwardmove = 37;
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 31000, &command);
    G_FlyingRespawn_RecordState(&h.state, 31000, &actual);
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&h.state, 31000, 30001));
    BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&h.state, 31000, 30000));
    BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 61000);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 60999, &command));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 61000, &command));
    BOOST_CHECK_EQUAL(command.forwardmove, 37);
    actual.commandTime = 61000;
    BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 61000, &actual, &command));
    BOOST_CHECK_EQUAL(h.state.respawnCount, 0);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(G_FlyingRespawn_IsRunInvalid(&h.state));
    BOOST_CHECK(h.state.testReplayUsed);
    BOOST_CHECK(!h.state.mismatch);
}

// Catches a sampled predecessor making a requested 30-second test run longer than the hard cap.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_test_predecessor_cannot_exceed_thirty_seconds)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 0, 0, 1));
    flyingRespawnMovement_t actual = {};
    G_FlyingRespawn_RecordState(&h.state, 999, &actual);
    G_FlyingRespawn_RecordState(&h.state, 31000, &actual);
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&h.state, 31000, 30000));
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!h.state.testReplayUsed);
}

// Catches applying the held live command (including angles) before validation in static mode.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_static_pre_live_angles_and_force_are_validated)
{
    for (int field = 0; field < 5; ++field) {
        FlyingPlayback h;
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
        flyingRespawnSnapshot_t contact = {};
        BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2050, &contact));
        usercmd_t live = {};
        live.forwardmove = live.upmove = 127; live.angles[YAW] = 30000;
        G_FlyingRespawn_BufferLiveCmd(&h.state, &live);
        if (field == 0) contact.movement.viewangles[YAW] = 90;
        if (field == 1) contact.movement.forcePower = 75;
        if (field == 2) contact.movement.velocity[0] = 100;
        if (field == 3) contact.movement.movementStyle++;
        if (field == 4) contact.movement.courseID++;
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2200, &contact.movement, &live));
        BOOST_CHECK(h.state.mismatch);
        BOOST_CHECK_EQUAL(live.angles[YAW], 30000);
        BOOST_CHECK_EQUAL(live.forwardmove, 127);
        BOOST_CHECK_EQUAL(live.upmove, 127);
    }
}

// Catches resampling a recorded step on a different fixed-pmove clock phase after relocation.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_keeps_source_command_quantization_phase)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    // The replay offset is 900. Source 1211 rounds to 1216, hence relocated 2116.
    BOOST_CHECK_EQUAL(G_FlyingRespawn_QuantizeCmdTime(&h.state, 2111, 16), 2116);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_QuantizeCmdTime(&h.state, 2111, 8), 2116);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_QuantizeCmdTime(&h.state, 2111, 3), 2112);
    h.state.replay.active = qfalse;
    BOOST_CHECK_EQUAL(G_FlyingRespawn_QuantizeCmdTime(&h.state, 2111, 16), 2112);
}

// Catches aborting only manual replay, retaining forced input, or resetting timer state on cancellation.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_cancellation_returns_latest_live_input)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    usercmd_t command = {}, live = {};
    live.forwardmove = 127; live.upmove = 127; live.angles[YAW] = 12345;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &live);
    BOOST_REQUIRE(G_FlyingRespawn_CancelReplay(&h.state, 2050, &command));
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK_EQUAL(command.forwardmove, 127);
    BOOST_CHECK_EQUAL(command.upmove, 127);
    BOOST_CHECK_EQUAL(command.angles[YAW], 12345);
    BOOST_CHECK_EQUAL(command.serverTime, 2050);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 1);
    BOOST_CHECK_EQUAL(h.state.secondaryBaseMs, 200);
    BOOST_CHECK(!h.state.secondaryRunning);
    BOOST_CHECK(!G_FlyingRespawn_CancelReplay(&h.state, 2051, &command));
}

// Catches frame consumers seeing buffered live input or advancing the movement cursor themselves.
BOOST_AUTO_TEST_CASE(flying_respawn_playback_frame_input_is_recorded_and_nonconsuming)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    usercmd_t command = {}, live = {};
    live.forwardmove = -127; live.upmove = -127;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &live);
    BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 2100, &command));
    BOOST_CHECK_EQUAL(command.forwardmove, 37);
    BOOST_CHECK_EQUAL(command.upmove, 0);
    BOOST_CHECK_EQUAL(command.serverTime, 2100);
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 0);
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &command));
    BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 2150, &command));
    BOOST_CHECK_EQUAL(command.forwardmove, 37);
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
    BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 2200, &command));
    BOOST_CHECK_EQUAL(command.upmove, 127);
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
    flyingRespawnSnapshot_t contact = {};
    BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2150, &contact));
    BOOST_CHECK(!G_FlyingRespawn_FrameCmd(&h.state, 2199, &command));
    BOOST_CHECK_EQUAL(command.forwardmove, 0);
    BOOST_CHECK_EQUAL(command.upmove, 0);
    BOOST_CHECK_EQUAL(h.state.replay.liveCmd.upmove, -127);
    BOOST_CHECK_EQUAL(command.serverTime, 2199);
}

// Catches selecting the newest state instead of the last state preceding rewind.
BOOST_AUTO_TEST_CASE(flying_respawn_history_selects_by_server_time)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 50, 0, 1));
    flyingRespawnMovement_t movement = {};
    usercmd_t cmd = {};
    for (int time : {1000, 1100, 1200, 1300}) {
        movement.commandTime = time;
        movement.origin[0] = static_cast<float>(time);
        cmd.serverTime = time + 7;
        cmd.forwardmove = 37;
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
    }
    BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&h.state, 1300, 50));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.serverTime, 1200);
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 1200);
    usercmd_t replay = {};
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 1400, &replay));
    BOOST_CHECK_EQUAL(replay.serverTime, 1407);
    BOOST_CHECK_EQUAL(replay.forwardmove, 37);
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
            G_FlyingRespawn_RecordExecutionCmd(s, time, &cmd);
            G_FlyingRespawn_RecordState(s, time, &movement);
        }
    }
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&ordinary.state, 31000, 1500));
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&test.state, 31000, 30001));
    BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&test.state, 31000, 30000));
    BOOST_CHECK(test.state.replay.active);
    BOOST_CHECK(test.state.replay.testReplay);
    BOOST_CHECK_EQUAL(test.state.replay.selection.commandCount, 30000);
    BOOST_CHECK_EQUAL(test.state.respawnCount, 0);
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&test.state, 1500, 0, 0));
    BOOST_CHECK(!test.state.replay.active);
    BOOST_CHECK(!G_FlyingRespawn_BeginTestReplay(&test.state, 31000, 30000));
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
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 100, 0, 0, 100));
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
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 3000, 100, 0, 0, 100));
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 3050), 50);
    BOOST_CHECK(!G_FlyingRespawn_IsRunInvalid(&h.state));
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetRespawnCount(&h.state), 0);
    BOOST_CHECK(h.state.manualSave.valid); // separate manual owner survives run restart
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
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
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        if (time % 100 == 0) G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 3050, &movement));
    BOOST_CHECK_EQUAL(h.state.manualSave.start.serverTime, 1500);
    BOOST_CHECK_EQUAL(h.state.manualSave.commandCount, 1550);
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
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    h.state.respawnCount = 3;
    G_FlyingRespawn_ResetSecondarySegment(&h.state, 1000, 42);
    const auto generation = h.state.historyGeneration;
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_CHECK_EQUAL(h.state.historyGeneration, generation);
    BOOST_CHECK_EQUAL(h.state.respawnCount, 3);
    BOOST_CHECK_EQUAL(G_FlyingRespawn_GetSecondaryElapsed(&h.state, 1100), 142);
    BOOST_CHECK(!h.state.replay.active);
    BOOST_CHECK(!h.state.runInvalid);
    BOOST_CHECK_EQUAL(movement.origin[0], 1100);
    for (int time = 1101; time <= 3500; ++time) {
        cmd.serverTime = movement.commandTime = time;
        cmd.forwardmove = 99;
        G_FlyingRespawn_RecordExecutionCmd(&h.state, time, &cmd);
        G_FlyingRespawn_RecordState(&h.state, time, &movement);
    }
    BOOST_CHECK(!G_FlyingRespawn_BeginManualReplay(&h.state, 4000, qfalse));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 4000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 1000);
    usercmd_t replay = {};
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 4049, &replay));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 4050, &replay));
    BOOST_CHECK_EQUAL(replay.serverTime, 4050);
    BOOST_CHECK_EQUAL(replay.forwardmove, 5);
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 4100, &replay));
    BOOST_CHECK_EQUAL(replay.forwardmove, 10);
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
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    BOOST_CHECK(!G_FlyingRespawn_FinishManualReplay(&h.state, 2100, qtrue)); // command must be simulated first
    usercmd_t replay = {};
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &replay));
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
        G_FlyingRespawn_RecordExecutionCmd(&h.state, 1050, &cmd);
        G_FlyingRespawn_RecordState(&h.state, 1050, &movement);
    }
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    usercmd_t replay = {};
    for (int expected : {1010, 1020, 1030}) {
        BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2050, &replay));
        BOOST_CHECK_EQUAL(replay.serverTime, expected + 1000);
    }
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2100, &replay));
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
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 2000, 1500, 0, 0, 1500));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 3000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.snapshot.movement.origin[0], 42);
    BOOST_CHECK(G_FlyingRespawn_FinishManualReplay(&h.state, 3000, qtrue));
}

// Catches positive-release captures losing their approach when the rolling ring expires.
BOOST_AUTO_TEST_CASE(flying_respawn_manual_shared_capture_owns_pending_automatic_input)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 100, 2000, 0, 100));
    flyingRespawnMovement_t movement = {};
    movement.commandTime = 1000;
    G_FlyingRespawn_RecordState(&h.state, 1000, &movement);
    usercmd_t cmd = {};
    cmd.serverTime = movement.commandTime = 1100;
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 20, &config, &movement));
    BOOST_CHECK(h.state.automaticCheckpoint.pending);
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    for (int time = 1101; time <= 3100; ++time) {
        cmd.serverTime = movement.commandTime = time;
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
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 4000, 100, 0, 0, 100));
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
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 1500, 0, 1));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    const auto generation = h.state.historyGeneration;
    cmd.forwardmove = 127;
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 2100, &cmd);
    G_FlyingRespawn_RecordState(&h.state, 2100, &movement);
    BOOST_CHECK_EQUAL(h.state.executionCount, 0);
    BOOST_CHECK_EQUAL(h.state.stateCount, 0);
    BOOST_CHECK_EQUAL(h.state.historyGeneration, generation);
    BOOST_CHECK(!G_FlyingRespawn_CaptureManual(&h.state, 2100, &movement));
    usercmd_t replay = {};
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &replay));
    BOOST_CHECK_EQUAL(replay.forwardmove, 17);
    replay.forwardmove = 99;
    BOOST_REQUIRE(G_FlyingRespawn_CancelReplay(&h.state, 2100, &replay));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 3000, qtrue));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 3100, &replay));
    BOOST_CHECK_EQUAL(replay.forwardmove, 17);
    G_FlyingRespawn_FreeClient(&h.state);
    BOOST_CHECK(!h.state.manualSave.valid);
    BOOST_CHECK(h.state.manualSave.commands == nullptr);
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
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    movement.commandTime = 1107;
    G_FlyingRespawn_RecordState(&h.state, 1100, &movement);
    cmd.serverTime = 1400; // ForceClientUpdate advances the last held input without a receipt
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1400, &cmd);
    movement.commandTime = 1400;
    G_FlyingRespawn_RecordState(&h.state, 1400, &movement);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1400, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    usercmd_t step = {};
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &step));
    BOOST_CHECK_EQUAL(step.serverTime, 2107);
    BOOST_CHECK_EQUAL(step.forwardmove, 37);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2399, &step));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2400, &step));
    BOOST_CHECK_EQUAL(step.serverTime, 2400);
    BOOST_CHECK_EQUAL(step.forwardmove, 37);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2400, &step));
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
    cmd.serverTime = 1068;
    cmd.forwardmove = -32;
    cmd.upmove = 0;
    cmd.angles[YAW] = 12345;
    cmd.serverTime = 1100; // G_RunClient uses the latest input at server frame time
    G_FlyingRespawn_RecordExecutionCmd(&h.state, 1100, &cmd);
    movement.commandTime = 1100;
    G_FlyingRespawn_RecordState(&h.state, 1100, &movement);
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 2000, qtrue));
    usercmd_t step = {};
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2099, &step));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &step));
    BOOST_CHECK_EQUAL(step.serverTime, 2100);
    BOOST_CHECK_EQUAL(step.forwardmove, -32);
    BOOST_CHECK_EQUAL(step.upmove, 0);
    BOOST_CHECK_EQUAL(step.angles[YAW], 12345);
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2100, &step));
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

// Catches alternating overlapping brush callbacks renewing each other's positive-release deadline.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_overlapping_contacts_keep_independent_gaps)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 500, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 20, &config, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 21, &config, &movement));
    for (int time = 1150; time <= 2100; time += 50) {
        BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, time, 20, &config, &movement));
        BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, time, 21, &config, &movement));
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.checkpointEntityNum, 21);
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1100);
        BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 1600);
        if (time == 1600)
            BOOST_CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(&h.state, time, &movement));
    }
    BOOST_CHECK(h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    // B remains in contact while A leaves for 251 ms, then re-enters.
    for (int time : {2200, 2300})
        BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, time, 21, &config, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 2351, 20, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.checkpointEntityNum, 20);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 2851);
    config.disabled = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 2352, 22, &config, &movement));
    config.disabled = qfalse;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 2353, 21, &config, &movement));
    BOOST_CHECK(!h.state.automaticCheckpoint.valid);
    BOOST_CHECK(!h.state.automaticCheckpoint.pending);
    // A new run clears every brush's contact gap, including the overlapping ones.
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 2400, 0, 500, 0, 0));
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 2401, 20, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.releaseServerTime, 2901);
}

// Catches invalid entity numbers indexing the per-checkpoint contact table or replacing the target.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_contact_table_rejects_invalid_entities)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 0, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, MAX_GENTITIES - 1, &config, &movement));
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 1150, -1, &config, &movement));
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 1150, MAX_GENTITIES, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.checkpointEntityNum, MAX_GENTITIES - 1);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1100);
}

// Catches a non-target overlapping brush being mistaken for a new crossing at replay handoff.
BOOST_AUTO_TEST_CASE(flying_respawn_checkpoint_overlapping_replay_contacts_preserve_handoff)
{
    FlyingHistory h;
    BOOST_REQUIRE(G_FlyingRespawn_OnTimerStart(&h.state, 1000, 0, 0, 0, 0));
    flyingRespawnMovement_t movement = {};
    flyingRespawnCheckpointConfig_t config = {};
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 20, &config, &movement));
    BOOST_REQUIRE(G_FlyingRespawn_OnCheckpointTouch(&h.state, 1100, 21, &config, &movement));
    h.state.replay.active = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5000, 20, &config, &movement));
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5000, 21, &config, &movement));
    h.state.replay.active = qfalse;
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5050, 20, &config, &movement));
    BOOST_CHECK(!G_FlyingRespawn_OnCheckpointTouch(&h.state, 5050, 21, &config, &movement));
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.checkpointEntityNum, 21);
    BOOST_CHECK_EQUAL(h.state.automaticCheckpoint.snapshot.serverTime, 1100);
}

BOOST_AUTO_TEST_CASE(flying_respawn_static_acknowledges_received_input)
{
    FlyingHistory h;
    playerState_t ps = {};
    usercmd_t cmd = {};
    ps.commandTime = 2000;
    h.state.replay.active = h.state.replay.staticReplay = qtrue;
    cmd.serverTime = 2150;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &cmd);
    G_FlyingRespawn_AcknowledgeStaticInput(&h.state, 2100, &ps);
    BOOST_CHECK_EQUAL(ps.commandTime, 2100);
    cmd.serverTime = 1900;
    G_FlyingRespawn_BufferLiveCmd(&h.state, &cmd);
    G_FlyingRespawn_AcknowledgeStaticInput(&h.state, 2200, &ps);
    BOOST_CHECK_EQUAL(ps.commandTime, 2100);
    for (int mode = 0; mode < 4; ++mode) {
        h.state.replay.active = (mode != 0) ? qtrue : qfalse;
        h.state.replay.staticReplay = (mode != 1) ? qtrue : qfalse;
        h.state.replay.manualReplay = (mode == 2) ? qtrue : qfalse;
        h.state.replay.hasLiveCmd = (mode != 3) ? qtrue : qfalse;
        h.state.replay.liveCmd.serverTime = 2300;
        G_FlyingRespawn_AcknowledgeStaticInput(&h.state, 2300, &ps);
        BOOST_CHECK_EQUAL(ps.commandTime, 2100);
    }
}

BOOST_AUTO_TEST_CASE(flying_respawn_static_ack_validation_scope)
{
    for (int change = 0; change < 3; ++change) {
        FlyingPlayback h;
        flyingRespawnSnapshot_t contact = {};
        usercmd_t cmd = {};
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
        BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2050, &contact));
        contact.movement.commandTime += 100;
        if (change == 1) ++contact.movement.forcePower;
        if (change == 2) ++contact.movement.checkpoints;
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, 2200, &contact.movement, &cmd));
        BOOST_CHECK_EQUAL(h.state.mismatch, change != 0);
    }
    for (int diagnostic = 0; diagnostic < 2; ++diagnostic) {
        FlyingPlayback h;
        usercmd_t cmd = {};
        if (diagnostic) BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&h.state, 2000, 100));
        else BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
        int deadline = h.state.replay.releaseServerTime;
        while (G_FlyingRespawn_OverrideCmd(&h.state, deadline, &cmd)) {}
        flyingRespawnMovement_t actual = h.state.replay.expectedMovement;
        ++actual.commandTime;
        BOOST_REQUIRE(G_FlyingRespawn_Advance(&h.state, deadline, &actual, &cmd));
        BOOST_CHECK(h.state.mismatch);
    }
}

BOOST_AUTO_TEST_CASE(flying_respawn_captured_setup_contract)
{
    FlyingPlayback h;
    auto before = h.state.replay;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qfalse));
    BOOST_CHECK_EQUAL(memcmp(&before, &h.state.replay, sizeof(before)), 0);
    h.state.automaticCheckpoint.pending = qtrue;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    h.state.automaticCheckpoint.pending = qfalse;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, INT_MAX, qtrue));
    BOOST_CHECK_EQUAL(memcmp(&before, &h.state.replay, sizeof(before)), 0);
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.selection.endServerTime, 1300);
    BOOST_CHECK_EQUAL(h.state.replay.expectedMovement.origin[0], 30);
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    FlyingHistory manual;
    flyingRespawnMovement_t m = {};
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&manual.state, 1000, &m));
    BOOST_CHECK(!G_FlyingRespawn_BeginManualReplay(&manual.state, 2000, qfalse));
    BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&manual.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(manual.state.replay.selection.endServerTime, 1000);
    BOOST_CHECK_EQUAL(manual.state.replay.releaseServerTime, 2000);
}

BOOST_AUTO_TEST_CASE(flying_respawn_command_peek_consume_contract)
{
    FlyingPlayback h;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    usercmd_t cmd = {};
    for (int i = 0; i < 2; ++i) {
        BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 2100, &cmd));
        BOOST_CHECK_EQUAL(cmd.forwardmove, 37);
        BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 0);
    }
    BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 2099, &cmd));
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2100, &cmd));
    BOOST_CHECK_EQUAL(cmd.serverTime, 2100);
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
    BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 2200, &cmd));
    BOOST_CHECK_EQUAL(cmd.upmove, 127);
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
    BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 2200, &cmd));
    BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 2);
}

BOOST_AUTO_TEST_CASE(flying_respawn_other_sources_peek_consume_contract)
{
    for (bool diagnostic : {false, true}) {
        FlyingHistory h;
        BOOST_REQUIRE(G_FlyingRespawn_Configure(&h.state, 100, 0, diagnostic ? 1 : 0));
        flyingRespawnMovement_t m = {};
        usercmd_t c = {};
        for (int t : {1000, 1050, 1100}) {
            c.serverTime = m.commandTime = t;
            c.forwardmove = (t - 1000) / 10;
            G_FlyingRespawn_RecordExecutionCmd(&h.state, t, &c);
            G_FlyingRespawn_RecordState(&h.state, t, &m);
        }
        if (diagnostic) BOOST_REQUIRE(G_FlyingRespawn_BeginTestReplay(&h.state, 1100, 100));
        else {
            BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&h.state, 1100, &m));
            BOOST_REQUIRE(G_FlyingRespawn_BeginManualReplay(&h.state, 1100, qtrue));
        }
        for (int i = 0; i < 2; ++i) {
            BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 1150, &c));
            BOOST_CHECK_EQUAL(c.forwardmove, 5);
            BOOST_CHECK_EQUAL(c.serverTime, 1150);
            BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 0);
        }
        BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 1149, &c));
        BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 1150, &c));
        BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
        if (diagnostic) {
            h.state.historyGeneration++;
            BOOST_CHECK(!G_FlyingRespawn_OverrideCmd(&h.state, 1200, &c));
            BOOST_REQUIRE(G_FlyingRespawn_FrameCmd(&h.state, 1200, &c));
            BOOST_CHECK_EQUAL(c.forwardmove, 5);
            BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 1);
        } else {
            BOOST_REQUIRE(G_FlyingRespawn_OverrideCmd(&h.state, 1200, &c));
            BOOST_CHECK_EQUAL(c.forwardmove, 10);
            BOOST_CHECK_EQUAL(h.state.replay.commandCursor, 2);
        }
    }
}

BOOST_AUTO_TEST_CASE(flying_respawn_pausable_deadline_contract)
{
    playerState_t ps = {}, out = {};
    ps.commandTime = 1000; ps.lastOnGround = 900; ps.groundTime = 1200;
    ps.pm_time = 33; ps.origin[0] = 8; ps.fd.forcePower = 42;
    for (int i = 0; i < NUM_FORCE_POWERS; ++i) {
        ps.fd.forcePowerDebounce[i] = i ? 1200 + i : 0;
        ps.fd.forcePowerDuration[i] = i ? 1300 + i : 0;
    }
    ps.fd.forcePowerRegenDebounceTime = 1400; ps.fd.forceJumpAddTime = 1500;
    ps.fd.forceRageRecoveryTime = INT_MAX - 5;
    flyingRespawnMovement_t m = {};
    G_FlyingRespawn_ReadMovement(&m, &ps, 0, 0, 0);
    for (int offset : {0, -100, 100}) {
        G_FlyingRespawn_WriteMovement(&out, &m, offset);
        for (int i = 0; i < NUM_FORCE_POWERS; ++i) {
            BOOST_CHECK_EQUAL(out.fd.forcePowerDebounce[i], i ? 1200 + i + offset : 0);
            BOOST_CHECK_EQUAL(out.fd.forcePowerDuration[i], i ? 1300 + i + offset : 0);
        }
        BOOST_CHECK_EQUAL(out.fd.forcePowerRegenDebounceTime, 1400 + offset);
        BOOST_CHECK_EQUAL(out.fd.forceJumpAddTime, 1500 + offset);
        BOOST_CHECK_EQUAL(out.fd.forceRageRecoveryTime, offset > 5 ? INT_MAX : INT_MAX - 5 + offset);
        BOOST_CHECK_EQUAL(out.groundTime, 1200 + offset);
        BOOST_CHECK_EQUAL(out.commandTime, 1000 + offset);
        BOOST_CHECK_EQUAL(out.lastOnGround, 900 + offset);
        BOOST_CHECK_EQUAL(out.pm_time, 33); BOOST_CHECK_EQUAL(out.origin[0], 8);
        BOOST_CHECK_EQUAL(out.fd.forcePower, 42);
    }
    FlyingPlayback h;
    auto &src = h.state.automaticCheckpoint.snapshot.movement;
    src.jetPackDebReduce = 1250; src.jetPackDebRecharge = 0; src.overheatRechargeDebReduce = 1350;
    src.forcePowerDebounce[1] = 1400; src.forcePowerDuration[1] = 1500;
    src.forcePowerRegenDebounceTime = 1600; src.forceJumpAddTime = 1700;
    src.forceRageRecoveryTime = INT_MAX - 5; src.groundTime = 1100; src.lastOnGround = 1100;
    src.pmTime = 33;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    flyingRespawnSnapshot_t contact = {};
    BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2050, &contact));
    BOOST_CHECK_EQUAL(contact.movement.commandTime, 2050);
    BOOST_CHECK_EQUAL(contact.movement.lastOnGround, 1950);
    BOOST_CHECK_EQUAL(contact.movement.jetPackDebReduce, 2250);
    BOOST_CHECK_EQUAL(contact.movement.jetPackDebRecharge, 0);
    BOOST_CHECK_EQUAL(contact.movement.overheatRechargeDebReduce, 2350);
    BOOST_CHECK_EQUAL(contact.movement.forcePowerDebounce[1], 2400);
    BOOST_CHECK_EQUAL(contact.movement.forcePowerDuration[1], 2500);
    BOOST_CHECK_EQUAL(contact.movement.forcePowerRegenDebounceTime, 2600);
    BOOST_CHECK_EQUAL(contact.movement.forceJumpAddTime, 2700);
    BOOST_CHECK_EQUAL(contact.movement.forceRageRecoveryTime, INT_MAX);
    BOOST_CHECK_EQUAL(contact.movement.groundTime, 2100);
    BOOST_CHECK_EQUAL(contact.movement.pmTime, 33);
}

BOOST_AUTO_TEST_CASE(flying_respawn_captured_setup_remaining_boundaries)
{
    FlyingPlayback h;
    auto before = h.state.replay;
    h.state.automaticCheckpoint.valid = qfalse;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(memcmp(&before, &h.state.replay, sizeof(before)), 0);
    h.state.automaticCheckpoint.valid = qtrue;
    h.state.automaticCheckpoint.handoff.serverTime = h.state.automaticCheckpoint.start.serverTime - 1;
    BOOST_CHECK(!G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(memcmp(&before, &h.state.replay, sizeof(before)), 0);
    h.state.automaticCheckpoint.handoff = h.state.automaticCheckpoint.start;
    BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(h.state.replay.releaseServerTime, 2000);
    BOOST_CHECK_EQUAL(h.state.replay.selection.endServerTime, h.state.automaticCheckpoint.start.serverTime);
    FlyingHistory manual;
    flyingRespawnMovement_t m = {};
    BOOST_REQUIRE(G_FlyingRespawn_CaptureManual(&manual.state, 1000, &m));
    manual.state.manualSave.snapshot.serverTime = 999;
    before = manual.state.replay;
    BOOST_CHECK(!G_FlyingRespawn_BeginManualReplay(&manual.state, 2000, qtrue));
    BOOST_CHECK_EQUAL(memcmp(&before, &manual.state.replay, sizeof(before)), 0);
}

// Exercise pause-only adjustments through public operations, including sentinels.
BOOST_AUTO_TEST_CASE(flying_respawn_pause_only_all_deadlines_contract)
{
    using Deadline = int flyingRespawnMovement_t::*;
    const Deadline scalars[] = {
        &flyingRespawnMovement_t::forcePowerRegenDebounceTime,
        &flyingRespawnMovement_t::forceJumpAddTime,
        &flyingRespawnMovement_t::forceRageRecoveryTime,
        &flyingRespawnMovement_t::groundTime,
        &flyingRespawnMovement_t::jetPackDebReduce,
        &flyingRespawnMovement_t::jetPackDebRecharge,
        &flyingRespawnMovement_t::overheatRechargeDebReduce
    };
    auto seed = [&](flyingRespawnMovement_t &m, int value) {
        for (int i = 0; i < NUM_FORCE_POWERS; ++i)
            m.forcePowerDebounce[i] = m.forcePowerDuration[i] = value;
        for (auto field : scalars) m.*field = value;
        m.commandTime = 1200; m.lastOnGround = 1100;
        m.origin[0] = 12; m.viewangles[YAW] = 45;
        m.pmTime = 33; m.dashTime = 34; m.wallJumpTime = 35; m.jumpTime = 36;
        m.weaponTime = 37; m.forcePower = 42; m.jetpackFuel = 43;
    };
    auto deadlines = [&](const flyingRespawnMovement_t &m, int expected) {
        for (int i = 0; i < NUM_FORCE_POWERS; ++i) {
            BOOST_CHECK_EQUAL(m.forcePowerDebounce[i], expected);
            BOOST_CHECK_EQUAL(m.forcePowerDuration[i], expected);
        }
        for (auto field : scalars) BOOST_CHECK_EQUAL(m.*field, expected);
    };
    auto unchanged = [&](const flyingRespawnMovement_t &m, int commandTime, int lastOnGround) {
        BOOST_CHECK_EQUAL(m.commandTime, commandTime); BOOST_CHECK_EQUAL(m.lastOnGround, lastOnGround);
        BOOST_CHECK_EQUAL(m.origin[0], 12); BOOST_CHECK_EQUAL(m.viewangles[YAW], 45);
        BOOST_CHECK_EQUAL(m.pmTime, 33); BOOST_CHECK_EQUAL(m.dashTime, 34);
        BOOST_CHECK_EQUAL(m.wallJumpTime, 35); BOOST_CHECK_EQUAL(m.jumpTime, 36);
        BOOST_CHECK_EQUAL(m.weaponTime, 37); BOOST_CHECK_EQUAL(m.forcePower, 42);
        BOOST_CHECK_EQUAL(m.jetpackFuel, 43);
    };
    for (int value : {0, 1400, INT_MAX - 5}) {
        FlyingPlayback h;
        auto &source = h.state.automaticCheckpoint.snapshot.movement;
        seed(source, value);
        BOOST_REQUIRE(G_FlyingRespawn_BeginCheckpointReplay(&h.state, 2000, qtrue));
        flyingRespawnSnapshot_t contact = {};
        BOOST_REQUIRE(G_FlyingRespawn_HandleSecondPress(&h.state, 2050, &contact));
        deadlines(contact.movement, value == 0 ? 0 : value == 1400 ? 2400 : INT_MAX);
        unchanged(contact.movement, 2050, 1950);

        flyingRespawnMovement_t movement = {};
        seed(movement, value);
        const auto original = movement;
        // Active, non-static, early, and exactly-on-time release must be no-ops.
        G_FlyingRespawn_ReleaseStaticDeadlines(&h.state, 2250, &movement);
        BOOST_CHECK_EQUAL(memcmp(&original, &movement, sizeof(movement)), 0);
        h.state.replay.active = qfalse; h.state.replay.staticReplay = qfalse;
        G_FlyingRespawn_ReleaseStaticDeadlines(&h.state, 2250, &movement);
        BOOST_CHECK_EQUAL(memcmp(&original, &movement, sizeof(movement)), 0);
        h.state.replay.staticReplay = qtrue;
        G_FlyingRespawn_ReleaseStaticDeadlines(&h.state, 2199, &movement);
        BOOST_CHECK_EQUAL(memcmp(&original, &movement, sizeof(movement)), 0);
        G_FlyingRespawn_ReleaseStaticDeadlines(&h.state, 2200, &movement);
        BOOST_CHECK_EQUAL(memcmp(&original, &movement, sizeof(movement)), 0);
        G_FlyingRespawn_ReleaseStaticDeadlines(&h.state, 2250, &movement);
        deadlines(movement, value == 0 ? 0 : value == 1400 ? 1450 : INT_MAX);
        unchanged(movement, 1200, 1100);
    }
}
