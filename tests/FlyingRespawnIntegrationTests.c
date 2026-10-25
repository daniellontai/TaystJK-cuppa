/* Links the real game module objects; only engine imports are replaced. */
#include "g_local.h"

#include <stdio.h>
#include <string.h>

void TimerStart(gentity_t *, gentity_t *, trace_t *);
void TimerStop(gentity_t *, gentity_t *, trace_t *);
void WP_ForcePowersUpdate(gentity_t *, usercmd_t *);
void G_RunFrame(int);
extern gclient_t g_clients[MAX_CLIENTS];

static gameImport_t imports;
static int failures, routeCalls, commandsSent;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); failures++; \
} } while (0)

static int Clock(void) { return 5000; }
static void ServerCommand(int client, const char *text) { (void)client; (void)text; commandsSent++; }
static void Userinfo(int client, char *buffer, int size) { (void)client; if (size) buffer[0] = 0; }
static void Serverinfo(char *buffer, int size) { if (size) buffer[0] = 0; }
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

int main(void)
{
    StaticForceWait();
    FlyingFrameInput();
    ReplayTimerBrushes();
    StaticTribesServerFrames();
    StaticServerFrameTimeout();
    if (failures) fprintf(stderr, "%d integration assertions failed\n", failures);
    else puts("5 flying-respawn integration cases passed");
    return failures ? 1 : 0;
}
