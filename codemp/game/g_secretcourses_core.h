#ifndef G_SECRETCOURSES_CORE_H
#define G_SECRETCOURSES_CORE_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_COURSE_ID_MAX 63
#define SC_COURSE_NAME_MAX 39
#define SC_SEASON_ID_MAX 31
#define SC_SEASON_NAME_MAX 63
#define SC_CFG_TEXT_MAX 65535
#define SC_MAX_SEASONS 16
#define SC_MAX_COURSES 64
#define SC_APPLY_PLAN_TTL 60

typedef enum {
	SC_COURSE_TYPE_CUP,
	SC_COURSE_TYPE_PERMANENT
} sc_course_type_t;

typedef enum {
	SC_COURSE_STATE_SCHEDULED,
	SC_COURSE_STATE_ACTIVE,
	SC_COURSE_STATE_EXPIRED
} sc_course_state_t;

typedef struct {
	char id[SC_COURSE_ID_MAX + 1];
	char course_name[SC_COURSE_NAME_MAX + 1];
	sc_course_type_t type;
	char season_id[SC_SEASON_ID_MAX + 1];
	time_t starts_at;
	time_t expires_at;
	time_t activated_at;
	time_t finalized_at;
	time_t archive_completed_at;
} sc_course_t;

typedef struct {
	char id[SC_SEASON_ID_MAX + 1];
	char name[SC_SEASON_NAME_MAX + 1];
	time_t starts_at;
	time_t expires_at;
} sc_season_t;

typedef struct {
	sc_season_t seasons[SC_MAX_SEASONS];
	sc_course_t courses[SC_MAX_COURSES];
	int num_seasons;
	int num_courses;
} sc_config_t;

typedef struct {
	int line;
	char message[160];
} sc_validation_result_t;

typedef struct {
	char revision[65];
	unsigned int raw_hash;
	int raw_length;
	char raw_text[SC_CFG_TEXT_MAX + 1];
} sc_cfg_snapshot_t;

typedef enum {
	SC_CFG_SNAPSHOT_DB_ERROR = -2,
	SC_CFG_SNAPSHOT_INVALID = -1,
	SC_CFG_SNAPSHOT_MISSING = 0,
	SC_CFG_SNAPSHOT_OK = 1
} sc_cfg_snapshot_result_t;

typedef struct {
	char token[17];
	char revision[65];
	time_t expires_at;
	int destructive;
	int valid;
} sc_apply_plan_t;

typedef struct {
	char token[17];
	char revision[65];
	int owner_client_num;
	time_t expires_at;
	int valid;
} sc_restore_plan_t;

typedef struct {
	char course_id[SC_COURSE_ID_MAX + 1];
	int rank;
} sc_jka_position_scan_t;

typedef int (*sc_json_write_fn)(void *context, const char *text, size_t length);

typedef struct {
	sc_json_write_fn write;
	void *context;
	int failed;
	int first_entry;
} sc_json_stream_t;

int SC_ParseUtcTimestamp(const char *text, time_t *out);
sc_course_state_t SC_ResolveCourseState(const sc_course_t *course, time_t now);
int SC_ValidatePathComponent(const char *component);
int SC_ValidateExportPathIds(const sc_course_t *course);
int SC_BuildArchivePath(char *out, size_t out_size, const char *season_id,
	const char *course_id, const char *player, const char *filename);
int SC_JsonWriteEscapedString(char *out, size_t out_size, const char *value);
int SC_EscapeConsoleText(char *out, size_t out_size, const char *value);
int SC_FormatCalendarTime(char *out, size_t out_size, const struct tm *calendar);
int SC_FormatLocalTime(char *out, size_t out_size, time_t value);
int SC_FormatIsoUtcFilenameTime(char *out, size_t out_size, time_t value);
int SC_CopySeasonName(char *out, size_t out_size, const sc_config_t *config, const char *season_id);
int SC_FormatExpiryHeading(char *out, size_t out_size, const char *course_name);
void SC_InitJkaPositionScan(sc_jka_position_scan_t *scan);
int SC_ScanJkaPosition(sc_jka_position_scan_t *scan, const char *username, const char *course_id, const char *row_username);
int SC_FormatJkaPosition(char *out, size_t out_size, int rank, int available);
int SC_FormatCompactTimeLeft(char *out, size_t out_size, time_t seconds);
int SC_FormatCourseListHeader(char *out, size_t out_size, int include_position);
int SC_CaptureCfgSnapshot(sc_cfg_snapshot_t *snapshot, const char *revision, const char *raw_text);
int SC_VerifyCfgSnapshot(const sc_cfg_snapshot_t *snapshot);
int SC_ParseConfigText(const char *text, sc_config_t *out, sc_validation_result_t *result);
void SC_FreeConfig(sc_config_t *config);
int SC_CourseIdentityChanged(const sc_course_t *applied, const sc_course_t *proposed);
int SC_DescribeCourseDifferences(char *out, size_t out_size, const sc_course_t *applied, const sc_course_t *proposed);
int SC_ReportLineFits(size_t used, size_t line_length, size_t capacity);
int SC_CourseIdentityCanChange(int has_event_history);
time_t SC_NextConfiguredTransition(const sc_config_t *config, time_t now);
int SC_ShouldNotifyAfterFullAdminLogin(int gained_full_admin, int revision_mismatch);
int SC_CourseSchedulesOverlap(const sc_course_t *first, const sc_course_t *second);
int SC_CourseCanBeApplied(const sc_course_t *course, time_t now);
int SC_CourseCanBeAppliedOrRetainFinalized(const sc_course_t *course, int already_finalized, time_t now);
int SC_BeginLeaderboardJson(sc_json_stream_t *stream, const sc_course_t *course, const char *revision);
int SC_AppendLeaderboardJsonEntry(sc_json_stream_t *stream, const char *username, int style, int duration_ms, int topspeed, int average, int end_time);
int SC_EndLeaderboardJson(sc_json_stream_t *stream);
int SC_RenderLeaderboardEntryJson(char *out, size_t out_size, const char *username, int style, int duration_ms, int topspeed, int average, int end_time);
int SC_AppendStatusSeasonJson(char *out, size_t out_size, const sc_season_t *season);
int SC_AppendStatusCourseJson(char *out, size_t out_size, const sc_course_t *course, const char *lifecycle, const char *demos_visibility, const char *leaderboard_path);
int SC_AppendStatusJsonRow(char *out, size_t capacity, size_t *used, int *first, const char *row);
int SC_StatusJsonHasRoom(size_t used, size_t row_length, size_t capacity);
int SC_ShouldAttemptArchiveRetry(time_t retry_at, time_t now);
void SC_RememberApplyPlan(sc_apply_plan_t *plan, const char *revision, int destructive, time_t now);
int SC_CanConfirmApply(const char *token, const char *revision, time_t now);
void SC_RememberRestorePlan(sc_restore_plan_t *plan, const char *revision, int owner_client_num, time_t now);
int SC_CanConfirmRestore(const sc_restore_plan_t *plan, const char *token, const char *revision, int owner_client_num, time_t now);

#ifdef __cplusplus
}
#endif

#endif
