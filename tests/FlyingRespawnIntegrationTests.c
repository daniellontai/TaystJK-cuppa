/* Links the real game module objects; only engine imports are replaced. */
#include "g_local.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

void TimerStart(gentity_t *, gentity_t *, trace_t *);
void TimerStop(gentity_t *, gentity_t *, trace_t *);
void TimerCheckpoint(gentity_t *, gentity_t *, trace_t *);
void SP_target_restrict(gentity_t *);
void WP_ForcePowersUpdate(gentity_t *, usercmd_t *);
void G_RunFrame(int);
void G_RunClient(gentity_t *);
void InitGameAccountStuff(void);
void Cmd_Aminfo_f(gentity_t *);
void G_AddRaceTime(char *, char *, int, int, int, int, int, int, int);
void PrintRaceTime(char *, char *, char *, char *, int, int, char *, int, int, qboolean, int, qboolean, qboolean, int, int, float, int, int);
extern gclient_t g_clients[MAX_CLIENTS];

static gameImport_t imports;
static int failures, routeCalls, commandsSent, fixtureBrush;
static struct { int client; char text[2048]; } sent[64];
static int debugSentCount;
static char debugSent[64][2048];
static char commandName[MAX_TOKEN_CHARS], commandArg[MAX_TOKEN_CHARS];
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); failures++; \
} } while (0)

static int cleanupSuccesses;
static int testWallTimeMs;
static void SetTimes(int serverTime, int wallTimeMs) { level.time = serverTime; testWallTimeMs = wallTimeMs; }
static int Clock(void) { return testWallTimeMs; }
static int Argc(void) { return commandName[0] ? (commandArg[0] ? 2 : 1) : 0; }
static void Argv(int n, char *buffer, int size)
{
    if (n == 0) Q_strncpyz(buffer, commandName, size);
    else if (n == 1) Q_strncpyz(buffer, commandArg, size);
    else if (size) buffer[0] = 0;
}
static void ServerCommand(int client, const char *text) {
    if (commandsSent < 64) {
        sent[commandsSent].client = client;
        Q_strncpyz(sent[commandsSent].text, text, sizeof(sent[commandsSent].text));
    }
    commandsSent++;
}
static void CvarString(const char *name, char *buffer, int size) { (void)name; Q_strncpyz(buffer, ".", size); }
static void Print(const char *format, ...) {
    char line[2048];
    va_list args, copy;
    if (strstr(format, "Cleaned up racetimes")) cleanupSuccesses++;
    va_start(args, format);
    va_copy(copy, args);
    vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (!strncmp(line, "FRDBG ", 6)) {
        if (debugSentCount < 64) Q_strncpyz(debugSent[debugSentCount], line, sizeof(debugSent[debugSentCount]));
        debugSentCount++;
    }
    vfprintf(stderr, format, args);
    va_end(args);
}
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
    return fixtureBrush && entity == (sharedEntity_t *)&g_entities[MAX_CLIENTS];
}
static void EmptyTrace(trace_t *trace, const vec3_t start, const vec3_t mins, const vec3_t maxs,
    const vec3_t end, int pass, int mask, int capsule, int flags, int lod)
{
    (void)start; (void)mins; (void)maxs; (void)pass; (void)mask; (void)capsule; (void)flags; (void)lod;
    memset(trace, 0, sizeof(*trace)); trace->fraction = 1; trace->entityNum = ENTITYNUM_NONE; VectorCopy(end, trace->endpos);
}
static int EmptyContents(const vec3_t point, int pass) { (void)point; (void)pass; return 0; }
static int EmptyEntities(const vec3_t mins, const vec3_t maxs, int *list, int count)
{ (void)mins; (void)maxs; if (fixtureBrush && count > 0) { list[0] = MAX_CLIENTS; return 1; } return 0; }

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
    imports.Argc = Argc;
    imports.Argv = Argv;
    imports.GetUserinfo = Userinfo;
    imports.GetServerinfo = Serverinfo;
    imports.Cvar_VariableStringBuffer = CvarString;
    imports.Print = Print;
    imports.FS_Open = Open;
    imports.InPVS = InPVS;
    imports.LinkEntity = LinkEntity;
    imports.UnlinkEntity = LinkEntity;
    imports.Trace = EmptyTrace;
    imports.PointContents = EmptyContents;
    imports.EntitiesInBox = EmptyEntities;
    imports.ICARUS_FreeEnt = LinkEntity;
    imports.EntityContact = Contact;
    imports.Cvar_Update = UpdateCvar;
    imports.Cvar_Set = SetCvar;
    imports.ROFF_UpdateEntities = Noop;
    imports.ICARUS_MaintainTaskManager = Maintain;
    imports.GetUsercmd = Usercmd;
    trap = &imports;
    SetTimes(1000, 5000);
    level.gametype = GT_FFA;
    level.num_entities = MAX_CLIENTS + 2;
    level.clients = g_clients;
    level.maxclients = 1;
    g_forceRegenTime.integer = 500;
    g_raceMode.integer = 1;
    g_forceLogin.integer = 0;
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
    routeCalls = commandsSent = fixtureBrush = 0;
    debugSentCount = 0;
    memset(sent, 0, sizeof(sent));
    G_FlyingRespawn_InitClient(&player->client->pers.flyingRespawn);
    return player;
}

static void RunCommand(const char *command)
{
    Q_strncpyz(commandName, command, sizeof(commandName));
    ClientCommand(0);
    commandName[0] = 0;
}

static void RunCommandWithArg(const char *command, const char *arg)
{
    Q_strncpyz(commandArg, arg, sizeof(commandArg));
    RunCommand(command);
    commandArg[0] = 0;
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

/* Account initialization always opens ./data.db. Each schema case owns its cwd,
 * including when the executable runs all cases in one process. */
typedef struct {
    char *originalDirectory;
    char directory[80];
    sqlite3 *db;
} RespawnSchemaFixture;

static int BeginRespawnSchema(RespawnSchemaFixture *fixture, int existing)
{
    memset(fixture, 0, sizeof(*fixture));
    Player();
    fixture->originalDirectory = getcwd(NULL, 0);
    CHECK(fixture->originalDirectory != NULL);
    if (!fixture->originalDirectory) return 0;
    strcpy(fixture->directory, "/tmp/flying-respawn-schema-XXXXXX");
    if (!mkdtemp(fixture->directory)) {
        CHECK(0); free(fixture->originalDirectory); return 0;
    }
    if (chdir(fixture->directory) != 0) {
        CHECK(0); rmdir(fixture->directory); free(fixture->originalDirectory); return 0;
    }
    CHECK(sqlite3_open("./data.db", &fixture->db) == SQLITE_OK);
    CHECK(sqlite3_exec(fixture->db,
        "CREATE TABLE LocalRun(id INTEGER PRIMARY KEY, username VARCHAR(16), coursename VARCHAR(40), "
        "duration_ms UNSIGNED INTEGER, topspeed UNSIGNED SMALLINT, average UNSIGNED SMALLINT, "
        "style UNSIGNED TINYINT, season UNSIGNED TINYINT, end_time UNSIGNED INTEGER, "
        "rank UNSIGNED SMALLINT, entries UNSIGNED SMALLINT, season_rank UNSIGNED SMALLINT, "
        "season_entries UNSIGNED SMALLINT, last_update UNSIGNED INTEGER, invalid UNSIGNED TINYINT, "
        "respawn_count_old INTEGER); "
        "INSERT INTO LocalRun VALUES(1, 'historical', 'testmap', 1234, 900, 800, 1, 6, "
        "1700000000, 2, 3, 4, 5, 1700000001, 2, 8)", NULL, NULL, NULL) == SQLITE_OK);
    if (existing) {
        CHECK(sqlite3_exec(fixture->db,
            "ALTER TABLE LocalRun ADD COLUMN respawn_count INTEGER NOT NULL DEFAULT 0; "
            "UPDATE LocalRun SET respawn_count=7 WHERE id=1", NULL, NULL, NULL) == SQLITE_OK);
    }
    return 1;
}

static void CheckHistoricalRespawnRow(sqlite3 *db)
{
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRun") == 1);
    CHECK(Scalar(db, "SELECT count(*) FROM LocalRun WHERE id=1 AND username='historical' "
        "AND coursename='testmap' AND duration_ms=1234 AND topspeed=900 AND average=800 "
        "AND style=1 AND season=6 AND end_time=1700000000 AND rank=2 AND entries=3 "
        "AND season_rank=4 AND season_entries=5 AND last_update=1700000001 AND invalid=2") == 1);
    CHECK(Scalar(db, "SELECT respawn_count_old FROM LocalRun WHERE id=1") == 8);
}

static void EndRespawnSchema(RespawnSchemaFixture *fixture)
{
    if (fixture->db) CHECK(sqlite3_close(fixture->db) == SQLITE_OK);
    CHECK(unlink("./data.db") == 0);
    if (fixture->originalDirectory) {
        CHECK(chdir(fixture->originalDirectory) == 0);
        free(fixture->originalDirectory);
    }
    CHECK(rmdir(fixture->directory) == 0);
}

static void RespawnSchemaHistorical(void)
{
    RespawnSchemaFixture fixture;
    puts("Running historical respawn schema through account initialization");
    if (!BeginRespawnSchema(&fixture, 0)) return;
    InitGameAccountStuff();
    InitGameAccountStuff();
    CHECK(commandsSent == 0);
    CheckHistoricalRespawnRow(fixture.db);
    CHECK(Scalar(fixture.db, "SELECT respawn_count FROM LocalRun WHERE id=1") == 0);
    EndRespawnSchema(&fixture);
}

static void RespawnSchemaExisting(void)
{
    RespawnSchemaFixture fixture;
    puts("Running existing respawn schema through repeated account initialization");
    if (!BeginRespawnSchema(&fixture, 1)) return;
    InitGameAccountStuff();
    InitGameAccountStuff();
    CHECK(commandsSent == 0);
    CheckHistoricalRespawnRow(fixture.db);
    CHECK(Scalar(fixture.db, "SELECT respawn_count FROM LocalRun WHERE id=1") == 7);
    EndRespawnSchema(&fixture);
}

static void RespawnSchemaError(void)
{
    RespawnSchemaFixture fixture;
    int migrationErrors = 0, targetColumns = 0;
    sqlite3_stmt *columns = NULL;
    puts("Running locked respawn schema through account initialization");
    if (!BeginRespawnSchema(&fixture, 0)) return;
    CHECK(sqlite3_exec(fixture.db, "BEGIN EXCLUSIVE", NULL, NULL, NULL) == SQLITE_OK);
    cleanupSuccesses = 0;
    fflush(stdout);
    fprintf(stderr, "Begin expected SQLite lock diagnostics:\n");
    InitGameAccountStuff();
    fprintf(stderr, "End expected SQLite lock diagnostics.\n");
    for (int i = 0; i < commandsSent && i < 64; ++i) {
        if (strstr(sent[i].text, "SQL Flying Respawn Migration Failed 5")) migrationErrors++;
    }
    CHECK(migrationErrors == 1); /* SQLITE_BUSY from the other connection's lock. */
    CHECK(cleanupSuccesses == 0);
    CHECK(sqlite3_exec(fixture.db, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK);
    CheckHistoricalRespawnRow(fixture.db);
    CHECK(sqlite3_prepare_v2(fixture.db, "PRAGMA table_info(LocalRun)", -1, &columns, NULL) == SQLITE_OK);
    while (columns && sqlite3_step(columns) == SQLITE_ROW) {
        if (!strcmp((const char *)sqlite3_column_text(columns, 1), "respawn_count")) targetColumns++;
    }
    sqlite3_finalize(columns);
    CHECK(targetColumns == 0);
    EndRespawnSchema(&fixture);
}

/* Restoring the failed attempt's cooldowns/resources would change the replay trajectory. */
static void MovementResources(void)
{
    puts("Running movement resource restore and mismatch validation");
    for (int field = 0; field < 14; ++field) {
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
        ps->fd.forcePowerLevel[FP_LEVITATION] = 1;
        player->client->savedJumpLevel = 2;
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
        if (field == 11) ps->fd.forcePowerLevel[FP_LEVITATION]++;
        if (field == 12) player->client->savedJumpLevel++;
        G_FlyingRespawn_CaptureMovement(player, &movement);
        CHECK(G_FlyingRespawn_Advance(state, 2000, &movement, &player->client->pers.cmd));
        CHECK(state->runInvalid == (field < 13) && state->mismatch == (field < 13));
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

/* A practice bookmark must not move a spectator, and must restore its saved respawn audit count. */
static void ManualReplayEligibilityAndRespawnCount(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;

    player->client->pers.practice = qtrue;
    player->client->ps.origin[0] = 42;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_CaptureManual(state, level.time, &movement));
    player->client->ps.origin[0] = 99;
    player->client->sess.sessionTeam = TEAM_SPECTATOR;
    RunCommand("respos");
    CHECK(player->client->ps.origin[0] == 99);
    CHECK(!state->replay.active);
    G_FlyingRespawn_FreeClient(state);

    player = Player();
    state = &player->client->pers.flyingRespawn;
    player->client->pers.practice = qtrue;
    CHECK(G_FlyingRespawn_OnTimerStart(state, level.time, 0, 0, 0, 0));
    state->respawnCount = 2;
    RunCommand("savepos");
    state->respawnCount = 7;
    RunCommand("respos");
    CHECK(state->respawnCount == 2);
    G_FlyingRespawn_FreeClient(state);
}

static void SynchronousReceiptsCoalesce(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    usercmd_t first = {0}, latest = {0}, replay = {0};
    int oldSynchronous = g_synchronousClients.integer;
    g_synchronousClients.integer = 1;
    player->client->pers.practice = qtrue;
    player->client->ps.commandTime = 1000;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    level.time = 1050;
    first.serverTime = 1048; first.forwardmove = 127; first.upmove = 127;
    ClientThink(0, &first);
    level.time = 1070;
    latest.serverTime = 1068; latest.forwardmove = -32; latest.angles[YAW] = 12345;
    ClientThink(0, &latest);
    CHECK(state->executionCount == 0);
    level.time = 1100;
    G_RunClient(player);
    CHECK(state->executionCount == 1);
    CHECK(G_SavePosition(player, qfalse));
    CHECK(state->manualSave.commandCount == 1);
    CHECK(G_FlyingRespawn_BeginManualReplay(state, 2000, qtrue));
    CHECK(!G_FlyingRespawn_OverrideCmd(state, 2099, &replay));
    CHECK(G_FlyingRespawn_OverrideCmd(state, 2100, &replay));
    CHECK(replay.serverTime == 2100);
    CHECK(replay.forwardmove == -32);
    CHECK(replay.upmove == 0);
    CHECK(replay.angles[YAW] == 12345);
    CHECK(!G_FlyingRespawn_OverrideCmd(state, 2100, &replay));
    g_synchronousClients.integer = oldSynchronous;
    G_FlyingRespawn_FreeClient(state);
}

static void ManualSkipDeadlines(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    player->client->pers.practice = qtrue;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 600, 500, 0, 0, 500));
    {
        flyingRespawnMovement_t initial;
        G_FlyingRespawn_CaptureMovement(player, &initial);
        G_FlyingRespawn_RecordState(state, 600, &initial);
    }
    SetTimes(1100, 20000);
    player->client->pers.stats.startTime = 11500;
    player->client->ps.commandTime = 1099;
    player->client->ps.fd.forcePowerRegenDebounceTime = 1500;
    player->client->jetPackDebRecharge = 1600;
    player->client->jetPackDebReduce = 0;
    player->client->ps.fd.forcePowerDebounce[FP_SPEED] = 0;
    player->client->ps.weaponTime = 321;
    player->client->ps.pm_time = 123;
    RunCommand("savepos");
    SetTimes(1750, 22000);
    player->client->pers.stats.startTime = 1000;
    RunCommand("respos");
    SetTimes(2050, 22300);
    RunCommand("respos");
    CHECK(player->client->ps.fd.forcePowerRegenDebounceTime == 2450);
    CHECK(player->client->jetPackDebRecharge == 2550);
    CHECK(player->client->jetPackDebReduce == 0);
    CHECK(player->client->ps.fd.forcePowerDebounce[FP_SPEED] == 0);
    CHECK(player->client->ps.weaponTime == 321);
    CHECK(player->client->ps.pm_time == 123);
    CHECK(Clock() - player->client->pers.stats.startTime == 8500);
    CHECK(player->client->ps.commandTime == 3399); /* saved 1099 + 2300 ms wall elapsed */
    G_FlyingRespawn_FreeClient(state);
}

static void ReplayRejectedBeginAndConfiguration(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    flyingRespawnSnapshot_t snapshot;
    flyingRespawnCmd_t frozen;
    usercmd_t input = {0};
    int startTime;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    input.serverTime = 1050; input.forwardmove = 37;
    G_FlyingRespawn_RecordExecutionCmd(state, 1050, &input);
    level.time = 1100;
    player->client->pers.practice = qtrue;
    RunCommand("savepos");
    CHECK(state->manualSave.commandCount == 1);
    snapshot = state->manualSave.snapshot;
    frozen = state->manualSave.commands[0];
    player->client->pers.cmd = input;
    player->client->ps.origin[0] = 999;
    player->client->pers.practice = qfalse;
    startTime = player->client->pers.stats.startTime;
    level.time = 2000;
    CHECK(!G_FlyingRespawn_StartManualReplay(player));
    CHECK(!G_FlyingRespawn_Configure(state, -1, 0, 0));
    CHECK(!(player->client->ps.eFlags & EF_FLYING_RESPAWN));
    CHECK(!state->replay.active);
    CHECK(!memcmp(&player->client->pers.cmd, &input, sizeof(input)));
    CHECK(player->client->ps.origin[0] == 999);
    CHECK(player->client->pers.stats.startTime == startTime);
    CHECK(!memcmp(&state->manualSave.snapshot, &snapshot, sizeof(snapshot)));
    CHECK(state->manualSave.commandCount == 1);
    CHECK(!memcmp(&state->manualSave.commands[0], &frozen, sizeof(frozen)));
    player->client->pers.practice = qtrue;
    CHECK(G_FlyingRespawn_StartManualReplay(player));
    CHECK(state->replay.active);
    CHECK(player->client->ps.eFlags & EF_FLYING_RESPAWN);
    level.time = 2050;
    CHECK(G_FlyingRespawn_StartManualReplay(player));
    CHECK(!state->replay.active);
    CHECK(!memcmp(&state->manualSave.commands[0], &frozen, sizeof(frozen)));
    G_FlyingRespawn_FreeClient(state);
}

static void ManualReleaseHistory(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    player->client->pers.practice = qtrue;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 600, 500, 0, 0, 500));
    player->client->ps.origin[0] = 7;
    player->client->ps.commandTime = 1099;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 600, &movement);
    SetTimes(1100, 20000);
    RunCommand("savepos");
    player->client->ps.origin[0] = 999;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1600, &movement);
    SetTimes(1750, 20650);
    RunCommand("respos");
    SetTimes(2050, 20950);
    RunCommand("respos");
    CHECK(player->client->ps.origin[0] == 7);
    G_FlyingRespawn_CaptureMovement(player, &movement);
    CHECK(G_FlyingRespawn_CaptureManual(state, 2050, &movement));
    CHECK(state->manualSave.start.serverTime == 2050);
    CHECK(state->manualSave.start.movement.origin[0] == 7);
    CHECK(state->manualSave.start.movement.commandTime == 2049);
    G_FlyingRespawn_FreeClient(state);
}

static void ManualReleaseTimer(void)
{
    for (int mode = 0; mode < 6; ++mode) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t initial;
        player->client->pers.practice = qtrue;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 600, mode >= 2 && mode != 3 ? 0 : 500, 0, 0, 500));
        G_FlyingRespawn_CaptureMovement(player, &initial);
        G_FlyingRespawn_RecordState(state, 600, &initial);
        SetTimes(1100, 20000);
        player->client->pers.stats.startTime = mode == 4 ? 0 : mode == 5 ? 20000 : 11500;
        player->client->pers.stats.startLevelTime = 900;
        player->client->pers.stats.topSpeed = 123;
        player->client->pers.stats.displacement = 456;
        player->client->pers.stats.displacementSamples = 9;
        player->client->pers.startLag = 37;
        state->respawnCount = 2;
        RunCommand("savepos");
        SetTimes(1750, 22000);
        player->client->pers.stats.startTime = 1000;
        player->client->pers.stats.topSpeed = 444;
        player->client->pers.stats.displacement = 0;
        player->client->pers.stats.displacementSamples = 0;
        player->client->pers.startLag = 0;
        state->respawnCount = 7;
        RunCommand("respos");
        /* The entry pose precedes the saved pose by the frozen approach duration. */
        if (mode != 2 && mode != 4 && mode != 5) {
            CHECK(Clock() - player->client->pers.stats.startTime == 8000);
            CHECK(player->client->pers.stats.topSpeed == 123);
        }
        if (mode == 1) {
            SetTimes(2050, 22300);
            RunCommand("respos");
        } else if (mode == 0) {
            SetTimes(2350, 22600);
            { usercmd_t cmd = {0}; cmd.serverTime = level.time; ClientThink(0, &cmd); }
        } else if (mode == 3) {
            player->client->pers.practice = qfalse;
            SetTimes(2050, 22300);
            { usercmd_t cmd = {0}; cmd.serverTime = level.time; ClientThink(0, &cmd); }
            CHECK(!state->replay.active);
            CHECK(Clock() - player->client->pers.stats.startTime == 8300);
            CHECK(player->client->pers.stats.topSpeed == 123);
            G_FlyingRespawn_FreeClient(state);
            continue;
        }
        CHECK(!state->replay.active);
        if (mode == 4) {
            CHECK(player->client->pers.stats.startTime == 0);
            CHECK(player->client->pers.stats.startLevelTime == 0);
            CHECK(player->client->ps.duelTime == 0);
            CHECK(!player->client->pers.stats.coopStarted);
            G_FlyingRespawn_FreeClient(state);
            continue;
        }
        CHECK(Clock() - player->client->pers.stats.startTime == (mode == 5 ? 0 : 8500));
        CHECK(level.time - player->client->pers.stats.startLevelTime == 200);
        CHECK(level.time - player->client->ps.duelTime == 200);
        CHECK(player->client->pers.stats.topSpeed == 123);
        CHECK(player->client->pers.stats.displacement == 456);
        CHECK(player->client->pers.stats.displacementSamples == 9);
        CHECK(player->client->pers.startLag == 37);
        CHECK(state->respawnCount == 2);
        CHECK(G_FlyingRespawn_GetSecondaryElapsed(state, level.time) == 500);
        SetTimes(level.time + 50, Clock() + 50);
        CHECK(Clock() - player->client->pers.stats.startTime == (mode == 5 ? 50 : 8550));
        CHECK(G_FlyingRespawn_GetSecondaryElapsed(state, level.time) == 550);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void SavePointDefinitionNoclip(void)
{
    gentity_t *player = Player();
    player->client->noclip = qtrue;
    player->client->ps.origin[0] = player->client->ps.origin[1] = player->client->ps.origin[2] = 1;
    RunCommand("addSavePoint");
    CHECK(player->client->pers.addSavePointPending);
    player->client->ps.origin[0] = player->client->ps.origin[1] = player->client->ps.origin[2] = 10;
    RunCommand("addSavePoint");
    CHECK(player->client->pers.addSavePointArmed);
    CHECK(!player->client->pers.flyingRespawn.manualSave.valid);
    RunCommand("addSavePoint");
    CHECK(player->client->pers.addSavePointPending);
    RunCommandWithArg("addSavePoint", "cancel");
    CHECK(!player->client->pers.addSavePointPending);
    RunCommand("savepos");
    RunCommand("respos");
    RunCommand("respawn");
    RunCommandWithArg("replaytest", "100");
    CHECK(!player->client->pers.flyingRespawn.manualSave.valid);
    CHECK(!player->client->pers.flyingRespawn.replay.active);
    G_FlyingRespawn_FreeClient(&player->client->pers.flyingRespawn);
}

static void LiveStep(int time)
{
    usercmd_t live = {0};
    level.time = live.serverTime = time;
    ClientThink(0, &live);
}

static void MomentumStep(int time, int forward, int buttons)
{
    usercmd_t live = {0};
    level.time = live.serverTime = time;
    live.forwardmove = forward;
    live.buttons = buttons;
    ClientThink(0, &live);
}

static void FlyingRespawnDebugTrace(void)
{
    float baselineX = 0, baselineVelocity = 0;
    int baselineCommand = 0, baselineTimer = 0, oldDebug = g_flyingRespawnDebug.integer;
    char baselineCP[2][2048] = {{0}};
    for (int enabled = 0; enabled < 2; ++enabled) {
        gentity_t *player = Player(), *checkpoint = &g_entities[MAX_CLIENTS];
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t movement;
        usercmd_t stale = {0};
        int saves = 0, begins = 0, gos = 0, source = 0, resumed = 0, early = 0, cps = 0;
        int lastFlushCount, cpOutput = 0;
        g_flyingRespawnDebug.integer = enabled;
        ++g_flyingRespawnDebug.modificationCount;
        player->client->pers.practice = qtrue;
        player->client->pers.showConsoleCP = qtrue;
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->ps.commandTime = 950;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
        SetTimes(1000, 19900);
        player->client->pers.stats.startTime = 11500;
        G_FlyingRespawn_CaptureMovement(player, &movement);
        G_FlyingRespawn_RecordState(state, 1000, &movement);
        SetTimes(1100, 20000);
        RunCommand("savepos");
        for (int i = 1; i <= 12; ++i) {
            SetTimes(1100 + 10 * i, 20000 + 10 * i);
            MomentumStep(level.time, 127, 0);
        }
        checkpoint->s.number = MAX_CLIENTS;
        CHECK(debugSentCount == 0); /* no diagnostic output before the brush */
        TimerCheckpoint(checkpoint, player, NULL);
        lastFlushCount = debugSentCount;
        SetTimes(2000, 22000);
        RunCommand("respos");
        CHECK(state->replay.active);
        SetTimes(2100, 22100);
        MomentumStep(2100, 127, 0);
        CHECK(!state->replay.active);
        stale.serverTime = player->client->ps.commandTime;
        ClientThink(0, &stale); /* an attempt rejected before Pmove */
        for (int i = 1; i <= 12; ++i) {
            SetTimes(2100 + 10 * i, 22100 + 10 * i);
            MomentumStep(level.time, 127, 0);
        }
        SetTimes(2300, 22300);
        CHECK(debugSentCount == lastFlushCount);
        TimerCheckpoint(checkpoint, player, NULL);
        for (int i = 0; i < commandsSent && i < 64; ++i) {
            if (strstr(sent[i].text, "Checkpoint:")) {
                CHECK(cpOutput < 2);
                if (cpOutput < 2) {
                    if (!enabled) Q_strncpyz(baselineCP[cpOutput], sent[i].text, sizeof(baselineCP[cpOutput]));
                    else CHECK(!strcmp(baselineCP[cpOutput], sent[i].text));
                }
                ++cpOutput;
            }
        }
        CHECK(cpOutput == 2);
        for (int i = 0; i < debugSentCount && i < 64; ++i) {
            const char *line = debugSent[i];
            if (strstr(line, "FRDBG SAVE ")) saves++;
            if (strstr(line, "FRDBG BEGIN ")) begins++;
            if (strstr(line, "FRDBG GO ")) gos++;
            if (strstr(line, "FRDBG CP ")) {
                cps++;
                CHECK(strstr(line, "raw=") && strstr(line, "endLag=") && strstr(line, "printed="));
            }
            if (strstr(line, "FRDBG ATTEMPT ")) {
                if (strstr(line, "phase=save")) source++;
                if (strstr(line, "phase=go")) resumed++;
                if (strstr(line, "result=early")) early++;
                CHECK(strstr(line, "rawCmd=") && strstr(line, "clampedCmd=") && strstr(line, "psCmd="));
            }
        }
        if (!enabled) {
            CHECK(debugSentCount == 0);
            baselineX = player->client->ps.origin[0];
            baselineVelocity = player->client->ps.velocity[0];
            baselineCommand = player->client->ps.commandTime;
            baselineTimer = player->client->pers.stats.startTime;
        } else {
            CHECK(saves == 1 && begins == 1 && gos == 1 && cps == 2);
            CHECK(source == 8 && resumed == 8 && early >= 1);
            CHECK(player->client->ps.origin[0] == baselineX);
            CHECK(player->client->ps.velocity[0] == baselineVelocity);
            CHECK(player->client->ps.commandTime == baselineCommand);
            CHECK(player->client->pers.stats.startTime == baselineTimer);
            /* Two real saves in one server tick must each arm a fresh budget. */
            {
                int first = debugSentCount, sameTickSaves = 0, attempts = 0;
                usercmd_t input = {0};
                SetTimes(2400, 22400);
                RunCommand("savepos");
                for (int i = 1; i <= 8; ++i) {
                    input.serverTime = 2400 + i; input.forwardmove = 127;
                    ClientThink(0, &input);
                }
                RunCommand("savepos");
                input.serverTime = 2409;
                ClientThink(0, &input);
                SetTimes(3501, 23501);
                TimerCheckpoint(checkpoint, player, NULL);
                for (int i = first; i < debugSentCount && i < 64; ++i) {
                    if (strstr(debugSent[i], "FRDBG SAVE ")) ++sameTickSaves;
                    if (strstr(debugSent[i], "FRDBG ATTEMPT ") && strstr(debugSent[i], "phase=save")) ++attempts;
                }
                CHECK(sameTickSaves == 2 && attempts == 9);
            }
            {
                int first = debugSentCount;
                G_FlyingRespawnDebugLog(0, qfalse, "FRDBG client-zero\n");
                level.clients[1].pers.connectTime = 17;
                G_FlyingRespawnDebugLog(1, qfalse, "FRDBG client-one\n");
                G_FlyingRespawnDebugLog(0, qtrue, NULL);
                CHECK(debugSentCount == first + 1 && strstr(debugSent[first], "client-zero"));
                G_FlyingRespawnDebugLog(1, qtrue, NULL);
                CHECK(debugSentCount == first + 2 && strstr(debugSent[first + 1], "client-one"));
            }
        }
        G_FlyingRespawn_FreeClient(state);
    }
    g_flyingRespawnDebug.integer = oldDebug;
}

static void ManualLiveClockQueue(void)
{
    for (int variant = 0; variant < 7; ++variant) {
        const int skip = variant == 1 || variant == 3 || variant == 4 || variant == 6;
        const int newer = variant >= 5;
        const int absent = variant == 3 || variant == 4;
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t initial;
        usercmd_t live = {0};
        float savedX;
        const int saveLevel = 6378139, saveWall = 6377222;
        const int startLevel = 6389820, startWall = 6388898;
        const int goLevel = skip ? 6389870 : variant == 2 ? 6389953 : 6389920;
        const int goWall = skip ? 6388968 : variant == 2 ? 6389031 : 6388998;
        const int expectedCmd = goWall + 874; /* saved command-to-wall phase */
        SetTimes(saveLevel - 100, saveWall - 100);
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->pers.stats.startTime = 6369276;
        player->client->pers.stats.startLevelTime = 6370186;
        player->client->ps.commandTime = 6378093;
        player->client->ps.velocity[0] = 250;
        CHECK(G_FlyingRespawn_OnTimerStart(state, level.time, 100, 0, 0, 100));
        G_FlyingRespawn_CaptureMovement(player, &initial);
        G_FlyingRespawn_RecordState(state, level.time, &initial);
        SetTimes(saveLevel, saveWall);
        live.serverTime = 6378096; live.forwardmove = 127;
        ClientThink(0, &live);
        if (variant == 4) player->client->pers.stats.startTime = 0;
        RunCommand("savepos");
        savedX = player->client->ps.origin[0];
        CHECK(player->client->pers.savePosTimerElapsed == (variant == 4 ? 0 : 7946));
        CHECK(state->manualSave.snapshot.movement.commandTime == 6378096);
        VectorClear(player->client->ps.velocity);
        RunCommand("practice");
        CHECK(player->client->pers.practice);
        SetTimes(startLevel, startWall);
        RunCommand("respos");
        CHECK(state->replay.active);
        SetTimes(goLevel, goWall);
        live.serverTime = expectedCmd + (newer ? 2 : -1);
        if (skip) {
            if (absent) state->replay.hasLiveCmd = qfalse;
            else ClientThink(0, &live); /* capture input before manual skip */
            RunCommand("respos");
        } else ClientThink(0, &live);
        CHECK(!state->replay.active);
        CHECK(player->client->ps.commandTime == expectedCmd + (newer && !skip ? 2 : 0));
        if (absent) CHECK(player->client->pers.cmd.serverTime == expectedCmd);
        if (variant == 4) CHECK(!player->client->pers.stats.startTime && !player->client->pers.stats.coopStarted);
        if (newer && !skip) CHECK(fabsf(player->client->ps.origin[0] - savedX - 0.5f) < 0.03f);
        else CHECK(fabsf(player->client->ps.origin[0] - savedX) < 0.01f);
        if (newer && skip) CHECK(player->client->pers.cmd.serverTime == expectedCmd + 2);
        live.serverTime = expectedCmd + (newer ? 5 : 3);
        ClientThink(0, &live);
        CHECK(player->client->ps.commandTime == expectedCmd + (newer ? 5 : 3));
        if (newer) CHECK(fabsf(player->client->ps.origin[0] - savedX - 1.25f) < 0.04f);
        else CHECK(player->client->ps.origin[0] > savedX + 0.5f);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void ManualTimerEntryAlignment(void)
{
    for (int skip = 0; skip < 2; ++skip) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        player->client->pers.practice = qtrue;
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->ps.commandTime = 950;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 250, 0, 0, 250));
        MomentumStep(1000, 0, 0);
        level.time = 1050;
        RunCommandWithArg("launch", "1500"); /* history boundary shortens configured 250ms */
        MomentumStep(1100, 0, 0);
        SetTimes(1200, 20000);
        player->client->pers.stats.startTime = 11500;
        player->client->pers.stats.startLevelTime = 300;
        RunCommand("savepos");
        CHECK(state->manualSave.start.serverTime == 1050);
        CHECK(state->manualSave.snapshot.serverTime == 1200);
        SetTimes(2000, 22000);
        RunCommand("respos");
        CHECK(state->replay.active && state->replay.releaseServerTime == 2150);
        CHECK(Clock() - player->client->pers.stats.startTime == 8350);
        CHECK(level.time - player->client->pers.stats.startLevelTime == 750);
        CHECK(level.time - player->client->ps.duelTime == 750);
        CHECK(G_FlyingRespawn_GetSecondaryElapsed(state, level.time) == 50);
        if (skip) {
            SetTimes(2050, 22050);
            RunCommand("respos");
        } else {
            SetTimes(2150, 22150);
            MomentumStep(2150, 0, 0);
        }
        CHECK(!state->replay.active);
        CHECK(Clock() - player->client->pers.stats.startTime == 8500);
        CHECK(level.time - player->client->ps.duelTime == 900);
        CHECK(G_FlyingRespawn_GetSecondaryElapsed(state, level.time) == 200);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void ManualSetupMomentum(void)
{
    for (int noclip = 0; noclip < 2; ++noclip) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        int oldAllowRaceTele = g_allowRaceTele.integer;
        float oldSpeed = g_speed.value;
        float expectedX, expectedVelocity;
        int firstLiveTime, replayFirstTime;
        player->client->pers.practice = qtrue;
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->ps.commandTime = 950;
        g_speed.value = 250;
        g_allowRaceTele.integer = 2;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 250, 0, 0, 250));
        MomentumStep(1000, 0, 0); /* earlier, stationary history exists */
        if (noclip) {
            level.time = 1025;
            RunCommand("noclip");
            CHECK(player->client->noclip);
            MomentumStep(1050, 127, BUTTON_ATTACK);
            MomentumStep(1100, 127, BUTTON_ATTACK);
            CHECK(player->client->ps.pm_type == PM_NOCLIP);
            CHECK(player->client->ps.velocity[0] > 500);
            RunCommand("noclip");
            CHECK(!player->client->noclip);
            firstLiveTime = 1150;
        } else {
            level.time = 1050;
            RunCommandWithArg("launch", "1500");
            CHECK(player->client->ps.velocity[0] == 1500);
            firstLiveTime = 1100;
        }
        MomentumStep(firstLiveTime, 0, 0);
        expectedX = player->client->ps.origin[0];
        expectedVelocity = player->client->ps.velocity[0];
        CHECK(expectedVelocity > 500);
        MomentumStep(1250, 0, 0);
        RunCommand("savepos");
        CHECK(state->manualSave.valid);
        CHECK(state->manualSave.start.serverTime <= firstLiveTime);
        if (noclip) {
            CHECK(state->manualSave.start.serverTime == firstLiveTime);
            CHECK(state->manualSave.start.movement.pmType != PM_NOCLIP);
            CHECK(state->manualSave.commandCount == 2);
            for (int i = 0; i < state->manualSave.commandCount; ++i)
                CHECK(!(state->manualSave.commands[i].cmd.buttons & BUTTON_ATTACK));
        }
        player->client->ps.velocity[0] = -900; /* later live setup must not supply replay momentum */
        level.time = 2000;
        RunCommand("respos");
        CHECK(state->replay.active);
        replayFirstTime = 2000 + firstLiveTime - state->manualSave.start.serverTime;
        MomentumStep(replayFirstTime, 0, 0);
        CHECK(state->replay.active);
        if (fabsf(player->client->ps.velocity[0] - expectedVelocity) > 0.25f ||
            fabsf(player->client->ps.origin[0] - expectedX) > 0.25f)
            fprintf(stderr, "momentum mode=%s: expected x=%.3f vx=%.3f, replay x=%.3f vx=%.3f, start=%d\n",
                noclip ? "noclip" : "launch", expectedX, expectedVelocity,
                player->client->ps.origin[0], player->client->ps.velocity[0], state->manualSave.start.serverTime);
        CHECK(fabsf(player->client->ps.velocity[0] - expectedVelocity) <= 0.25f);
        CHECK(fabsf(player->client->ps.origin[0] - expectedX) <= 0.25f);
        g_allowRaceTele.integer = oldAllowRaceTele;
        g_speed.value = oldSpeed;
        G_FlyingRespawn_FreeClient(state);
    }
}

/* Diagnostic for a possible boundary-based fix: setup and movement can share one tick. */
static void ManualSetupSameTickBoundary(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t setup;
    float afterFirst;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.commandTime = 950;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    MomentumStep(1000, 0, 0);
    level.time = 1050;
    RunCommandWithArg("launch", "1500");
    G_FlyingRespawn_CaptureMovement(player, &setup);
    G_FlyingRespawn_RecordHandoff(state, level.time, &setup);
    MomentumStep(1050, 37, 0);
    afterFirst = player->client->ps.origin[0];
    MomentumStep(1100, 0, 0);
    RunCommand("savepos");
    CHECK(state->manualSave.valid);
    CHECK(state->manualSave.start.serverTime == 1050);
    CHECK(fabsf(state->manualSave.start.movement.origin[0] - setup.origin[0]) <= 0.01f);
    CHECK(state->manualSave.commandCount == 2);
    if (state->manualSave.commandCount > 0)
        CHECK(state->manualSave.commands[0].cmd.forwardmove == 37);
    level.time = 2000;
    RunCommand("respos");
    MomentumStep(2000, 0, 0);
    CHECK(state->replay.commandCursor == 1);
    CHECK(fabsf(player->client->ps.origin[0] - afterFirst) <= 0.25f);
    G_FlyingRespawn_FreeClient(state);
}

static void NoclipImmediateBookmark(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    int oldAllow = g_allowRaceTele.integer;
    float oldSpeed = g_speed.value, savedX, savedVelocity;
    flyingRespawnSnapshot_t saved;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.commandTime = 700;
    g_allowRaceTele.integer = 2;
    g_speed.value = 250;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 750, 250, 0, 0, 250));
    MomentumStep(750, 0, 0);
    MomentumStep(1000, 0, 0);
    level.time = 1025;
    RunCommand("noclip");
    CHECK(player->client->noclip);
    MomentumStep(1050, 127, BUTTON_ATTACK);
    MomentumStep(1100, 127, BUTTON_ATTACK);
    RunCommand("noclip");
    CHECK(!player->client->noclip);
    savedX = player->client->ps.origin[0];
    savedVelocity = player->client->ps.velocity[0];
    CHECK(savedVelocity > 500);
    RunCommand("savepos");
    saved = state->manualSave.snapshot;
    CHECK(state->manualSave.valid);
    CHECK(state->manualSave.start.serverTime == 1100);
    CHECK(state->manualSave.commandCount == 0);
    player->client->ps.velocity[0] = -900;
    level.time = 2000;
    RunCommand("respos");
    CHECK(!state->replay.active);
    CHECK(fabsf(player->client->ps.origin[0] - savedX) <= 0.01f);
    CHECK(fabsf(player->client->ps.velocity[0] - savedVelocity) <= 0.01f);
    MomentumStep(2050, 0, 0);
    CHECK(player->client->ps.pm_type == PM_NORMAL);
    CHECK(player->client->ps.velocity[0] > 500);
    CHECK(!memcmp(&state->manualSave.snapshot, &saved, sizeof(saved)));
    g_allowRaceTele.integer = oldAllow;
    g_speed.value = oldSpeed;
    G_FlyingRespawn_FreeClient(state);
}

static void SetupSameTickExecution(void)
{
    for (int noclip = 0; noclip < 2; ++noclip) {
        for (int sync = 0; sync < 2; ++sync) {
            gentity_t *player = Player();
            flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
            usercmd_t first = {0}, latest = {0}, replay = {0};
            int oldSync = g_synchronousClients.integer, oldAllow = g_allowRaceTele.integer;
            float oldSpeed = g_speed.value;
            player->client->pers.practice = qtrue;
            player->client->ps.stats[STAT_HEALTH] = 100;
            player->client->ps.commandTime = 950;
            g_speed.value = 250;
            g_allowRaceTele.integer = 2;
            g_synchronousClients.integer = 0;
            CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
            MomentumStep(1000, 0, 0);
            g_synchronousClients.integer = sync;
            if (noclip) {
                level.time = 1025;
                RunCommand("noclip");
                MomentumStep(1050, 127, BUTTON_ATTACK);
                if (sync) G_RunClient(player);
                CHECK(player->client->noclip && player->client->ps.velocity[0] > 500);
                level.time = 1100;
                RunCommand("noclip");
                CHECK(!player->client->noclip);
            } else {
                level.time = 1100;
                RunCommandWithArg("launch", "1500");
            }
            first.serverTime = 1090; first.forwardmove = 19;
            latest.serverTime = 1100; latest.forwardmove = 25;
            if (!noclip || sync) ClientThink(0, &first);
            ClientThink(0, &latest);
            if (sync) G_RunClient(player);
            RunCommand("savepos");
            CHECK(state->manualSave.start.serverTime == 1100);
            CHECK(state->manualSave.start.movement.pmType != PM_NOCLIP);
            CHECK(state->manualSave.commandCount == ((!noclip && !sync) ? 2 : 1));
            level.time = 2000;
            RunCommand("respos");
            CHECK(state->replay.active);
            CHECK(G_FlyingRespawn_OverrideCmd(state, 2000, &replay));
            CHECK(replay.forwardmove == ((!noclip && !sync) ? 19 : 25));
            if (!noclip && !sync) {
                CHECK(G_FlyingRespawn_OverrideCmd(state, 2000, &replay));
                CHECK(replay.forwardmove == 25);
            }
            CHECK(!G_FlyingRespawn_OverrideCmd(state, 2000, &replay));
            g_synchronousClients.integer = oldSync;
            g_allowRaceTele.integer = oldAllow;
            g_speed.value = oldSpeed;
            G_FlyingRespawn_FreeClient(state);
        }
    }
}

static void SetupPreservesOwnersAndWindow(void)
{
    for (int noclip = 0; noclip < 2; ++noclip) {
        for (int pending = 0; pending < 2; ++pending) {
            gentity_t *player = Player();
            flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
            flyingRespawnMovement_t movement;
            flyingRespawnSnapshot_t saved;
            flyingRespawnCheckpointConfig_t config = {0};
            int oldAllow = g_allowRaceTele.integer;
            float oldSpeed = g_speed.value;
            player->client->pers.practice = qtrue;
            player->client->ps.stats[STAT_HEALTH] = 100;
            player->client->ps.commandTime = 950;
            g_allowRaceTele.integer = 2;
            g_speed.value = 250;
            CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 200, 200, 0, 200));
            MomentumStep(1000, 0, 0);
            RunCommand("savepos");
            saved = state->manualSave.snapshot;
            G_FlyingRespawn_CaptureMovement(player, &movement);
            config.hasRewind = qtrue;
            config.rewindMs = 0;
            CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1000, 20, &config, &movement));
            if (!pending) CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1200, &movement));
            CHECK(state->automaticCheckpoint.pending == pending);
            level.time = 1250;
            if (noclip) {
                RunCommand("noclip");
                CHECK(player->client->noclip);
                MomentumStep(1300, 127, BUTTON_ATTACK);
                RunCommand("noclip");
                CHECK(!player->client->noclip);
                MomentumStep(1350, 0, 0);
            } else {
                RunCommandWithArg("launch", "1500");
                MomentumStep(1300, 0, 0);
            }
            CHECK(!state->automaticCheckpoint.pending);
            CHECK(state->automaticCheckpoint.valid == !pending);
            CHECK(!memcmp(&state->manualSave.snapshot, &saved, sizeof(saved)));
            MomentumStep(1400, 0, 0);
            MomentumStep(1500, 0, 0);
            MomentumStep(1600, 0, 0);
            RunCommand("savepos");
            CHECK(state->manualSave.start.serverTime == 1400);
            g_allowRaceTele.integer = oldAllow;
            g_speed.value = oldSpeed;
            G_FlyingRespawn_FreeClient(state);
        }
    }
}

static void LaunchRejectedDuringReplay(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    usercmd_t input;
    int timer, flags;
    unsigned int generation;
    player->client->pers.practice = qtrue;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    level.time = 1050;
    generation = state->historyGeneration;
    RunCommand("launch"); /* invalid usage must leave rolling history alone */
    CHECK(state->historyGeneration == generation);
    level.time = 1100;
    RunCommand("savepos");
    level.time = 2000;
    RunCommand("respos");
    CHECK(state->replay.active);
    G_FlyingRespawn_CaptureMovement(player, &movement);
    input = player->client->pers.cmd;
    timer = player->client->pers.stats.startTime;
    flags = player->client->ps.eFlags;
    generation = state->historyGeneration;
    RunCommandWithArg("launch", "1500");
    CHECK(state->replay.active && state->historyGeneration == generation);
    CHECK(!memcmp(&input, &player->client->pers.cmd, sizeof(input)));
    CHECK(player->client->pers.stats.startTime == timer);
    CHECK(player->client->ps.eFlags == flags);
    CHECK(player->client->ps.velocity[0] == movement.velocity[0]);
    G_FlyingRespawn_FreeClient(state);

    /* First action after connect can be launch, before any movement configured history. */
    {
        int oldRewind = g_flyingRespawnRewindMs.integer;
        player = Player();
        state = &player->client->pers.flyingRespawn;
        player->client->pers.practice = qtrue;
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->ps.commandTime = 950;
        g_flyingRespawnRewindMs.integer = 100;
        CHECK(!state->historyCapacity);
        RunCommandWithArg("launch", "1500");
        CHECK(state->historyCapacity > 0);
        CHECK(state->historyContinuousSince == 1000);
        MomentumStep(1050, 0, 0);
        RunCommand("savepos");
        CHECK(state->manualSave.start.serverTime == 1000);
        CHECK(state->manualSave.start.movement.velocity[0] == 1500);
        CHECK(state->manualSave.commandCount == 1);
        g_flyingRespawnRewindMs.integer = oldRewind;
        G_FlyingRespawn_FreeClient(state);
    }
}

static void SetupTestReplayWaitsForEndpoint(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    int oldTestMode = g_flyingRespawnTestMode.integer;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.commandTime = 950;
    g_flyingRespawnTestMode.integer = 1;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 1, 100));
    MomentumStep(1000, 0, 0);
    level.time = 1050;
    RunCommandWithArg("launch", "1500");
    MomentumStep(1050, 0, 0);
    CHECK(!G_FlyingRespawn_StartTestReplay(player, 0));
    CHECK(!state->replay.active && !state->testReplayUsed && !state->runInvalid);
    MomentumStep(1100, 0, 0);
    CHECK(G_FlyingRespawn_StartTestReplay(player, 50));
    MomentumStep(1150, 0, 0);
    CHECK(!state->replay.active);
    CHECK(!state->mismatch);
    g_flyingRespawnTestMode.integer = oldTestMode;
    G_FlyingRespawn_FreeClient(state);
}

static void NoclipCancelsReplayWithoutGo(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    int oldAllow = g_allowRaceTele.integer;
    player->client->pers.practice = qtrue;
    g_allowRaceTele.integer = 2;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    SetTimes(1100, 20000);
    player->client->pers.stats.startTime = 11500;
    RunCommand("savepos");
    SetTimes(2000, 22000);
    RunCommand("respos");
    CHECK(state->replay.active);
    CHECK(Clock() - player->client->pers.stats.startTime == 8400);
    commandsSent = 0;
    RunCommand("noclip");
    CHECK(player->client->noclip);
    SetTimes(2050, 22050);
    MomentumStep(2050, 0, 0);
    CHECK(!state->replay.active && !(player->client->ps.eFlags & EF_FLYING_RESPAWN));
    CHECK(Clock() - player->client->pers.stats.startTime == 8450);
    for (int i = 0; i < commandsSent && i < 64; ++i)
        CHECK(!strstr(sent[i].text, "GO!"));
    g_allowRaceTele.integer = oldAllow;
    G_FlyingRespawn_FreeClient(state);
}

static void NoclipSpaceMomentum(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    int oldAllow = g_allowRaceTele.integer;
    float oldSpeed = g_speed.value, expectedX, expectedVelocity;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.commandTime = 950;
    g_allowRaceTele.integer = 2;
    g_speed.value = 250;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 250, 0, 0, 250));
    MomentumStep(1000, 0, 0);
    level.time = 1025;
    RunCommand("noclip");
    MomentumStep(1050, 127, BUTTON_ATTACK);
    MomentumStep(1100, 127, BUTTON_ATTACK);
    CHECK(player->client->ps.velocity[0] > 500);
    RunCommand("noclip");
    player->client->inSpaceIndex = 1;
    MomentumStep(1150, 0, 0);
    expectedX = player->client->ps.origin[0];
    expectedVelocity = player->client->ps.velocity[0];
    MomentumStep(1250, 0, 0);
    RunCommand("savepos");
    CHECK(state->manualSave.valid && state->manualSave.start.serverTime == 1150);
    level.time = 2000;
    RunCommand("respos");
    MomentumStep(2000, 0, 0);
    if (fabsf(player->client->ps.velocity[0] - expectedVelocity) > 0.25f)
        fprintf(stderr, "space boundary expected x=%.3f vx=%.3f, replay x=%.3f vx=%.3f\n",
            expectedX, expectedVelocity, player->client->ps.origin[0], player->client->ps.velocity[0]);
    CHECK(state->replay.active);
    CHECK(fabsf(player->client->ps.origin[0] - expectedX) <= 0.25f);
    CHECK(fabsf(player->client->ps.velocity[0] - expectedVelocity) <= 0.25f);
    g_allowRaceTele.integer = oldAllow;
    g_speed.value = oldSpeed;
    G_FlyingRespawn_FreeClient(state);
}

static void NoclipCancelThenExitBoundary(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    int oldAllow = g_allowRaceTele.integer;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.commandTime = 50;
    g_allowRaceTele.integer = 2;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 100, 1000, 0, 0, 1000));
    MomentumStep(100, 0, 0);
    level.time = 1100;
    RunCommand("savepos");
    CHECK(state->manualSave.valid && state->manualSave.start.serverTime == 100);
    player->client->ps.origin[0] = 999;
    MomentumStep(1200, 0, 0);
    MomentumStep(1500, 0, 0);
    level.time = 2000;
    RunCommand("respos");
    CHECK(state->replay.active && player->client->ps.origin[0] != 999);
    level.time = 2050;
    RunCommand("noclip");
    MomentumStep(2050, 0, 0); /* cancels before noclip movement */
    CHECK(!state->replay.active && player->client->noclip);
    RunCommand("noclip"); /* leave noclip before another movement */
    CHECK(!player->client->noclip);
    MomentumStep(2100, 0, 0);
    MomentumStep(2200, 0, 0);
    RunCommand("savepos");
    CHECK(state->manualSave.start.serverTime >= 2100);
    CHECK(state->manualSave.start.movement.origin[0] != 999);
    level.time = 3000;
    RunCommand("respos");
    CHECK(player->client->ps.origin[0] != 999);
    g_allowRaceTele.integer = oldAllow;
    G_FlyingRespawn_FreeClient(state);
}

static void SavepointFeedback(void)
{
    for (int timed = 0; timed < 2; ++timed) {
        for (int choice = 0; choice < 4; ++choice) {
            gentity_t *player = Player();
            int confirmation = 0, stats = 0;
            SetTimes(1000, 20000);
            player->client->pers.stats.startTime = timed ? 11500 : 0;
            player->client->pers.showCenterCP = choice == 1;
            player->client->pers.showConsoleCP = choice == 2;
            player->client->pers.showChatCP = choice == 3;
            player->client->pers.addSavePointArmed = qtrue;
            VectorSet(player->client->pers.addSavePointMins, -10, -10, -10000);
            VectorSet(player->client->pers.addSavePointMaxs, 10, 10, 10000);
            LiveStep(1050);
            CHECK(player->client->pers.flyingRespawn.manualSave.valid);
            for (int i = 0; i < commandsSent && i < 64; ++i) {
                if (strstr(sent[i].text, "Savepoint saved.")) confirmation++;
                if (strstr(sent[i].text, "avg")) {
                    stats++;
                    CHECK(!strstr(sent[i].text, "\\n"));
                    if (choice == 1 || choice == 2) CHECK(strchr(sent[i].text, '\n') != NULL);
                }
            }
            CHECK(confirmation == 1);
            CHECK(stats == (timed && choice != 0));
            G_FlyingRespawn_FreeClient(&player->client->pers.flyingRespawn);
        }
    }
}

static void SavedRankedRunCompletesInPractice(void)
{
    for (int skip = 0; skip < 2; ++skip) {
        gentity_t *player = Player(), *finish = &g_entities[MAX_CLIENTS];
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t movement;
        sqlite3 *db = NULL;
        int invalidOutput = 0;
        InitGameAccountStuff();
        CHECK(sqlite3_open("./data.db", &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db, "DELETE FROM LocalRun; DELETE FROM LocalRunInvalid", NULL, NULL, NULL) == SQLITE_OK);
        Q_strncpyz(player->client->pers.userName, "runner", sizeof(player->client->pers.userName));
        SetTimes(1000, 20000);
        player->client->ps.stats[STAT_HEALTH] = 100;
        player->client->pers.stats.startTime = 11500;
        player->client->pers.stats.startLevelTime = 900;
        player->client->pers.stats.coopStarted = qtrue;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
        G_FlyingRespawn_CaptureMovement(player, &movement);
        G_FlyingRespawn_RecordState(state, 1000, &movement);
        player->client->pers.addSavePointArmed = qtrue;
        VectorSet(player->client->pers.addSavePointMins, -10, -10, -10000);
        VectorSet(player->client->pers.addSavePointMaxs, 10, 10, 10000);
        player->client->ps.origin[0] = 100;
        LiveStep(1050);
        player->client->ps.origin[0] = 0;
        LiveStep(1100);
        CHECK(state->manualSave.valid && state->manualSave.snapshot.serverTime == 1100);
        CHECK(player->client->pers.stats.coopStarted);
        player->client->ps.origin[0] = 100;
        LiveStep(1150);
        VectorClear(player->client->ps.velocity);
        RunCommand("practice");
        CHECK(player->client->pers.practice && !player->client->pers.stats.coopStarted);
        SetTimes(1200, 22000);
        RunCommand("respos");
        CHECK(state->replay.active);
        if (skip) { SetTimes(1250, 22050); RunCommand("respos"); }
        else { SetTimes(1300, 22100); LiveStep(1300); }
        CHECK(!state->replay.active);
        CHECK(player->client->pers.stats.coopStarted);
        finish->inuse = qtrue;
        finish->wait = 1;
        commandsSent = 0;
        TimerStop(finish, player, NULL);
        for (int i = 0; i < commandsSent && i < 64; ++i)
            if (strstr(sent[i].text, "dffinish_japro_invalid")) invalidOutput++;
        CHECK(invalidOutput == 1);
        CHECK(!player->client->pers.stats.startTime);
        CHECK(!player->client->pers.stats.startLevelTime);
        CHECK(!player->client->ps.duelTime);
        CHECK(Scalar(db, "SELECT count(*) FROM LocalRun") == 0);
        CHECK(Scalar(db, "SELECT count(*) FROM LocalRunInvalid") == 0);
        sqlite3_close(db);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void LaunchSaveClock(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    SetTimes(66660000, 20000);
    player->client->pmoveMsec = 16;
    VectorClear(player->client->ps.velocity);
    RunCommand("practice");
    CHECK(player->client->pers.practice && !player->client->pers.stats.startTime);
    player->client->ps.commandTime = level.time;
    player->client->pers.addSavePointArmed = qtrue;
    VectorSet(player->client->pers.addSavePointMins, -10, -10, -10000);
    VectorSet(player->client->pers.addSavePointMaxs, 10, 10, 10000);
    RunCommandWithArg("launch", "0");
    CHECK(player->client->pers.stats.startTime == 20016);
    CHECK(player->client->pers.stats.startLevelTime == 66660000);
    SetTimes(66660050, 20066);
    LiveStep(66660050);
    CHECK(state->manualSave.valid && state->manualSave.snapshot.serverTime == 66660050);
    CHECK(player->client->pers.savePosTimerElapsed == 50);
    CHECK(player->client->pers.savePosLevelTimeElapsed == 50);
    VectorClear(player->client->ps.velocity);
    RunCommand("practice");
    RunCommand("practice");
    CHECK(player->client->pers.practice && !player->client->pers.stats.startTime);
    SetTimes(66661000, 21016);
    RunCommand("respos");
    CHECK(Clock() - player->client->pers.stats.startTime == 50);
    CHECK(level.time - player->client->ps.duelTime == 50);
    CHECK(level.time - player->client->pers.stats.startLevelTime == 50);
    G_FlyingRespawn_FreeClient(state);

    /* A later launch must not replace a pre-existing bookmark's timer state. */
    for (int savedRunning = 0; savedRunning < 2; ++savedRunning) {
        flyingRespawnMovement_t movement;
        player = Player();
        state = &player->client->pers.flyingRespawn;
        player->client->pers.practice = qtrue;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
        G_FlyingRespawn_CaptureMovement(player, &movement);
        G_FlyingRespawn_RecordState(state, 1000, &movement);
        SetTimes(1100, 20000);
        player->client->pers.stats.startTime = savedRunning ? 11500 : 0;
        player->client->pers.stats.startLevelTime = 900;
        player->client->pers.stats.coopStarted = qfalse;
        RunCommand("savepos");
        SetTimes(1750, 22000);
        RunCommandWithArg("launch", "0");
        CHECK(player->client->pers.stats.coopStarted);
        RunCommand("respos");
        CHECK(state->replay.active);
        CHECK(!player->client->pers.stats.coopStarted);
        if (savedRunning) {
            CHECK(Clock() - player->client->pers.stats.startTime == 8400);
            CHECK(level.time - player->client->ps.duelTime == 100);
        } else {
            CHECK(!player->client->pers.stats.startTime && !player->client->ps.duelTime);
        }
        SetTimes(1800, 22050);
        RunCommand("respos");
        CHECK(!state->replay.active && !player->client->pers.stats.coopStarted);
        if (savedRunning) CHECK(Clock() - player->client->pers.stats.startTime == 8500);
        else CHECK(!player->client->pers.stats.startTime && !player->client->ps.duelTime);
        G_FlyingRespawn_FreeClient(state);
    }
}

static int SaveMessages(void)
{
    int count = 0;
    for (int i = 0; i < commandsSent && i < 64; ++i)
        if (strstr(sent[i].text, "Savepoint:")) ++count;
    return count;
}

static void SavePointReplayPreservesBookmark(void)
{
    for (int mode = 0; mode < 3; ++mode) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t initial;
        flyingRespawnSnapshot_t original, originalStart, originalHandoff;
        flyingRespawnCmd_t *commands;
        int commandCount, messageCount, savedElapsed, savedLevel, savedSecondary, savedCount;
        int savedLag, savedSamples, savedCheckpoints;
        float savedDisplacement, savedTop;
        qboolean savedRunning;
        player->client->pers.practice = qtrue;
        player->client->pers.showConsoleCP = qtrue;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, mode ? 100 : 0, 0, 0, 100));
        player->client->pers.addSavePointArmed = qtrue;
        VectorSet(player->client->pers.addSavePointMins, -10, -10, -10000);
        VectorSet(player->client->pers.addSavePointMaxs, 10, 10, 10000);
        G_FlyingRespawn_CaptureMovement(player, &initial);
        G_FlyingRespawn_RecordState(state, 1000, &initial);
        player->client->ps.origin[0] = 100;
        LiveStep(1050);
        player->client->ps.origin[0] = 0;
        LiveStep(1100);
        CHECK(player->client->pers.addSavePointInside);
        CHECK(state->manualSave.valid && state->manualSave.snapshot.serverTime == 1100);
        original = state->manualSave.snapshot;
        originalStart = state->manualSave.start;
        originalHandoff = state->manualSave.handoff;
        commandCount = state->manualSave.commandCount;
        CHECK(!mode || commandCount > 0);
        commands = commandCount ? malloc(commandCount * sizeof(*commands)) : NULL;
        if (commandCount) memcpy(commands, state->manualSave.commands, commandCount * sizeof(*commands));
        savedElapsed = player->client->pers.savePosTimerElapsed;
        savedLevel = player->client->pers.savePosLevelTimeElapsed;
        savedSecondary = player->client->pers.savePosNoRespawnElapsed;
        savedCount = player->client->pers.savePosRespawnCount;
        savedLag = player->client->pers.savePosStartLag;
        savedSamples = player->client->pers.savePosDisplacementSamples;
        savedCheckpoints = player->client->pers.savePosCheckpoints;
        savedDisplacement = player->client->pers.savePosDisplacement;
        savedTop = player->client->pers.savePosTopSpeed;
        savedRunning = player->client->pers.savePosTimerRunning;
        messageCount = SaveMessages();
        player->client->ps.origin[0] = 100;
        LiveStep(1150);
        CHECK(!player->client->pers.addSavePointInside);
        level.time = 1200;
        RunCommand("respos");
        if (mode == 1) LiveStep(1300);
        if (mode == 2) { level.time = 1250; RunCommand("respos"); }
        CHECK(!state->replay.active);
        CHECK(player->client->pers.addSavePointInside);
        LiveStep(1350);
        LiveStep(1400);
        CHECK(player->client->pers.addSavePointInside);
        CHECK(!memcmp(&state->manualSave.snapshot, &original, sizeof(original)));
        CHECK(!memcmp(&state->manualSave.start, &originalStart, sizeof(originalStart)));
        CHECK(!memcmp(&state->manualSave.handoff, &originalHandoff, sizeof(originalHandoff)));
        CHECK(state->manualSave.commandCount == commandCount);
        if (commandCount == state->manualSave.commandCount && commandCount)
            CHECK(!memcmp(state->manualSave.commands, commands, commandCount * sizeof(*commands)));
        CHECK(player->client->pers.savePosTimerElapsed == savedElapsed);
        CHECK(player->client->pers.savePosLevelTimeElapsed == savedLevel);
        CHECK(player->client->pers.savePosNoRespawnElapsed == savedSecondary);
        CHECK(player->client->pers.savePosRespawnCount == savedCount);
        CHECK(player->client->pers.savePosStartLag == savedLag);
        CHECK(player->client->pers.savePosDisplacementSamples == savedSamples);
        CHECK(player->client->pers.savePosCheckpoints == savedCheckpoints);
        CHECK(player->client->pers.savePosDisplacement == savedDisplacement);
        CHECK(player->client->pers.savePosTopSpeed == savedTop);
        CHECK(player->client->pers.savePosTimerRunning == savedRunning);
        CHECK(SaveMessages() == messageCount);
        player->client->ps.origin[0] = 100;
        LiveStep(1450);
        player->client->ps.origin[0] = 0;
        LiveStep(1500);
        CHECK(state->manualSave.snapshot.serverTime == 1500);
        free(commands);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void SavePointOccupancyReplayMatrix(void)
{
    /* cancellation inside/outside, checkpoint release, test release, no armed box */
    for (int mode = 0; mode < 5; ++mode) {
        gentity_t *player = Player();
        flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
        flyingRespawnMovement_t movement;
        flyingRespawnCheckpointConfig_t config = {0};
        player->client->pers.practice = qtrue;
        CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 100, 1, 100));
        player->client->pers.addSavePointArmed = mode != 4;
        VectorSet(player->client->pers.addSavePointMins, -10, -10, -10000);
        VectorSet(player->client->pers.addSavePointMaxs, 10, 10, 10000);
        G_FlyingRespawn_CaptureMovement(player, &movement);
        G_FlyingRespawn_RecordState(state, 1000, &movement);
        level.time = 1050;
        RunCommand("savepos");
        G_FlyingRespawn_RecordState(state, 1100, &movement);
        CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1100, 20, &config, &movement));
        CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1200, &movement));
        player->client->pers.addSavePointInside = qfalse;
        level.time = 2000;
        if (mode == 3) {
            g_flyingRespawnTestMode.integer = 1;
            CHECK(G_FlyingRespawn_StartTestReplay(player, 100));
        } else {
            g_enableFlyingRespawn.integer = 1;
            CHECK(G_FlyingRespawn_StartCheckpointReplay(player));
        }
        if (mode < 2) {
            player->client->ps.origin[0] = mode ? 100 : 0;
            player->client->sess.raceMode = qfalse;
            LiveStep(2050);
            player->client->sess.raceMode = qtrue;
            CHECK(!state->replay.active);
            CHECK(player->client->pers.addSavePointInside == !mode);
        } else {
            LiveStep(state->replay.releaseServerTime);
            CHECK(!state->replay.active);
            CHECK(player->client->pers.addSavePointInside == (mode != 4));
        }
        LiveStep(level.time + 50);
        CHECK(state->manualSave.snapshot.serverTime == 1050);
        G_FlyingRespawn_FreeClient(state);
    }
}

static void FirstGoCheckpointBrush(void)
{
    gentity_t *player = Player(), *brush = &g_entities[MAX_CLIENTS];
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    usercmd_t live = {0};
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    level.time = 1100;
    RunCommand("savepos");
    player->client->ps.origin[0] = 999;
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1500, &movement);
    level.time = 2000;
    RunCommand("respos");
    CHECK(state->replay.active && state->replay.releaseServerTime == 2100);
    brush->inuse = qtrue;
    brush->s.number = MAX_CLIENTS;
    brush->r.contents = CONTENTS_TRIGGER;
    brush->touch = TimerCheckpoint;
    brush->objective = 1;
    fixtureBrush = 1;
    level.time = live.serverTime = 2100;
    live.forwardmove = 37;
    ClientThink(0, &live);
    fixtureBrush = 0;
    CHECK(!state->replay.active);
    CHECK(state->automaticCheckpoint.valid);
    CHECK(state->automaticCheckpoint.start.serverTime == 2100);
    CHECK(state->automaticCheckpoint.start.movement.origin[0] != 999);
    CHECK(state->automaticCheckpoint.commandCount == 1);
    if (state->automaticCheckpoint.commandCount == 1) {
        CHECK(state->automaticCheckpoint.commands[0].serverTime == 2100);
        CHECK(state->automaticCheckpoint.commands[0].cmd.forwardmove == 37);
    }
    G_FlyingRespawn_FreeClient(state);
}

static void FixtureTargetTouch(gentity_t *brush, gentity_t *player, trace_t *trace)
{
    gentity_t *target = &g_entities[MAX_CLIENTS + 1];
    (void)trace;
    target->use(target, brush, player);
}

static void ReplayMapperTransition(void)
{
    gentity_t *player = Player(), *brush = &g_entities[MAX_CLIENTS], *target = brush + 1;
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    flyingRespawnMovement_t movement;
    int timer;
    player->client->pers.practice = qtrue;
    player->client->ps.stats[STAT_HEALTH] = 100;
    player->client->ps.powerups[PW_NEUTRALFLAG] = 99999;
    player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 1;
    CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 0, 0, 100));
    G_FlyingRespawn_CaptureMovement(player, &movement);
    G_FlyingRespawn_RecordState(state, 1000, &movement);
    target->spawnflags = 8;
    target->count = 3;
    SP_target_restrict(target);
    brush->inuse = qtrue;
    brush->r.contents = CONTENTS_TRIGGER;
    brush->touch = FixtureTargetTouch;
    timer = player->client->pers.stats.startTime;
    fixtureBrush = 1;
    LiveStep(1050);
    fixtureBrush = 0;
    CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 3);
    CHECK(player->client->savedJumpLevel == 1);
    CHECK(player->client->pers.stats.startTime == timer);
    level.time = 1100;
    RunCommand("savepos");
    CHECK(state->manualSave.commandCount == 1);
    player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 2;
    player->client->savedJumpLevel = 0;
    level.time = 2000;
    RunCommand("respos");
    CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 1);
    CHECK(player->client->savedJumpLevel == 0);
    fixtureBrush = 1;
    LiveStep(2050);
    CHECK(state->replay.active);
    CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 3);
    CHECK(player->client->savedJumpLevel == 1);
    LiveStep(2100);
    fixtureBrush = 0;
    CHECK(!state->replay.active);
    CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 3);
    CHECK(player->client->savedJumpLevel == 3); /* neutral-flag update on first live step */
    G_FlyingRespawn_FreeClient(state);
}

static void ReplaySettingCommandResets(void)
{
    for (int command = 0; command < 2; ++command) {
        for (int replay = 0; replay < 3; ++replay) {
            gentity_t *player = Player();
            flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
            flyingRespawnMovement_t movement;
            flyingRespawnCheckpointConfig_t config = {0};
            player->client->ps.groundEntityNum = ENTITYNUM_WORLD;
            player->client->sess.movementStyle = MV_JKA;
            player->client->tempSpectate = -1;
            CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 0, 500, 0, 0));
            G_FlyingRespawn_CaptureMovement(player, &movement);
            CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1100, 20, &config, &movement));
            CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1600, &movement));
            level.time = 2000;
            if (replay) { g_enableFlyingRespawn.integer = 1; CHECK(G_FlyingRespawn_StartCheckpointReplay(player)); }
            if (replay == 2) {
                gentity_t *checkpoint = &g_entities[20];
                checkpoint->s.number = 20;
                TimerCheckpoint(checkpoint, player, NULL);
                player->client->ps.groundEntityNum = ENTITYNUM_NONE;
                LiveStep(state->replay.releaseServerTime);
                CHECK(!state->replay.active);
                player->client->ps.groundEntityNum = ENTITYNUM_WORLD;
                VectorClear(player->client->ps.velocity);
            }
            RunCommandWithArg(command ? "move" : "jump", command ? "qw" : "2");
            CHECK(player->client->pers.stats.startTime == 0);
            CHECK(command ? player->client->sess.movementStyle == MV_QW : player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 2);
            if (replay == 1) {
                usercmd_t live = {0}; live.serverTime = ++level.time; ClientThink(0, &live);
                CHECK(!state->replay.active);
            }
            RunCommand("respawn");
            CHECK(!state->replay.active);
            CHECK(G_FlyingRespawn_OnTimerStart(state, level.time, 0, 500, 0, 0));
            CHECK(!state->automaticCheckpoint.valid);
            G_FlyingRespawn_FreeClient(state);
        }
    }
}

static void ReplayJumpContextModes(void)
{
    for (int mode = 0; mode < 5; ++mode) {
        for (int saved = 0; saved < 2; ++saved) {
            gentity_t *player = Player();
            flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
            flyingRespawnMovement_t movement;
            flyingRespawnCheckpointConfig_t config = {0};
            player->client->pers.practice = qtrue;
            player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 1;
            player->client->savedJumpLevel = saved ? 2 : 0;
            if (saved) player->client->ps.powerups[PW_NEUTRALFLAG] = 99999;
            CHECK(G_FlyingRespawn_OnTimerStart(state, 1000, 100, 100, 1, 100));
            G_FlyingRespawn_CaptureMovement(player, &movement);
            G_FlyingRespawn_RecordState(state, 1000, &movement);
            level.time = 1100;
            RunCommand("savepos");
            G_FlyingRespawn_RecordState(state, 1100, &movement);
            CHECK(G_FlyingRespawn_OnCheckpointTouch(state, 1100, 20, &config, &movement));
            CHECK(G_FlyingRespawn_CaptureAutomaticHandoff(state, 1200, &movement));
            player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 3;
            player->client->savedJumpLevel = saved ? 0 : 2;
            level.time = 2000;
            if (mode < 2) RunCommand("respos");
            else if (mode == 4) { g_flyingRespawnTestMode.integer = 1; CHECK(G_FlyingRespawn_StartTestReplay(player, 100)); }
            else { g_enableFlyingRespawn.integer = 1; CHECK(G_FlyingRespawn_StartCheckpointReplay(player)); }
            CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 1);
            CHECK(player->client->savedJumpLevel == (saved ? 2 : 0));
            if (mode == 1) { level.time = 2050; RunCommand("respos"); }
            if (mode == 3) { level.time = 2050; CHECK(G_FlyingRespawn_StartCheckpointReplay(player)); }
            if (mode == 2) {
                gentity_t *checkpoint = &g_entities[20];
                checkpoint->s.number = 20;
                level.time = 2100;
                TimerCheckpoint(checkpoint, player, NULL);
            }
            if (state->replay.active) LiveStep(state->replay.releaseServerTime);
            else LiveStep(level.time + 50);
            CHECK(!state->replay.active);
            CHECK(!state->mismatch);
            CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 1);
            CHECK(player->client->savedJumpLevel == (saved ? 2 : 0));
            G_FlyingRespawn_FreeClient(state);
        }
    }
}

static void ReplayJumpContext(void)
{
    gentity_t *player = Player();
    flyingRespawnState_t *state = &player->client->pers.flyingRespawn;
    player->client->pers.practice = qtrue;
    player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 1;
    player->client->savedJumpLevel = 2;
    RunCommand("savepos");
    player->client->ps.fd.forcePowerLevel[FP_LEVITATION] = 3;
    player->client->savedJumpLevel = 0;
    level.time = 2000;
    RunCommand("respos");
    CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 1);
    CHECK(player->client->savedJumpLevel == 2);
    {
        gentity_t *target = &g_entities[MAX_CLIENTS];
        int timer = player->client->pers.stats.startTime;
        player->client->savedJumpLevel = 0;
        target->spawnflags = 8;
        target->count = 3;
        SP_target_restrict(target);
        target->use(target, player, player);
        CHECK(player->client->ps.fd.forcePowerLevel[FP_LEVITATION] == 3);
        CHECK(player->client->savedJumpLevel == 1);
        CHECK(player->client->pers.stats.startTime == timer);
    }
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
    CHECK(!strcmp(sent[0].text, "print \"^2Completed (respawns: 2) in ^39.500       ^2 max:^3123       ^2 avg:^345        ^2 style:^3jka       ^2 by ^7Runner ^7\n\" dffinish_japro_valid"));
    CHECK(sent[1].client == 0);
    CHECK(strstr(sent[1].text, "no-respawn") != NULL);
    CHECK(strstr(sent[1].text, "3.500") != NULL);
    CHECK(strstr(sent[1].text, "2") != NULL);
    CHECK(player->client->pers.stats.startTime == 777);
    CHECK(player->client->pers.stats.startLevelTime == 888);
    CHECK(player->client->ps.duelTime == 999);
	commandsSent = 0;
	state->respawnCount = 0;
	PrintRaceTime(NULL, playerName, NULL, style, 123, 45, time, 0, 0, qfalse, 0, qfalse, qtrue, 0, 0, 0, -1, -1);
	CHECK(commandsSent == 1);
	CHECK(!strcmp(sent[0].text, "print \"^2Completed in ^39.500       ^2 max:^3123       ^2 avg:^345        ^2 style:^3jka       ^2 by ^7Runner ^7\n\" dffinish_japro_valid"));
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

typedef struct { const char *name; void (*run)(void); int finish; } IntegrationCase;
#define CASE(name, finish) { #name, name, finish }
static const IntegrationCase cases[] = {
    CASE(RespawnSchemaHistorical, 0), CASE(RespawnSchemaExisting, 0), CASE(RespawnSchemaError, 0),
    CASE(SynchronousReceiptsCoalesce, 0), CASE(ManualSkipDeadlines, 0), CASE(ReplayRejectedBeginAndConfiguration, 0), CASE(ManualReleaseTimer, 0), CASE(ManualReleaseHistory, 0),
    CASE(SavePointDefinitionNoclip, 0), CASE(FlyingRespawnDebugTrace, 0), CASE(ManualLiveClockQueue, 0), CASE(ManualTimerEntryAlignment, 0), CASE(ManualSetupMomentum, 0), CASE(ManualSetupSameTickBoundary, 0), CASE(NoclipImmediateBookmark, 0), CASE(SetupSameTickExecution, 0), CASE(SetupPreservesOwnersAndWindow, 0), CASE(LaunchRejectedDuringReplay, 0), CASE(SetupTestReplayWaitsForEndpoint, 0), CASE(NoclipCancelsReplayWithoutGo, 0), CASE(NoclipSpaceMomentum, 0), CASE(NoclipCancelThenExitBoundary, 0), CASE(SavepointFeedback, 0), CASE(SavedRankedRunCompletesInPractice, 0), CASE(LaunchSaveClock, 0), CASE(SavePointReplayPreservesBookmark, 0),
    CASE(SavePointOccupancyReplayMatrix, 0), CASE(FirstGoCheckpointBrush, 0),
    CASE(ReplayJumpContext, 0), CASE(ReplayJumpContextModes, 0), CASE(ReplayMapperTransition, 0), CASE(ReplaySettingCommandResets, 0), CASE(StaticForceWait, 0), CASE(FlyingFrameInput, 0),
    CASE(ReplayTimerBrushes, 0), CASE(StaticTribesServerFrames, 0),
    CASE(StaticServerFrameTimeout, 0), CASE(MovementResources, 0),
    CASE(StaticJetpackResources, 0), CASE(CoopTimerStartReset, 0),
    CASE(ProgressCountdown, 0), CASE(ManualReplayEligibilityAndRespawnCount, 0),
    CASE(FinishPublicPrivate, 1), CASE(FinishPersistence, 1), CASE(FinishHelp, 1),
    CASE(FinishCoopEligibility, 1), CASE(FinishCoopActiveReplay, 1)
};
int main(int argc, char **argv)
{
    int ran = 0;
    int named = argc == 3 && !strcmp(argv[1], "--case");
    int finish = argc == 2 && !strcmp(argv[1], "finish");
    if (argc != 1 && !named && !finish) return 2;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (named && strcmp(argv[2], cases[i].name)) continue;
        if (finish && !cases[i].finish) continue;
        cases[i].run();
        ran++;
    }
    if (!ran) { fprintf(stderr, "Unknown case: %s\n", argv[2]); return 2; }
    if (failures) fprintf(stderr, "%d integration assertions failed\n", failures);
    else printf("%d flying-respawn integration cases passed\n", ran);
    return failures ? 1 : 0;
}
