#include "g_local.h"
#include "g_secretcourses.h"

#include <string.h>

int G_AdminAllowed(gentity_t *ent, unsigned int adminCmd, qboolean cheatAllowed, qboolean raceAllowed, char *cmdName);

void SC_EnsureConfigTemplate(void)
{
	static const char template_text[] =
		"// Secret Courses configuration. Times are UTC: YYYY-MM-DDTHH:MM:SSZ.\n"
		"// Validate with /sc_validatecfg, then apply with /sc_applycfg.\n"
		"secretcourses_version 1\n"
		"// sc_season <season-id> <number> <starts-at> <expires-at>\n"
		"// sc_course <course-id> \"<course-name>\" cup <season-id> <starts-at> <expires-at>\n"
		"// sc_course <course-id> \"<course-name>\" permanent <starts-at>\n";
	fileHandle_t file = 0;
	int length = trap->FS_Open("secretcourses.cfg", &file, FS_READ);

	if (file) {
		trap->FS_Close(file);
		if (length > 0) {
			return;
		}
	}

	if (trap->FS_Open("secretcourses.cfg", &file, FS_WRITE) >= 0 && file) {
		trap->FS_Write(template_text, strlen(template_text), file);
		trap->FS_Close(file);
	}
}

static qboolean SC_ReadTextFile(char *text, size_t text_size, sc_validation_result_t *error)
{
	fileHandle_t file;
	int length = trap->FS_Open("secretcourses.cfg", &file, FS_READ);

	if (!file || length <= 0) {
		if (file) {
			trap->FS_Close(file);
		}
		SC_EnsureConfigTemplate();
		length = trap->FS_Open("secretcourses.cfg", &file, FS_READ);
	}
	if (!file || length <= 0 || (size_t)length >= text_size) {
		if (file) trap->FS_Close(file);
		if (error) Q_strncpyz(error->message, "could not read secretcourses.cfg after creating its template", sizeof(error->message));
		return qfalse;
	}
	if (trap->FS_Read(text, length, file) != length) {
		trap->FS_Close(file);
		if (error) Q_strncpyz(error->message, "could not read secretcourses.cfg", sizeof(error->message));
		return qfalse;
	}
	trap->FS_Close(file);
	text[length] = '\0';
	return qtrue;
}

qboolean SC_ReadAndValidateConfig(sc_config_t *config, char revision[65], sc_validation_result_t *error)
{
	static char text[65536];
	unsigned int hash = 2166136261u;
	const unsigned char *cursor;

	if (error) memset(error, 0, sizeof(*error));
	if (!SC_ReadTextFile(text, sizeof(text), error) || !SC_ParseConfigText(text, config, error)) {
		return qfalse;
	}
	for (cursor = (const unsigned char *)text; *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 16777619u;
	}
	Com_sprintf(revision, 65, "%08x", hash);
	return qtrue;
}

void SC_ValidateConfig_f(gentity_t *ent)
{
	sc_config_t config;
	sc_validation_result_t error;
	char revision[65];
	char applied[65] = { 0 };
	sc_projection_plan_t plan;

	if (!G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_GRANTADMIN, qfalse, qfalse, "sc_validatecfg")) {
		return;
	}
	if (!SC_ReadAndValidateConfig(&config, revision, &error)) {
		trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg invalid%s%s%s%s.\n\"", error.line ? " at line " : "", error.line ? va("%d", error.line) : "", error.message[0] ? ": " : "", error.message));
		return;
	}
	if (!SC_DBPlanProjection(&config, &plan)) {
		trap->SendServerCommand(ent - g_entities, "print \"Could not read Secret Course projection.\n\"");
		return;
	}
	SC_DBGetAppliedRevision(applied);
	trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg valid (%d courses, revision %s): %d additions, %d retirements, %d correctable identity changes, %d blocked identity changes, %d schedule conflicts; applied revision %s.\n\"", config.num_courses, revision, plan.additions, plan.retirements, plan.corrections, plan.changes, plan.schedule_conflicts, applied[0] ? applied : "none"));
}

void SC_ApplyConfig_f(gentity_t *ent)
{
	sc_config_t config;
	sc_validation_result_t error;
	sc_projection_plan_t projection;
	sc_apply_plan_t plan;
	char revision[65];
	char token[32];
	time_t now;
	qboolean confirming = qfalse;

	if (!G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_GRANTADMIN, qfalse, qfalse, "sc_applycfg")) {
		return;
	}
	if (!SC_ReadAndValidateConfig(&config, revision, &error)) {
		trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg invalid%s%s%s%s.\n\"", error.line ? " at line " : "", error.line ? va("%d", error.line) : "", error.message[0] ? ": " : "", error.message));
		return;
	}
	if (!SC_DBPlanProjection(&config, &projection)) {
		trap->SendServerCommand(ent - g_entities, "print \"Could not read Secret Course projection.\n\"");
		return;
	}
	time(&now);
	for (int course_index = 0; course_index < config.num_courses; course_index++) {
		if (!SC_CourseCanBeApplied(&config.courses[course_index], now)) {
			trap->SendServerCommand(ent - g_entities, va("print \"Secret Course %s is already expired and cannot be applied.\n\"", config.courses[course_index].id));
			return;
		}
	}
	if (trap->Argc() == 3) {
		trap->Argv(1, token, sizeof(token));
		confirming = !Q_stricmp(token, "confirm");
		trap->Argv(2, token, sizeof(token));
	}
	if (projection.changes) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Course IDs cannot change course name or type. Create a new ID instead.\n\"");
		return;
	}
	if (projection.schedule_conflicts) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Course schedules for the same course name may not overlap.\n\"");
		return;
	}
	if (projection.retirements || projection.corrections) {
		if (!confirming || !SC_CanConfirmApply(token, revision, now)) {
			SC_RememberApplyPlan(&plan, revision, 1, now);
			trap->SendServerCommand(ent - g_entities, va("print \"Destructive Secret Course plan: %d retirements, %d identity corrections. Confirm within %d seconds: /sc_applycfg confirm %s\n\"", projection.retirements, projection.corrections, SC_APPLY_PLAN_TTL, plan.token));
			return;
		}
	}
	if (!SC_DBApplyProjection(&config, revision, confirming)) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Courses projection was not applied. Revalidate and retry.\n\"");
		return;
	}
	trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg revision %s applied.\n\"", revision));
	SC_LoadSecretCourses();
	SC_ReconcileSchedule(now);
	SC_WriteStaticExports();
}

void SC_NotifyConfigRevisionAfterFullAdminLogin(gentity_t *ent)
{
	sc_config_t config;
	sc_validation_result_t error;
	char revision[65];
	char applied[65] = { 0 };

	if (!SC_ReadAndValidateConfig(&config, revision, &error) || !SC_DBGetAppliedRevision(applied) ||
		!SC_ShouldNotifyAfterFullAdminLogin(qtrue, Q_stricmp(applied, revision) != 0)) {
		return;
	}
	trap->SendServerCommand(ent - g_entities, "print \"Secret Courses cfg differs from the applied projection; run /sc_validatecfg then /sc_applycfg.\n\"");
	trap->Print("Secret Courses cfg revision mismatch for full admin %s.\n", ent->client->pers.userName);
}

static time_t sc_next_transition_time;
static time_t sc_archive_retry_time;
static qboolean sc_cache_loaded;

time_t SC_GetNextTransitionTime(void)
{
	return sc_next_transition_time;
}

void SC_ReconcileSchedule(time_t now)
{
	sc_course_t courses[SC_MAX_COURSES];
	sc_config_t config;
	int count;
	int index;
	qboolean refresh_cache = sc_cache_loaded ? qfalse : qtrue;
	qboolean refresh_exports = qfalse;

	if (!SC_DBGetLifecycleRows(courses, SC_MAX_COURSES, &count)) return;
	memset(&config, 0, sizeof(config));
	for (index = 0; index < count; index++) {
		sc_course_t *course = &courses[index];
		sc_course_state_t state = SC_ResolveCourseState(course, now);
		config.courses[config.num_courses++] = *course;
		if (state == SC_COURSE_STATE_ACTIVE && !course->activated_at) {
			if (SC_DBActivateCourse(course, now)) {
				refresh_cache = qtrue;
				refresh_exports = qtrue;
			}
		} else if (state == SC_COURSE_STATE_EXPIRED && course->type == SC_COURSE_TYPE_CUP) {
			qboolean claimed = SC_DBClaimExpiredCourse(course->id, now);
			if (claimed) {
				SC_DBLogExpiredCourseById(course->id);
				refresh_cache = qtrue;
				refresh_exports = qtrue;
			}
			if ((claimed || !course->archive_completed_at) && (!sc_archive_retry_time || now >= sc_archive_retry_time)) {
				if (SC_ArchiveExpiredCourseDemos(course)) {
					SC_DBMarkArchiveComplete(course->id, now);
					sc_archive_retry_time = 0;
					refresh_exports = qtrue;
				} else {
					sc_archive_retry_time = now + 60;
				}
			}
		}
	}
	if (refresh_cache) {
		SC_LoadSecretCourses();
		sc_cache_loaded = qtrue;
	}
	if (refresh_exports) SC_WriteStaticExports();
	sc_next_transition_time = SC_NextConfiguredTransition(&config, now);
	if (sc_archive_retry_time && (!sc_next_transition_time || sc_archive_retry_time < sc_next_transition_time))
		sc_next_transition_time = sc_archive_retry_time;
}

static void SC_WriteStaticFile(const char *path, const char *text)
{
	fileHandle_t file;
	if (trap->FS_Open(path, &file, FS_WRITE) >= 0 && file) {
		trap->FS_Write(text, strlen(text), file);
		trap->FS_Close(file);
	}
}

void SC_WriteStaticExports(void)
{
	sc_config_t config;
	sc_validation_result_t error;
	char revision[65];
	char status[16384];
	int used;
	int index;
	time_t now;

	if (!SC_ReadAndValidateConfig(&config, revision, &error)) return;
	time(&now);
	used = Com_sprintf(status, sizeof(status), "{\"schema_version\":1,\"revision\":\"%s\",\"courses\":[", revision);
	for (index = 0; index < config.num_courses; index++) {
		const sc_course_t *course = &config.courses[index];
		sc_course_state_t state = SC_ResolveCourseState(course, now);
		char leaderboard[2048];
		char path[MAX_QPATH];
		const char *kind = course->type == SC_COURSE_TYPE_CUP ? "cup" : "permanent";
		if (!SC_DBRenderLeaderboardJson(leaderboard, sizeof(leaderboard), course, state, revision)) continue;
		if (course->type == SC_COURSE_TYPE_CUP)
			Com_sprintf(path, sizeof(path), "secretcourses/cup/%s/%s/leaderboard.json", course->season_id, course->id);
		else
			Com_sprintf(path, sizeof(path), "secretcourses/permanent/%s/leaderboard.json", course->id);
		SC_WriteStaticFile(path, leaderboard);
		used += Com_sprintf(status + used, sizeof(status) - used, "%s{\"id\":\"%s\",\"course_name\":\"%s\",\"type\":\"%s\",\"lifecycle\":\"%s\",\"demos_visibility\":\"%s\",\"leaderboard_path\":\"/%s\"}", index ? "," : "", course->id, course->course_name, kind, state == SC_COURSE_STATE_ACTIVE ? "active" : (state == SC_COURSE_STATE_EXPIRED ? "expired" : "scheduled"), state == SC_COURSE_STATE_EXPIRED && course->type == SC_COURSE_TYPE_CUP ? "public" : "private", path);
		if (used >= (int)sizeof(status) - 256) break;
	}
	Q_strcat(status, sizeof(status), "]}");
	SC_WriteStaticFile("secretcourses/secretcourses-status.json", status);
}
