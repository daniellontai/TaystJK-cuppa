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

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "finish")) {
        StaticForceWait();
        FlyingFrameInput();
        ReplayTimerBrushes();
        StaticTribesServerFrames();
        StaticServerFrameTimeout();
    }
    FinishPublicPrivate();
    FinishPersistence();
    FinishHelp();
    FinishCoopEligibility();
    if (failures) fprintf(stderr, "%d integration assertions failed\n", failures);
    else puts(argc > 1 ? "4 flying-respawn finish integration cases passed" : "9 flying-respawn integration cases passed");
    return failures ? 1 : 0;
}
