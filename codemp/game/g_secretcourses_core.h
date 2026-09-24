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
	int number;
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
	char token[17];
	char revision[65];
	time_t expires_at;
	int destructive;
	int valid;
} sc_apply_plan_t;

typedef struct {
	int num_expired_cup_courses;
} sc_transition_set_t;

typedef struct {
	char source[256];
	char destination[256];
} sc_demo_move_t;

int SC_ParseUtcTimestamp(const char *text, time_t *out);
sc_course_state_t SC_ResolveCourseState(const sc_course_t *course, time_t now);
int SC_ValidatePathComponent(const char *component);
int SC_BuildArchivePath(char *out, size_t out_size, const char *season_id,
	const char *course_id, const char *player, const char *filename);
int SC_JsonWriteEscapedString(char *out, size_t out_size, const char *value);
int SC_ParseConfigText(const char *text, sc_config_t *out, sc_validation_result_t *result);
void SC_FreeConfig(sc_config_t *config);
int SC_ConfigRequiresConfirmation(const sc_config_t *applied, const sc_config_t *proposed);
int SC_CourseIdentityChanged(const sc_course_t *applied, const sc_course_t *proposed);
int SC_CourseIdentityCanChange(int has_event_history);
time_t SC_NextConfiguredTransition(const sc_config_t *config, time_t now);
int SC_ShouldNotifyAfterFullAdminLogin(int gained_full_admin, int revision_mismatch);
int SC_CourseSchedulesOverlap(const sc_course_t *first, const sc_course_t *second);
int SC_CourseCanBeApplied(const sc_course_t *course, time_t now);
void SC_CollectTransitions(const sc_config_t *config, time_t previous, time_t now, sc_transition_set_t *out);
int SC_IsConfiguredCourseActive(const sc_config_t *config, const char *course_name, time_t now);
int SC_PlanDemoArchive(sc_demo_move_t *out, const char *player_and_file, const char *season_id, const char *course_id);
int SC_RenderLeaderboardJson(char *out, size_t out_size, const sc_course_t *course, sc_course_state_t state, const char *revision);
void SC_RememberApplyPlan(sc_apply_plan_t *plan, const char *revision, int destructive, time_t now);
int SC_CanConfirmApply(const char *token, const char *revision, time_t now);

#ifdef __cplusplus
}
#endif

#endif
