/* Links the real game module objects; only engine imports are replaced. */
#include "g_local.h"
#include "sqlite3.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void TimerStart(gentity_t *, gentity_t *, trace_t *);
void TimerStop(gentity_t *, gentity_t *, trace_t *);
void WP_ForcePowersUpdate(gentity_t *, usercmd_t *);
void G_RunFrame(int);
void InitGameAccountStuff(void);
void Cmd_Aminfo_f(gentity_t *);
void G_AddRaceTime(char *, char *, int, int, int, int, int, int, int);
void PrintRaceTime(char *, char *, char *, char *, int, int, char *, int, int, qboolean, int, qboolean, qboolean, int, int, float, int, int);
extern gclient_t g_clients[MAX_CLIENTS];

static gameImport_t imports;
static int failures, routeCalls, commandsSent;
static struct { int client; char text[2048]; } sent[64];
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); failures++; \
} } while (0)

static int Clock(void) { return 5000; }
static void ServerCommand(int client, const char *text) {
    if (commandsSent < 64) {
        sent[commandsSent].client = client;
        Q_strncpyz(sent[commandsSent].text, text, sizeof(sent[commandsSent].text));
    }
    commandsSent++;
}
static void CvarString(const char *name, char *buffer, int size) { (void)name; Q_strncpyz(buffer, ".", size); }
static void Print(const char *format, ...) { va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args); }
static int Open(const char *path, fileHandle_t *file, fsMode_t mode) { (void)path; (void)mode; *file = 0; return -1; }
static void Userinfo(int client, char *buffer, int size) { (void)client; if (size) buffer[0] = 0; }
static void Serverinfo(char *buffer, int size) { Q_strncpyz(buffer, "\\mapname\\testmap", size); }
static qboolean InPVS(const vec3_t a, const vec3_t b) { (void)a; (void)b; return qtrue; }
static void LinkEntity(sharedEntity_t *entity) { (void)entity; }
static void Noop(void) {}
static void UpdateCvar(vmCvar_t *cvar) { (void)cvar; }
static void SetCvar(const char *name, const char *value) { (void)name; (void)value; }
static qboolean Maintain(int entity) { (void)entity; return qtrue; }
static void Usercmd(int client, usercmd_t *cmd) { (void)client; memset(cmd, 0, sizeof(*cmd)); }
static qboolean Contact(const vec3_t mins, const vec3_t maxs, const sharedEntity_t *entity, int capsule)
{
    (void)mins; (void)maxs; (void)entity; (void)capsule;
    return qfalse;
}
static void Route(gentity_t *target, gentity_t *source, gentity_t *player)
{
    (void)target; (void)source; (void)player;
    routeCalls++;
}

static gentity_t *Player(void)
{
    gentity_t *player = &g_entities[0];
    memset(&level, 0, sizeof(level));
    memset(g_entities, 0, sizeof(g_entities));
    memset(g_clients, 0, sizeof(g_clients));
    memset(&imports, 0, sizeof(imports));
    imports.Milliseconds = Clock;
    imports.SendServerCommand = ServerCommand;
    imports.GetUserinfo = Userinfo;
    imports.GetServerinfo = Serverinfo;
    imports.Cvar_VariableStringBuffer = CvarString;
    imports.Print = Print;
    imports.FS_Open = Open;
    imports.InPVS = InPVS;
    imports.LinkEntity = LinkEntity;
    imports.ICARUS_FreeEnt = LinkEntity;
    imports.EntityContact = Contact;
    imports.Cvar_Update = UpdateCvar;
    imports.Cvar_Set = SetCvar;
    imports.ROFF_UpdateEntities = Noop;
    imports.ICARUS_MaintainTaskManager = Maintain;
    imports.GetUsercmd = Usercmd;
    trap = &imports;
    level.time = 1000;
    level.gametype = GT_FFA;
    level.num_entities = MAX_CLIENTS + 2;
    level.clients = g_clients;
    level.maxclients = 1;
    g_forceRegenTime.integer = 500;
    g_fixTimerOOB.integer = 0;
    g_timeouttospec.integer = 0;
    sv_fps.integer = 20;
    sv_fps.value = 20;
    player->client = &g_clients[0];
    player->inuse = qtrue;
    player->health = 100;
    player->client->pers.connected = CON_CONNECTED;
    player->client->sess.sessionTeam = TEAM_FREE;
    player->client->sess.raceMode = qtrue;
    player->client->ps.pm_type = PM_NORMAL;
    player->client->ps.groundEntityNum = ENTITYNUM_NONE;
    player->client->ps.stats[STAT_RACEMODE] = 1;
    player->client->ps.fd.forcePower = 80;
    player->client->ps.fd.forcePowerMax = 100;
    player->client->ps.fd.forcePowerRegenDebounceTime = 1275;
    player->client->pers.stats.startTime = 777;
    player->client->pers.stats.startLevelTime = 888;
    player->client->pers.stats.coopStarted = qtrue;
    player->client->ps.duelTime = 999;
    routeCalls = commandsSent = 0;
    memset(sent, 0, sizeof(sent));
    G_FlyingRespawn_InitClient(&player->client->pers.flyingRespawn);
    return player;
}

static void StaticForceWait(void)
{
    puts("Running static Force wait"); fflush(stdout);
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnSnapshot_t contact;
    flyingRespawnCheckpointConfig_t config = {0};
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 500, 0, 0));
    player->client->ps.commandTime = 1200;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1700, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 2000, qtrue));
    CHECK(G_FlyingRespawn_HandleSecondPress(state, 2050, &contact));
    G_FlyingRespawn_WriteMovement(&player->client->ps, &contact.movement, 0);
    for (level.time = 2100; level.time <= 2500; level.time += 50)
        WP_ForcePowersUpdate(player, &player->client->pers.cmd);
    CHECK(player->client->ps.fd.forcePower == 80);
    CHECK(player->client->ps.fd.forcePowerRegenDebounceTime == 2575);
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_Advance(state, 2550, &movement, &player->client->pers.cmd));
    G_FlyingRespawn_ReleaseStaticForce(state, 2550, &player->client->ps, &player->client->jetPackDebReduce);
    CHECK(!state->mismatch);
    CHECK(player->client->ps.fd.forcePowerRegenDebounceTime == 2625);
    level.time = 2550;
    WP_ForcePowersUpdate(player, &player->client->pers.cmd);
    CHECK(player->client->ps.fd.forcePower == 80);
    G_FlyingRespawn_FreeClient(state);
}

static void FlyingFrameInput(void)
{
    puts("Running flying frame input"); fflush(stdout);
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnCheckpointConfig_t config = {0};
    usercmd_t source = {0}, live = {0};
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 100, 0, 100));
    player->client->ps.commandTime = source.serverTime = 1000;
    player->client->ps.fd.forceJumpCharge = 40;
    player->client->ps.fd.forcePowersActive = 1 << FP_LEVITATION;
    player->client->ps.pm_flags = PMF_JUMP_HELD;
    player->client->fjDidJump = qtrue;
    source.upmove = 127;
    G_FlyingRespawn_RecordExecutionCmd(state, 1000, &source);
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1100, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1200, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 2000, qtrue));
    G_FlyingRespawn_WriteMovement(&player->client->ps, &state->replay.selection.snapshot.movement, 1000);
    live.upmove = 0;
    live.forwardmove = 127;
    live.angles[YAW] = 12345;
    G_FlyingRespawn_BufferLiveCmd(state, &live);
    player->client->pers.cmd = live; /* receipt before the frame Force consumer */
    level.time = 2050;
    WP_ForcePowersUpdate(player, &player->client->pers.cmd);
    CHECK(player->client->ps.fd.forceJumpCharge == 40);
    CHECK(player->client->pers.cmd.upmove == 127);
    CHECK(state->replay.liveCmd.upmove == 0);
    CHECK(state->replay.liveCmd.angles[YAW] == 12345);
    G_FlyingRespawn_FreeClient(state);
}

static void ReplayTimerBrushes(void)
{
    puts("Running replay timer brushes"); fflush(stdout);
    gentity_t *player = Player(), *trigger = &g_entities[MAX_CLIENTS], *target = trigger + 1;
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnCheckpointConfig_t config = {0};
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 100, 0, 0));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1300, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 2000, qtrue));
    trigger->inuse = target->inuse = qtrue;
    trigger->s.number = MAX_CLIENTS;
    target->s.number = MAX_CLIENTS + 1;
    trigger->target = "route";
    target->targetname = "route";
    target->use = Route;
    trigger->classname = "df_trigger_start";
    trigger->spawnflags = 2;
    trigger->speed = 50;
    player->client->ps.velocity[0] = 100;
    level.time = 2050;
    TimerStart(trigger, player, NULL);
    CHECK(routeCalls == 1);
    CHECK(player->client->ps.velocity[0] == 50);
    CHECK(player->client->pers.stats.startTime == 777);
    CHECK(player->client->pers.stats.startLevelTime == 888);
    CHECK(player->client->ps.duelTime == 999);
    CHECK(state->replay.active);
    CHECK(state->automaticCheckpoint.valid);
    CHECK(state->respawnCount == 1);
    state->replay.active = qtrue; /* independently exercise finish despite the broken start path */
    trigger->classname = "df_trigger_finish";
    trigger->spawnflags = 0;
    trigger->aimDebounceTime = 0;
    TimerStop(trigger, player, NULL);
    CHECK(routeCalls == 2);
    CHECK(player->client->pers.stats.startTime == 777);
    CHECK(player->client->pers.stats.startLevelTime == 888);
    CHECK(player->client->ps.duelTime == 999);
    CHECK(state->replay.active);
    CHECK(commandsSent == 0);
    G_FlyingRespawn_FreeClient(state);
}

static void StaticTribesServerFrames(void)
{
    puts("Running static Tribes through G_RunFrame"); fflush(stdout);
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnCheckpointConfig_t config = {0};
    int frame;
    level.maxclients = 0; /* isolate entity/frame resources from end-of-frame prediction */
    player->client->sess.movementStyle = MV_TRIBES;
    player->client->ps.stats[STAT_MOVEMENTSTYLE] = MV_TRIBES;
    player->client->ps.eFlags |= EF_JETPACK_ACTIVE;
    player->client->ps.jetpackFuel = 100;
    player->client->overheatRechargeDebReduce = 3000;
    player->client->jetPackDebReduce = 1275;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 500, 0, 0));
    player->client->ps.commandTime = 1200;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1700, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 2000, qtrue));
    level.time = 2050;
    CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
    for (frame = 2100; frame < 2500; frame += 50)
        G_RunFrame(frame);
    CHECK(player->client->ps.fd.forcePower == 80);
    CHECK(state->replay.active);
    CHECK(player->client->jetPackDebReduce == 2575);
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_Advance(state, 2550, &movement, &player->client->pers.cmd));
    G_FlyingRespawn_ReleaseStaticForce(state, 2550, &player->client->ps, &player->client->jetPackDebReduce);
    CHECK(!state->mismatch);
    CHECK(player->client->jetPackDebReduce == 2625);
    /* Keep normal movement idle while checking the real post-GO resource path. */
    player->client->ps.commandTime = 2700;
    G_RunFrame(2620);
    CHECK(player->client->ps.fd.forcePower == 80);
    CHECK(player->client->jetPackDebReduce == 2625);
    G_RunFrame(2650);
    CHECK(player->client->ps.fd.forcePower == 76);
    CHECK(player->client->jetPackDebReduce == 2850);
    G_FlyingRespawn_FreeClient(state);
}

static void StaticServerFrameTimeout(void)
{
    puts("Running static input timeout through G_RunFrame"); fflush(stdout);
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnCheckpointConfig_t config = {0};
    level.maxclients = 0;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 500, 0, 0));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1700, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 100000, qtrue));
    level.time = 100050;
    CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
    CHECK(player->client->pers.cmd.serverTime == 100050);
    /* A zero timestamp already proves the regression; avoid entering unrelated
     * spectator spawn/teardown in that known-broken pre-fix state. */
    if (player->client->pers.cmd.serverTime) {
        usercmd_t live = {0};
        g_timeouttospec.integer = 1;
        G_RunFrame(100100);
        CHECK(player->client->sess.sessionTeam == TEAM_FREE);
        CHECK(state->replay.active);
        live.serverTime = 100140;
        live.forwardmove = live.upmove = 127;
        live.angles[YAW] = 12345;
        ClientThink(0, &live);
        CHECK(player->client->pers.cmd.serverTime == 100100);
        CHECK(player->client->pers.cmd.forwardmove == 0);
        CHECK(state->replay.liveCmd.serverTime == 100140);
        CHECK(state->replay.liveCmd.forwardmove == 127);
        CHECK(state->replay.liveCmd.upmove == 127);
        CHECK(state->replay.liveCmd.angles[YAW] == 12345);
        G_RunFrame(100150);
        CHECK(player->client->sess.sessionTeam == TEAM_FREE);
        CHECK(state->replay.active);
    }
    G_FlyingRespawn_FreeClient(state);
}

static int Scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    int value = -1;
    CHECK(sqlite3_prepare_v2(db, sql, -1, &statement, NULL) == SQLITE_OK);
    if (statement && sqlite3_step(statement) == SQLITE_ROW) value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

/* Restoring the failed attempt's cooldowns/resources would change the replay trajectory. */
static void MovementResources(void)
{
    puts("Running movement resource restore and mismatch validation");
    for (int field = 0; field < 11; ++field) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t movement;
        flyingRespawnCheckpointConfig_t config = {0};
        usercmd_t live = {0};
        playerState_t *ps = &player->client->ps;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 0, 0, 0));
        ps->commandTime = 1200;
        ps->stats[STAT_DASHTIME] = 701;
        ps->stats[STAT_WJTIME] = 302;
        ps->stats[STAT_JUMPTIME] = 203;
        ps->jetpackFuel = 37;
        ps->eFlags = EF_JETPACK_ACTIVE | EF_JETPACK | EF_JETPACK_FLAMING;
        ps->groundTime = 1050;
        player->client->fjDidJump = qtrue;
        player->client->jetPackOn = qtrue;
        player->client->jetPackDebRecharge = 1350;
        player->client->overheatRechargeDebReduce = 1400;
        ps->fd.forceRageRecoveryTime = 1500;
        G_FlyingRespawn_CaptureMovement(player, &movement);
        CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
        ps->stats[STAT_DASHTIME] = ps->stats[STAT_WJTIME] = ps->stats[STAT_JUMPTIME] = 0;
        ps->jetpackFuel = 1;
        ps->eFlags = EF_TALK;
        ps->groundTime = 1999;
        player->client->fjDidJump = qfalse;
        player->client->jetPackOn = qfalse;
        player->client->jetPackDebRecharge = player->client->overheatRechargeDebReduce = 0;
        ps->fd.forceRageRecoveryTime = 0;
        level.time = 2000;
        g_enableFlyingRespawn.integer = 1;
        CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
        CHECK(ps->stats[STAT_DASHTIME] == 701);
        CHECK(ps->stats[STAT_WJTIME] == 302);
        CHECK(ps->stats[STAT_JUMPTIME] == 203);
        CHECK(ps->jetpackFuel == 37);
        CHECK((ps->eFlags & (EF_JETPACK_ACTIVE | EF_JETPACK | EF_JETPACK_FLAMING)) ==
            (EF_JETPACK_ACTIVE | EF_JETPACK | EF_JETPACK_FLAMING));
        CHECK(ps->eFlags & EF_TALK); /* unrelated network flags remain live */
        CHECK(ps->groundTime == 1850);
        CHECK(player->client->fjDidJump);
        CHECK(player->client->jetPackOn);
        CHECK(player->client->jetPackDebRecharge == 2150);
        CHECK(player->client->overheatRechargeDebReduce == 2200);
        CHECK(ps->fd.forceRageRecoveryTime == 2300);
        if (field == 0) ps->stats[STAT_DASHTIME]++;
        if (field == 1) ps->stats[STAT_WJTIME]++;
        if (field == 2) ps->stats[STAT_JUMPTIME]++;
        if (field == 3) ps->jetpackFuel++;
        if (field == 4) ps->eFlags ^= EF_JETPACK_ACTIVE;
        if (field == 5) ps->groundTime++;
        if (field == 6) player->client->fjDidJump = !player->client->fjDidJump;
        if (field == 7) player->client->jetPackDebRecharge++;
        if (field == 8) player->client->overheatRechargeDebReduce++;
        if (field == 9) ps->fd.forceRageRecoveryTime++;
        if (field == 10) player->client->jetPackOn = !player->client->jetPackOn;
        G_FlyingRespawn_CaptureMovement(player, &movement);
        CHECK(G_FlyingRespawn_Advance(state, 2000, &movement, &player->client->pers.cmd));
        CHECK(state->runInvalid && state->mismatch);
        CHECK(player->client->pers.stats.startTime == 777);
        CHECK(player->client->pers.stats.startLevelTime == 888);
        CHECK(ps->duelTime == 999);
        G_FlyingRespawn_FreeClient(state);
    }
}

/* Exercises the actual frame resource consumers while static playback owns state. */
static void StaticJetpackResources(void)
{
    puts("Running static jetpack resources through G_RunFrame");
    for (int tribes = 0; tribes < 2; ++tribes) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t movement;
        flyingRespawnCheckpointConfig_t config = {0};
        usercmd_t live = {0};
        level.maxclients = 0;
        player->client->sess.movementStyle = tribes ? MV_TRIBES : MV_JETPACK;
        player->client->ps.stats[STAT_MOVEMENTSTYLE] = player->client->sess.movementStyle;
        player->client->ps.eFlags = EF_JETPACK_ACTIVE;
        player->client->ps.jetpackFuel = 37;
        player->client->ps.speed = 250;
        player->client->ps.commandTime = 1200;
        player->client->ps.groundTime = 1100;
        player->client->jetPackDebReduce = 1275;
        player->client->jetPackDebRecharge = 1300;
        player->client->overheatRechargeDebReduce = 1350;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 500, 0, 0));
        G_FlyingRespawn_CaptureMovement(player, &movement);
        CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
        CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1700, &movement));
        CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 2000, qtrue));
        level.time = 2050;
        CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
        for (int frame = 2100; frame < 2500; frame += 50) G_RunFrame(frame);
        CHECK(player->client->ps.jetpackFuel == 37);
        CHECK(player->client->ps.fd.forcePower == 80);
        live.serverTime = 2450;
        live.forwardmove = 127;
        live.angles[YAW] = 12345;
        ClientThink(0, &live);
        /* Execute the real late handoff, stopping before unrelated collision imports. */
        player->client->pers.connected = CON_CONNECTING;
        level.time = 2550;
        ClientThink(0, NULL);
        CHECK(!state->replay.active && !state->mismatch);
        CHECK(player->client->pers.cmd.forwardmove == 127);
        CHECK(player->client->ps.delta_angles[YAW] == -12345);
        CHECK(player->client->ps.groundTime == 2450);
        CHECK(player->client->jetPackDebReduce == 2625);
        CHECK(player->client->jetPackDebRecharge == 2650);
        CHECK(player->client->overheatRechargeDebReduce == 2700);
        CHECK(state->movementStates[state->stateHead].movement.commandTime == 2549);
        CHECK(state->movementStates[state->stateHead].movement.jetPackDebRecharge == 2650);
        G_FlyingRespawn_FreeClient(state);
    }
}

static gentity_t *CoopPartner(gentity_t *player)
{
    gentity_t *partner = &g_entities[1];
    partner->inuse = qtrue;
    partner->health = 100;
    partner->client = &g_clients[1];
    *partner->client = *player->client;
    G_FlyingRespawn_InitClient(&partner->client->pers.flyingRespawn);
    level.maxclients = 2;
    for (int i = 0; i < 2; ++i) {
        g_clients[i].ps.clientNum = i;
        g_clients[i].ps.duelInProgress = qtrue;
        g_clients[i].ps.duelIndex = 1 - i;
        g_clients[i].sess.movementStyle = g_clients[i].ps.stats[STAT_MOVEMENTSTYLE] = MV_COOP_JKA;
    }
    return partner;
}

/* The start brush starts the partner's official run too; its feature state must follow. */
static void CoopTimerStartReset(void)
{
    puts("Running co-op timer-start feature reset");
    gentity_t *player = Player(), *partner = CoopPartner(player), *start = &g_entities[MAX_CLIENTS];
    flyingRespawnState_t *state = &partner->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement = {0};
    flyingRespawnCheckpointConfig_t config = {0};
    CHECK(G_FlyingRespawn_OnTimerStart(state, 100, 0, 0, 0, 0));
    CHECK(G_FlyingRespawn_CaptureManual(state, 200, &movement));
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 300, qtrue));
    state->runInvalid = state->mismatch = state->testReplayUsed = qtrue;
    state->respawnCount = 4;
    g_flyingRespawnRewindMs.integer = 1500;
    g_flyingRespawnReleaseOffsetMs.integer = 150;
    g_flyingRespawnTestMode.integer = 1;
    level.time = 5000;
    start->inuse = qtrue;
    start->wait = 1;
    TimerStart(start, player, NULL);
    CHECK(partner->client->pers.stats.startTime == player->client->pers.stats.startTime);
    CHECK(!state->runInvalid && !state->mismatch && !state->testReplayUsed);
    CHECK(!state->automaticCheckpoint.valid && !state->automaticCheckpoint.pending);
    CHECK(!state->replay.active && state->respawnCount == 0);
    CHECK(state->rewindMs == 1500 && state->releaseOffsetMs == 150 && state->testMode);
    CHECK(G_FlyingRespawn_GetSecondaryElapsed(state, 5050) == 50);
    CHECK(state->manualSave.valid);
    G_FlyingRespawn_FreeClient(state);
    G_FlyingRespawn_FreeClient(&player->client->pers.flyingRespawn);
}

/* Check output at thirds of a 1.5-second replay, including the unchanged static deadline. */
static void ProgressCountdown(void)
{
    puts("Running progress countdown and GO output");
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnCheckpointConfig_t config = {0};
    usercmd_t live = {0};
    player->client->ps.weapon = WP_SABER;
    player->client->ps.weaponstate = WEAPON_FIRING;
    player->client->ps.weaponTime = 321;
    player->client->ps.saberMove = LS_A_TL2BR;
    player->client->ps.fd.saberAnimLevel = SS_STRONG;
    player->client->ps.fd.saberDrawAnimLevel = SS_STRONG;
    player->client->ps.fd.saberAnimLevelBase = SS_STRONG;
    player->client->ps.saberHolstered = 0;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 1500, 0, 0));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
    CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 2700, &movement));
    player->client->ps.weapon = WP_DISRUPTOR;
    player->client->ps.weaponstate = WEAPON_READY;
    player->client->ps.weaponTime = 0;
    player->client->ps.saberMove = LS_READY;
    player->client->ps.fd.saberAnimLevel = SS_FAST;
    player->client->ps.fd.saberDrawAnimLevel = SS_FAST;
    player->client->ps.fd.saberAnimLevelBase = SS_FAST;
    player->client->ps.saberHolstered = 2;
    level.time = 4000;
    g_enableFlyingRespawn.integer = 1;
    CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
    CHECK(player->client->ps.weapon == WP_SABER);
    CHECK(player->client->ps.weaponstate == WEAPON_FIRING);
    CHECK(player->client->ps.weaponTime == 321);
    CHECK(player->client->ps.saberMove == LS_A_TL2BR);
    CHECK(player->client->ps.fd.saberAnimLevel == SS_STRONG);
    CHECK(player->client->ps.fd.saberDrawAnimLevel == SS_STRONG);
    CHECK(player->client->ps.fd.saberAnimLevelBase == SS_STRONG);
    CHECK(player->client->ps.saberHolstered == 0);
    CHECK(commandsSent == 1 && !strcmp(sent[0].text, "cp \"^33\""));
    level.time = 4500;
    CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
    CHECK(commandsSent == 2 && !strcmp(sent[1].text, "cp \"^32\""));
    level.time = 5000;
    live.serverTime = 5000;
    live.forwardmove = 127;
    live.buttons = BUTTON_ATTACK;
    live.weapon = WP_DISRUPTOR;
    live.angles[PITCH] = 1000;
    live.angles[YAW] = 12345;
    live.angles[ROLL] = -500;
    ClientThink(0, &live);
    CHECK(commandsSent == 3 && !strcmp(sent[2].text, "cp \"^31\""));
    CHECK(state->replay.releaseServerTime == 5500);
    /* Isolate presentation/handoff from collision and renderer imports after GO. */
    player->client->pers.connected = CON_CONNECTING;
    level.time = 5500;
    ClientThink(0, &live);
    CHECK(!state->replay.active && !state->mismatch);
    CHECK(commandsSent == 4 && !strcmp(sent[3].text, "cp \"^2GO!\""));
    CHECK(player->client->pers.cmd.forwardmove == 127);
    CHECK(player->client->pers.cmd.buttons == BUTTON_ATTACK);
    CHECK(player->client->pers.cmd.weapon == WP_SABER);
    CHECK(player->client->ps.delta_angles[PITCH] == -1000);
    CHECK(player->client->ps.delta_angles[YAW] == -12345);
    CHECK(player->client->ps.delta_angles[ROLL] == 500);
    G_FlyingRespawn_FreeClient(state);
}

/* Catches public-format/audience changes and secondary time leaking to spectators. */
static void FinishPublicPrivate(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    char playerName[] = "Runner", style[] = "jka", time[] = "9.500";
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 0, 0, 0));
    G_FlyingRespawn_ResetSecondarySegment(state, 9000, 3000);
    state->respawnCount = 2;
    level.time = 9500;
    PrintRaceTime(NULL, playerName, NULL, style, 123, 45, time, 0, 0, qfalse, 0, qfalse, qtrue, 0, 0, 0, -1, -1);
    CHECK(commandsSent == 2);
    CHECK(sent[0].client == -1);
    CHECK(!strcmp(sent[0].text, "print \"^2Completed in ^39.500       ^2 max:^3123       ^2 avg:^345        ^2 style:^3jka       ^2 by ^7Runner ^7\n\" dffinish_japro_valid"));
    CHECK(sent[1].client == 0);
    CHECK(strstr(sent[1].text, "no-respawn") != NULL);
    CHECK(strstr(sent[1].text, "3.500") != NULL);
    CHECK(strstr(sent[1].text, "2") != NULL);
    CHECK(player->client->pers.stats.startTime == 777);
    CHECK(player->client->pers.stats.startLevelTime == 888);
    CHECK(player->client->ps.duelTime == 999);
    G_FlyingRespawn_FreeClient(state);
}

/* Catches missing metadata on INSERT/UPDATE and mismatch/test pollution of PBs. */
static void FinishPersistence(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    sqlite3 *db = NULL;
    char user[] = "runner";
    InitGameAccountStuff();
    InitGameAccountStuff(); /* fresh schema and repeated map initialization */
    CHECK(sqlite3_open("./data.db", &db) == SQLITE_OK);
    CHECK(Scalar(db, "SELECT count(*) FROM sqlite_master WHERE name='LocalRunInvalid'") == 1);
    CHECK(sqlite3_exec(db, "INSERT INTO LocalSecretCourse (id, course_name, type, starts_at, applied_revision) VALUES ('active', 'testmap', 1, 0, 'fixture')", NULL, NULL, NULL) == SQLITE_OK);
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 0, 0, 0));
    state->respawnCount = 2;
    G_AddRaceTime(user, NULL, 9500, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT respawn_count FROM LocalRun WHERE username='runner'") == 2);
    CHECK(Scalar(db, "SELECT invalid FROM LocalRun WHERE username='runner'") == 0);
    CHECK(Scalar(db, "SELECT rank FROM LocalRun WHERE username='runner'") == 1);
    state->respawnCount = 3;
    G_AddRaceTime(user, NULL, 8500, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT respawn_count FROM LocalRun WHERE username='runner'") == 3);
    state->respawnCount = 9;
    G_AddRaceTime(user, NULL, 9000, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT respawn_count FROM LocalRun WHERE username='runner'") == 3);
    memset(cosmeticUnlocks, 0, sizeof(cosmeticUnlocks));
    Q_strncpyz(cosmeticUnlocks[0].mapname, "testmap", sizeof(cosmeticUnlocks[0].mapname));
    cosmeticUnlocks[0].style = MV_JKA;
    cosmeticUnlocks[0].duration = 2000;
    cosmeticUnlocks[0].bitvalue = 1;
    state->runInvalid = state->mismatch = qtrue;
    state->respawnCount = 4;
    commandsSent = 0;
    G_AddRaceTime(user, NULL, 1000, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT duration_ms FROM LocalRun WHERE username='runner'") == 8500);
    CHECK(Scalar(db, "SELECT respawn_count FROM LocalRun WHERE username='runner'") == 3);
    CHECK(Scalar(db, "SELECT rank FROM LocalRun WHERE username='runner'") == 1);
    CHECK(Scalar(db, "SELECT season_rank FROM LocalRun WHERE username='runner'") == 1);
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid WHERE username='runner' AND coursename='testmap' AND duration_ms=1000 AND invalid=1 AND respawn_count=4 AND season=6") == 1);
    CHECK(Scalar(db, "SELECT duration_ms FROM LocalSecretCourseRun WHERE username='runner'") == 8500);
    CHECK(player->client->pers.unlocks == 0);
    for (int i = 0; i < commandsSent && i < 64; i++) CHECK(sent[i].client == 0);
    state->testReplayUsed = qtrue;
    state->runInvalid = state->mismatch = qfalse;
    commandsSent = 0;
    G_AddRaceTime(user, NULL, 500, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRun") == 1);
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == 1);
    CHECK(Scalar(db, "SELECT duration_ms FROM LocalRun WHERE username='runner'") == 8500);
    CHECK(Scalar(db, "SELECT duration_ms FROM LocalSecretCourseRun WHERE username='runner'") == 8500);
    CHECK(commandsSent == 0);
    state->runInvalid = state->mismatch = qtrue;
    G_AddRaceTime(user, NULL, 250, MV_JKA, 0, 0, 0, -1, -1);
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == 1);
    CHECK(commandsSent == 0);
    memset(cosmeticUnlocks, 0, sizeof(cosmeticUnlocks));
    sqlite3_close(db);
    G_FlyingRespawn_FreeClient(state);
}

/* Catches a missing command category in actual /aminfo output. */
static void FinishHelp(void)
{
    gentity_t *player = Player();
    int found = 0;
    g_raceMode.integer = 1;
    Cmd_Aminfo_f(player);
    for (int i = 0; i < commandsSent && i < 64; i++) {
        if (strstr(sent[i].text, "Respawn & practice")) {
            CHECK(sent[i].client == 0);
            CHECK(strstr(sent[i].text, "/savepos (practice bookmark)") != NULL);
            CHECK(strstr(sent[i].text, "/respos (practice)") != NULL);
            CHECK(strstr(sent[i].text, "/respawn (map checkpoint)") != NULL);
            found++;
        }
    }
    CHECK(found == 1);
    G_FlyingRespawn_FreeClient(&player->client->pers.flyingRespawn);
}

/* Catches one co-op partner submitting a shared result the other cannot rank. */
static void FinishCoopEligibility(void)
{
    for (int mode = 0; mode < 3; mode++) {
        for (int owner = 0; owner < 2; owner++) {
            gentity_t *player = Player(), *partner = &g_entities[1];
            gentity_t *finish = &g_entities[MAX_CLIENTS];
            sqlite3 *db = NULL;
            flyingRespawnState_t *flagged;
            fprintf(stderr, "Running co-op finish mode=%d flagged partner=%d\n", mode, owner);
            level.maxclients = 2;
            level.time = level.frameStartTime = 5000;
            g_stepSlideFix.integer = g_smoothClients.integer = 1;
            timescale.value = 1.0f;
            partner->inuse = qtrue;
            partner->client = &g_clients[1];
            *partner->client = *player->client;
            G_FlyingRespawn_InitClient(&partner->client->pers.flyingRespawn);
            for (int i = 0; i < 2; i++) {
                gclient_t *client = &g_clients[i];
                client->ps.clientNum = i;
                client->ps.duelInProgress = qtrue;
                client->ps.duelIndex = 1 - i;
                client->ps.stats[STAT_MOVEMENTSTYLE] = client->sess.movementStyle = MV_COOP_JKA;
                client->pers.cmd.serverTime = 5000;
                Q_strncpyz(client->pers.userName, i ? "booster" : "runner", sizeof(client->pers.userName));
                CHECK(G_FlyingRespawn_OnTimerStart(&client->pers.flyingRespawn, 1000, 0, 0, 0, 0));
                client->pers.flyingRespawn.respawnCount = i + 2;
            }
            InitGameAccountStuff();
            CHECK(sqlite3_open("./data.db", &db) == SQLITE_OK);
            CHECK(sqlite3_exec(db,
                "DELETE FROM LocalRun; DELETE FROM LocalRunInvalid; DELETE FROM LocalSecretCourseRun; DELETE FROM LocalSecretCourse;"
                "INSERT INTO LocalSecretCourse (id, course_name, type, starts_at, applied_revision) VALUES ('coop', 'testmap', 1, 0, 'fixture');"
                "INSERT INTO LocalRun (username, coursename, duration_ms, style, season, rank, season_rank, invalid, respawn_count) VALUES ('runner', 'testmap', 10000, 15, 6, 1, 1, 0, 0), ('booster', 'testmap', 10000, 15, 6, 1, 1, 0, 0);"
                "INSERT INTO LocalSecretCourseRun (secret_course_id, username, style, duration_ms, end_time) VALUES ('coop', 'runner', 15, 10000, 1), ('coop', 'booster', 15, 10000, 1)",
                NULL, NULL, NULL) == SQLITE_OK);
            memset(cosmeticUnlocks, 0, sizeof(cosmeticUnlocks));
            Q_strncpyz(cosmeticUnlocks[0].mapname, "testmap", sizeof(cosmeticUnlocks[0].mapname));
            cosmeticUnlocks[0].style = MV_COOP_JKA;
            cosmeticUnlocks[0].duration = 5000;
            cosmeticUnlocks[0].bitvalue = 1;
            flagged = &g_clients[owner].pers.flyingRespawn;
            flagged->testReplayUsed = mode != 1;
            if (mode == 1) flagged->runInvalid = qtrue;
            if (mode == 2) g_clients[1 - owner].pers.flyingRespawn.runInvalid = qtrue;
            finish->inuse = qtrue;
            finish->wait = 1;
            commandsSent = 0;
            TimerStop(finish, player, NULL);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRun WHERE duration_ms=10000 AND rank=1 AND season_rank=1 AND respawn_count=0") == 2);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalSecretCourseRun WHERE duration_ms=10000") == 2);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == (mode == 1 ? 2 : 0));
            if (mode == 1) {
                CHECK(Scalar(db, "SELECT respawn_count FROM LocalRunInvalid WHERE username='runner' AND invalid=1") == 2);
                CHECK(Scalar(db, "SELECT respawn_count FROM LocalRunInvalid WHERE username='booster' AND invalid=1") == 3);
            }
            CHECK(g_clients[0].pers.unlocks == 0 && g_clients[1].pers.unlocks == 0);
            CHECK(g_clients[0].pers.stats.startTime == 0 && g_clients[1].pers.stats.startTime == 0);
            for (int i = 0; i < commandsSent && i < 64; i++) {
                CHECK(sent[i].client == 0 || sent[i].client == 1);
                CHECK(strstr(sent[i].text, "dffinish_") == NULL);
            }
            if (mode != 1) CHECK(commandsSent == 0);
            CHECK(g_clients[owner].pers.flyingRespawn.testReplayUsed == (mode != 1));
            CHECK(g_clients[1 - owner].pers.flyingRespawn.testReplayUsed == qfalse);
            CHECK(g_clients[owner].pers.flyingRespawn.runInvalid == (mode == 1));
            CHECK(g_clients[1 - owner].pers.flyingRespawn.runInvalid == (mode == 2));
            sqlite3_close(db);
            G_FlyingRespawn_FreeClient(&g_clients[0].pers.flyingRespawn);
            G_FlyingRespawn_FreeClient(&g_clients[1].pers.flyingRespawn);
            memset(cosmeticUnlocks, 0, sizeof(cosmeticUnlocks));
        }
    }
}

/* A shared result cannot be committed until both replay handoffs have validated. */
static void FinishCoopActiveReplay(void)
{
    puts("Running co-op finish deferred through partner validation");
    for (int mismatch = 0; mismatch < 2; ++mismatch) {
        for (int owner = 0; owner < 2; ++owner) {
            gentity_t *player = Player();
            CoopPartner(player);
            gentity_t *finish = &g_entities[MAX_CLIENTS];
            flyingRespawnState_t *state = &g_clients[owner].pers.flyingRespawn;
            flyingRespawnMovement_t movement = {0};
            flyingRespawnCheckpointConfig_t config = {0};
            usercmd_t cmd = {0};
            sqlite3 *db = NULL;
            level.time = level.frameStartTime = 5000;
            g_stepSlideFix.integer = g_smoothClients.integer = 1;
            timescale.value = 1.0f;
            for (int i = 0; i < 2; ++i) {
                g_clients[i].pers.cmd.serverTime = 5000;
                Q_strncpyz(g_clients[i].pers.userName, i ? "booster" : "runner", sizeof(g_clients[i].pers.userName));
            }
            CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 0, 0, 0));
            movement.commandTime = 1200;
            CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1200, 20, &config, &movement));
            CHECK(G_FlyingRespawn_BeginCheckpointReplay(state, 5000, qtrue));
            InitGameAccountStuff();
            CHECK(sqlite3_open("./data.db", &db) == SQLITE_OK);
            CHECK(sqlite3_exec(db, "DELETE FROM LocalRun; DELETE FROM LocalRunInvalid; DELETE FROM LocalSecretCourseRun; DELETE FROM LocalSecretCourse", NULL, NULL, NULL) == SQLITE_OK);
            finish->inuse = qtrue;
            finish->wait = 1;
            commandsSent = 0;
            TimerStop(finish, player, NULL);
            CHECK(g_clients[0].pers.stats.startTime == 777 && g_clients[1].pers.stats.startTime == 777);
            CHECK(g_clients[0].ps.duelTime == 999 && g_clients[1].ps.duelTime == 999);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRun") == 0);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == 0);
            CHECK(commandsSent == 0);
            movement.commandTime = 5000;
            movement.origin[0] = mismatch ? 10 : 0;
            CHECK(G_FlyingRespawn_Advance(state, 5000, &movement, &cmd));
            CHECK(state->runInvalid == mismatch);
            TimerStop(finish, player, NULL);
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRun") == (mismatch ? 0 : 2));
            CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == (mismatch ? 2 : 0));
            CHECK(!g_clients[0].pers.stats.startTime && !g_clients[1].pers.stats.startTime);
            sqlite3_close(db);
            G_FlyingRespawn_FreeClient(&g_clients[0].pers.flyingRespawn);
            G_FlyingRespawn_FreeClient(&g_clients[1].pers.flyingRespawn);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "finish")) {
        StaticForceWait();
        FlyingFrameInput();
        ReplayTimerBrushes();
        StaticTribesServerFrames();
        StaticServerFrameTimeout();
        MovementResources();
        StaticJetpackResources();
        CoopTimerStartReset();
        ProgressCountdown();
    }
    FinishPublicPrivate();
    FinishPersistence();
    FinishHelp();
    FinishCoopEligibility();
    FinishCoopActiveReplay();
    if (failures) fprintf(stderr, "%d integration assertions failed\n", failures);
    else puts(argc > 1 ? "5 flying-respawn finish integration cases passed" : "14 flying-respawn integration cases passed");
    return failures ? 1 : 0;
}
