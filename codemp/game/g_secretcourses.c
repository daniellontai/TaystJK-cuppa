#include "g_local.h"
#include "g_secretcourses.h"

#include <string.h>

static char sc_last_config_text[SC_CFG_TEXT_MAX + 1];
static sc_restore_plan_t sc_last_restore_plan;
static unsigned int sc_restore_backup_serial;

int G_AdminAllowed(gentity_t *ent, unsigned int adminCmd, qboolean cheatAllowed, qboolean raceAllowed, char *cmdName);

void SC_EnsureConfigTemplate(void)
{
	static const char template_text[] =
		"// Secret Courses configuration. Times are UTC: YYYY-MM-DDTHH:MM:SSZ.\n"
		"// Validate with /sc_validatecfg, then apply with /sc_applycfg.\n"
		"secretcourses_version 2\n"
		"// sc_season <season-id> \"<name>\" <starts-at> <expires-at>\n"
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
	if (memchr(text, '\0', length)) {
		if (error) Q_strncpyz(error->message, "secretcourses.cfg contains an embedded NUL byte", sizeof(error->message));
		return qfalse;
	}
	text[length] = '\0';
	return qtrue;
}

static qboolean SC_ReadExistingTextFile(char *text, size_t text_size, int *out_length)
{
	fileHandle_t file;
	int length = trap->FS_Open("secretcourses.cfg", &file, FS_READ);
	if (!file || length < 0 || (size_t)length >= text_size) {
		if (file) trap->FS_Close(file);
		return qfalse;
	}
	if (length && trap->FS_Read(text, length, file) != length) {
		trap->FS_Close(file);
		return qfalse;
	}
	trap->FS_Close(file);
	text[length] = '\0';
	*out_length = length;
	return qtrue;
}

static qboolean SC_WriteRawTextFile(const char *path, const char *text, int length)
{
	fileHandle_t file;
	if (!path || !text || length < 0 || trap->FS_Open(path, &file, FS_WRITE) < 0 || !file) return qfalse;
	if (length) trap->FS_Write(text, length, file);
	trap->FS_Close(file);
	return qtrue;
}

qboolean SC_ReadAndValidateConfig(sc_config_t *config, char revision[65], sc_validation_result_t *error)
{
	char *text = sc_last_config_text;
	unsigned int hash = 2166136261u;
	const unsigned char *cursor;

	if (error) memset(error, 0, sizeof(*error));
	if (!SC_ReadTextFile(text, SC_CFG_TEXT_MAX + 1, error) || !SC_ParseConfigText(text, config, error)) {
		return qfalse;
	}
	for (cursor = (const unsigned char *)text; *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 16777619u;
	}
	Com_sprintf(revision, 65, "%08x", hash);
	return qtrue;
}

const char *SC_GetLastConfigText(void)
{
	return sc_last_config_text;
}

static const char *SC_CourseTypeName(sc_course_type_t type)
{
	return type == SC_COURSE_TYPE_CUP ? "cup" : "permanent";
}

typedef struct {
	gentity_t *ent;
	char text[900];
} sc_projection_report_t;

static void SC_FlushProjectionReport(sc_projection_report_t *report)
{
	if (!report || !report->ent || !report->text[0]) return;
	trap->SendServerCommand(report->ent - g_entities, va("print \"%s\"", report->text));
	report->text[0] = '\0';
}

static void SC_AppendProjectionReportLine(sc_projection_report_t *report, const char *line)
{
	size_t used;
	size_t line_length;

	if (!report || !line) return;
	used = strlen(report->text);
	line_length = strlen(line);
	if (!SC_ReportLineFits(used, line_length, sizeof(report->text))) SC_FlushProjectionReport(report);
	if (!SC_ReportLineFits(strlen(report->text), line_length, sizeof(report->text))) return;
	Q_strcat(report->text, sizeof(report->text), line);
	Q_strcat(report->text, sizeof(report->text), "\n");
}

static const char *SC_SeasonDisplayName(const sc_config_t *config, const char *id)
{
	int index;
	if (!config || !id || !id[0]) return "";
	for (index = 0; index < config->num_seasons; index++)
		if (!Q_stricmp(config->seasons[index].id, id)) return config->seasons[index].name;
	return id;
}

static void SC_PrintProjectionDetail(sc_projection_report_t *report, const sc_config_t *config, const char *prefix, const sc_projection_course_detail_t *detail)
{
	char id[SC_COURSE_ID_MAX + 1];
	char season[SC_SEASON_NAME_MAX * 4 + 1];
	char course_name[SC_COURSE_NAME_MAX * 4 + 1];
	char line[384];

	if (!report || !prefix || !detail) return;
	Q_strncpyz(id, detail->course.id, sizeof(id));
	Q_strstrip(id, "\n\r\"\\", NULL);
	if (!SC_EscapeConsoleText(course_name, sizeof(course_name), detail->course.course_name)) return;
	if (!SC_EscapeConsoleText(season, sizeof(season), SC_SeasonDisplayName(config, detail->course.season_id))) return;
	Com_sprintf(line, sizeof(line), "  %s %s (%s) - %s%s%s%s%s%s.", prefix, id, course_name, SC_CourseTypeName(detail->course.type), season[0] ? ", season " : "", season[0] ? season : "", detail->fields[0] ? ", fields: " : "", detail->fields[0] ? detail->fields : "", detail->reason[0] ? ", reason: " : "", detail->reason[0] ? detail->reason : "");
	SC_AppendProjectionReportLine(report, line);
}

static void SC_PrintProjectionCategory(sc_projection_report_t *report, const sc_config_t *config, const char *name, const char *prefix, int count, const sc_projection_course_detail_t *details)
{
	int index;
	char line[96];

	Com_sprintf(line, sizeof(line), "%s (%d):", name, count);
	SC_AppendProjectionReportLine(report, line);
	if (!count) {
		SC_AppendProjectionReportLine(report, "  none.");
		return;
	}
	for (index = 0; index < count && index < SC_MAX_COURSES; index++) SC_PrintProjectionDetail(report, config, prefix, &details[index]);
}

static void SC_PrintProjectionPlan(gentity_t *ent, const sc_config_t *config, const sc_projection_plan_t *plan, const char *revision, const char *applied_revision)
{
	int index;
	char line[256];
	sc_projection_report_t report;

	if (!ent || !config || !plan) return;
	memset(&report, 0, sizeof(report));
	report.ent = ent;
	Com_sprintf(line, sizeof(line), "Secret Courses cfg ^2valid^7 (proposed %s; applied %s).", revision ? revision : "unknown", applied_revision && applied_revision[0] ? applied_revision : "none");
	SC_AppendProjectionReportLine(&report, line);
	Com_sprintf(line, sizeof(line), "seasons defined (%d):", config->num_seasons);
	SC_AppendProjectionReportLine(&report, line);
	for (index = 0; index < config->num_seasons; index++) {
		const sc_season_t *season = &config->seasons[index];
		char season_id[SC_SEASON_ID_MAX + 1];
		char season_name[SC_SEASON_NAME_MAX * 4 + 1];
		char starts_at[32];
		char expires_at[32];
		Q_strncpyz(season_id, season->id, sizeof(season_id));
		Q_strstrip(season_id, "\n\r\"\\", NULL);
		if (!SC_EscapeConsoleText(season_name, sizeof(season_name), season->name)) continue;
		if (!SC_FormatLocalTime(starts_at, sizeof(starts_at), season->starts_at)) Q_strncpyz(starts_at, "unknown", sizeof(starts_at));
		if (!SC_FormatLocalTime(expires_at, sizeof(expires_at), season->expires_at)) Q_strncpyz(expires_at, "unknown", sizeof(expires_at));
		Com_sprintf(line, sizeof(line), "  * %s - %s, start %s, expiry %s.", season_id, season_name, starts_at, expires_at);
		SC_AppendProjectionReportLine(&report, line);
	}
	SC_PrintProjectionCategory(&report, config, "additions", "+", plan->num_addition_details, plan->addition_details);
	SC_PrintProjectionCategory(&report, config, "retirements", "-", plan->num_retirement_details, plan->retirement_details);
	SC_PrintProjectionCategory(&report, config, "permitted corrections", "~", plan->num_correction_details, plan->correction_details);
	SC_PrintProjectionCategory(&report, config, "permitted updates", "~", plan->num_update_details, plan->update_details);
	SC_PrintProjectionCategory(&report, config, "reactivations", "~", plan->num_reactivation_details, plan->reactivation_details);
	SC_PrintProjectionCategory(&report, config, "blocked changes", "!", plan->num_blocked_details, plan->blocked_details);
	Com_sprintf(line, sizeof(line), "schedule conflicts (%d):", plan->schedule_conflicts);
	SC_AppendProjectionReportLine(&report, line);
	if (!plan->schedule_conflicts) {
		SC_AppendProjectionReportLine(&report, "  none.");
	} else {
		for (index = 0; index < plan->num_schedule_conflict_details; index++) {
			const sc_projection_schedule_conflict_t *conflict = &plan->schedule_conflict_details[index];
			char course_name[SC_COURSE_NAME_MAX * 4 + 1];
			if (!SC_EscapeConsoleText(course_name, sizeof(course_name), conflict->course_name)) continue;
			Com_sprintf(line, sizeof(line), "  ! %s (%s) and %s (%s) overlap.", conflict->first_id, course_name, conflict->second_id, course_name);
			SC_AppendProjectionReportLine(&report, line);
		}
		if (plan->schedule_conflicts > plan->num_schedule_conflict_details) {
			Com_sprintf(line, sizeof(line), "  ... %d additional conflicts not listed.", plan->schedule_conflicts - plan->num_schedule_conflict_details);
			SC_AppendProjectionReportLine(&report, line);
		}
	}
	Com_sprintf(line, sizeof(line), "unchanged applied courses: %d.", plan->unchanged);
	SC_AppendProjectionReportLine(&report, line);
	SC_FlushProjectionReport(&report);
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
		trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg ^1invalid^7%s%s%s%s.\n\"", error.line ? " at line " : "", error.line ? va("%d", error.line) : "", error.message[0] ? ": " : "", error.message));
		return;
	}
	if (!SC_DBPlanProjection(&config, &plan)) {
		trap->SendServerCommand(ent - g_entities, "print \"Could not read Secret Course projection.\n\"");
		return;
	}
	SC_DBGetAppliedRevision(applied);
	SC_PrintProjectionPlan(ent, &config, &plan, revision, applied);
}

void SC_ApplyConfig_f(gentity_t *ent)
{
	sc_config_t config;
	sc_validation_result_t error;
	sc_projection_plan_t projection;
	sc_apply_plan_t plan;
	sc_course_t applied_courses[SC_MAX_COURSES];
	char revision[65];
	char applied_revision[65] = { 0 };
	char token[32];
	time_t now;
	int applied_count;
	qboolean confirming = qfalse;

	if (!G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_GRANTADMIN, qfalse, qfalse, "sc_applycfg")) {
		return;
	}
	if (!SC_ReadAndValidateConfig(&config, revision, &error)) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Courses cfg was not applied. Run /sc_validatecfg for details.\n\"");
		return;
	}
	if (!SC_DBPlanProjection(&config, &projection)) {
		trap->SendServerCommand(ent - g_entities, "print \"Could not read Secret Course projection.\n\"");
		return;
	}
	SC_DBGetAppliedRevision(applied_revision);
	time(&now);
	if (projection.changes) {
		trap->SendServerCommand(ent - g_entities, "print \"Finalized Secret Course IDs cannot change name, type, season, start, or expiry. Create a new ID instead.\n\"");
		return;
	}
	if (projection.schedule_conflicts) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Course schedules for the same course name may not overlap.\n\"");
		return;
	}
	if (!SC_DBGetAppliedCourses(applied_courses, SC_MAX_COURSES, &applied_count)) {
		trap->SendServerCommand(ent - g_entities, "print \"Could not read Secret Course projection.\n\"");
		return;
	}
	for (int course_index = 0; course_index < config.num_courses; course_index++) {
		int applied_index;
		qboolean already_finalized = qfalse;
		for (applied_index = 0; applied_index < applied_count; applied_index++) {
			if (!strcmp(applied_courses[applied_index].id, config.courses[course_index].id) && applied_courses[applied_index].finalized_at) {
				already_finalized = qtrue;
				break;
			}
		}
		if (!SC_CourseCanBeAppliedOrRetainFinalized(&config.courses[course_index], already_finalized, now)) {
			trap->SendServerCommand(ent - g_entities, va("print \"Secret Course %s is already expired and cannot be applied.\n\"", config.courses[course_index].id));
			return;
		}
	}
	if (trap->Argc() == 3) {
		trap->Argv(1, token, sizeof(token));
		confirming = !Q_stricmp(token, "confirm");
		trap->Argv(2, token, sizeof(token));
	}
	if (projection.retirements || projection.corrections) {
		if (!confirming || !SC_CanConfirmApply(token, revision, now)) {
			SC_RememberApplyPlan(&plan, revision, 1, now);
			trap->SendServerCommand(ent - g_entities, va("print \"Destructive Secret Course plan: %d retirements, %d identity corrections. Confirm within %d seconds: /sc_applycfg confirm %s\n\"", projection.retirements, projection.corrections, SC_APPLY_PLAN_TTL, plan.token));
			return;
		}
	}
	if (!SC_DBApplyProjection(&config, revision, SC_GetLastConfigText(), confirming)) {
		trap->SendServerCommand(ent - g_entities, "print \"Secret Courses projection was not applied. Revalidate and retry.\n\"");
		return;
	}
	trap->SendServerCommand(ent - g_entities, va("print \"Secret Courses cfg revision %s applied (%d courses).\n\"", revision, config.num_courses));
	SC_LoadSecretCourses();
	SC_ReconcileSchedule(now);
	SC_WriteStaticExports();
}

void SC_RestoreConfig_f(gentity_t *ent)
{
	char revision[65];
	char token[32];
	char backup_path[128];
	char backup_timestamp[32];
	char current_text[SC_CFG_TEXT_MAX + 1];
	sc_cfg_snapshot_t snapshot;
	sc_cfg_snapshot_result_t snapshot_result;
	time_t now;
	int owner_client_num;
	int current_length;

	if (!G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_GRANTADMIN, qfalse, qfalse, "sc_restorecfg")) return;
	owner_client_num = (int)(ent - g_entities);
	if (!SC_DBGetAppliedRevision(revision)) {
		trap->SendServerCommand(owner_client_num, "print \"Secret Courses restore unavailable: could not read applied revision.\n\"");
		return;
	}
	snapshot_result = SC_DBLoadCfgSnapshot(revision, &snapshot);
	if (snapshot_result == SC_CFG_SNAPSHOT_MISSING) {
		if (trap->Argc() != 1) sc_last_restore_plan.valid = 0;
		trap->SendServerCommand(owner_client_num, va("print \"Secret Courses restore unavailable: applied revision %s has no saved cfg snapshot.\n\"", revision));
		return;
	}
	if (snapshot_result != SC_CFG_SNAPSHOT_OK) {
		if (trap->Argc() != 1) sc_last_restore_plan.valid = 0;
		trap->SendServerCommand(owner_client_num, snapshot_result == SC_CFG_SNAPSHOT_DB_ERROR ? "print \"Secret Courses restore unavailable: could not read saved cfg snapshot.\n\"" : "print \"Secret Courses restore unavailable: saved cfg snapshot failed integrity checks.\n\"");
		return;
	}
	time(&now);
	if (trap->Argc() == 1) {
		SC_RememberRestorePlan(&sc_last_restore_plan, revision, owner_client_num, now);
		trap->SendServerCommand(owner_client_num, va("print \"Secret Courses restore will replace the editable cfg. Confirm: /sc_restorecfg confirm %s\n\"", sc_last_restore_plan.token));
		return;
	}
	if (trap->Argc() != 3) {
		sc_last_restore_plan.valid = 0;
		trap->SendServerCommand(owner_client_num, "print \"Usage: /sc_restorecfg or /sc_restorecfg confirm <token>\n\"");
		return;
	}
	trap->Argv(1, token, sizeof(token));
	if (Q_stricmp(token, "confirm")) {
		sc_last_restore_plan.valid = 0;
		trap->SendServerCommand(owner_client_num, "print \"Usage: /sc_restorecfg confirm <token>\n\"");
		return;
	}
	trap->Argv(2, token, sizeof(token));
	if (!SC_CanConfirmRestore(&sc_last_restore_plan, token, revision, owner_client_num, now)) {
		sc_last_restore_plan.valid = 0;
		trap->SendServerCommand(owner_client_num, "print \"Secret Courses restore confirmation is invalid or expired.\n\"");
		return;
	}
	sc_last_restore_plan.valid = 0;
	snapshot_result = SC_DBLoadCfgSnapshot(revision, &snapshot);
	if (snapshot_result != SC_CFG_SNAPSHOT_OK || !SC_ReadExistingTextFile(current_text, sizeof(current_text), &current_length)) {
		trap->SendServerCommand(owner_client_num, "print \"Secret Courses restore failed before changing the editable cfg.\n\"");
		return;
	}
	if (!SC_FormatIsoUtcFilenameTime(backup_timestamp, sizeof(backup_timestamp), now)) {
		trap->SendServerCommand(owner_client_num, "print \"Secret Courses restore failed: could not format backup timestamp.\n\"");
		return;
	}
	Com_sprintf(backup_path, sizeof(backup_path), "secretcourses.cfg.restore-%s-%s-%u.bak", revision, backup_timestamp, ++sc_restore_backup_serial);
	if (!SC_WriteRawTextFile(backup_path, current_text, current_length)) {
		trap->SendServerCommand(owner_client_num, "print \"Secret Courses restore failed: could not create cfg backup.\n\"");
		return;
	}
	if (!SC_WriteRawTextFile("secretcourses.cfg", snapshot.raw_text, snapshot.raw_length)) {
		trap->SendServerCommand(owner_client_num, va("print \"Secret Courses restore interrupted; recover from %s or retry restore.\n\"", backup_path));
		return;
	}
	trap->SendServerCommand(owner_client_num, va("print \"Secret Courses cfg restored from revision %s. Backup: %s\n\"", revision, backup_path));
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
	qboolean archive_retry_eligible = SC_ShouldAttemptArchiveRetry(sc_archive_retry_time, now) ? qtrue : qfalse;
	qboolean archive_failed = qfalse;

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
			if (!course->archive_completed_at && archive_retry_eligible) {
				if (SC_ArchiveExpiredCourseDemos(course) && SC_DBMarkArchiveComplete(course->id, now)) {
					refresh_exports = qtrue;
				} else {
					archive_failed = qtrue;
				}
			}
		}
	}
	if (archive_retry_eligible) {
		sc_archive_retry_time = archive_failed ? now + 60 : 0;
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
	sc_course_t courses[SC_MAX_COURSES];
	char revision[65];
	char escaped_revision[136];
	char status[16384];
	int count;
	int used;
	int index;
	time_t now;
	qboolean first = qtrue;
	qboolean status_full = qfalse;

	if (!SC_DBGetAppliedCourses(courses, SC_MAX_COURSES, &count) || !SC_DBGetAppliedRevision(revision) ||
		!SC_JsonWriteEscapedString(escaped_revision, sizeof(escaped_revision), revision)) return;
	time(&now);
	used = Com_sprintf(status, sizeof(status), "{\"schema_version\":1,\"revision\":%s,\"courses\":[", escaped_revision);
	if (used < 0 || (size_t)used >= sizeof(status)) return;
	for (index = 0; index < count; index++) {
		const sc_course_t *course = &courses[index];
		sc_course_state_t state = course->finalized_at ? SC_COURSE_STATE_EXPIRED : SC_ResolveCourseState(course, now);
		char leaderboard[2048];
		char path[256];
		char status_row[1024];
		char status_path[257];
		const char *kind = course->type == SC_COURSE_TYPE_CUP ? "cup" : "permanent";
		const char *lifecycle = state == SC_COURSE_STATE_ACTIVE ? "active" : (state == SC_COURSE_STATE_EXPIRED ? "expired" : "scheduled");
		const char *visibility = course->archive_completed_at ? "public" : "private";
		int written;

		if (!SC_ValidateExportPathIds(course)) continue;
		if (course->type == SC_COURSE_TYPE_CUP)
			written = Com_sprintf(path, sizeof(path), "secretcourses/cup/%s/%s/leaderboard.json", course->season_id, course->id);
		else
			written = Com_sprintf(path, sizeof(path), "secretcourses/permanent/%s/leaderboard.json", course->id);
		if (written < 0 || (size_t)written >= sizeof(path)) continue;
		if (!SC_DBRenderLeaderboardJson(leaderboard, sizeof(leaderboard), course, state, revision)) continue;
		SC_WriteStaticFile(path, leaderboard);
		if (status_full) continue;
		written = Com_sprintf(status_path, sizeof(status_path), "/%s", path);
		if (written < 0 || (size_t)written >= sizeof(status_path) ||
			!SC_AppendStatusCourseJson(status_row, sizeof(status_row), course->id, course->course_name, kind, lifecycle, visibility, status_path)) continue;
		if (!SC_StatusJsonHasRoom((size_t)used + (first ? 0 : 1), strlen(status_row), sizeof(status))) {
			status_full = qtrue;
			continue;
		}
		if (!first) status[used++] = ',';
		Q_strncpyz(status + used, status_row, sizeof(status) - used);
		used += strlen(status_row);
		first = qfalse;
	}
	Q_strncpyz(status + used, "]}", sizeof(status) - used);
	SC_WriteStaticFile("secretcourses/secretcourses-status.json", status);
}
