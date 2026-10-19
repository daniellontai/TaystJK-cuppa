#ifndef G_SECRETCOURSES_H
#define G_SECRETCOURSES_H

#include "g_secretcourses_core.h"

#define SC_MAX_PROJECTION_CONFLICTS SC_MAX_COURSES

typedef struct {
	sc_course_t course;
	char fields[96];
	char reason[64];
} sc_projection_course_detail_t;

typedef struct {
	char first_id[SC_COURSE_ID_MAX + 1];
	char second_id[SC_COURSE_ID_MAX + 1];
	char course_name[SC_COURSE_NAME_MAX + 1];
} sc_projection_schedule_conflict_t;

typedef struct {
	int additions;
	int retirements;
	int corrections;
	int updates;
	int changes;
	int schedule_conflicts;
	int reactivations;
	int unchanged;
	int num_addition_details;
	int num_retirement_details;
	int num_correction_details;
	int num_update_details;
	int num_blocked_details;
	int num_reactivation_details;
	int num_schedule_conflict_details;
	sc_projection_course_detail_t addition_details[SC_MAX_COURSES];
	sc_projection_course_detail_t retirement_details[SC_MAX_COURSES];
	sc_projection_course_detail_t correction_details[SC_MAX_COURSES];
	sc_projection_course_detail_t update_details[SC_MAX_COURSES];
	sc_projection_course_detail_t blocked_details[SC_MAX_COURSES];
	sc_projection_course_detail_t reactivation_details[SC_MAX_COURSES];
	sc_projection_schedule_conflict_t schedule_conflict_details[SC_MAX_PROJECTION_CONFLICTS];
} sc_projection_plan_t;

void SC_EnsureConfigTemplate(void);
qboolean SC_ReadAndValidateConfig(sc_config_t *config, char revision[65], sc_validation_result_t *error);
const char *SC_GetLastConfigText(void);
void SC_ValidateConfig_f(gentity_t *ent);
void SC_ApplyConfig_f(gentity_t *ent);
void SC_RestoreConfig_f(gentity_t *ent);
void SC_NotifyConfigRevisionAfterFullAdminLogin(gentity_t *ent);
void SC_ReconcileSchedule(time_t now);
time_t SC_GetNextTransitionTime(void);
void SC_WriteStaticExports(void);

qboolean SC_DBGetAppliedRevision(char revision[65]);
sc_cfg_snapshot_result_t SC_DBLoadCfgSnapshot(const char *revision, sc_cfg_snapshot_t *out);
qboolean SC_DBGetLifecycleRows(sc_course_t *out, int capacity, int *count);
qboolean SC_DBGetAppliedCourses(sc_course_t *out, int capacity, int *count);
qboolean SC_DBActivateCourse(const sc_course_t *course, time_t now);
qboolean SC_DBClaimExpiredCourse(const char *id, time_t now);
qboolean SC_DBMarkArchiveComplete(const char *id, time_t now);
qboolean SC_DBWriteLeaderboardJson(const sc_course_t *course, const char *revision, sc_json_stream_t *stream);
void SC_DBLogExpiredCourseById(const char *id);
qboolean SC_ArchiveExpiredCourseDemos(const sc_course_t *course);
qboolean SC_DBPlanProjection(const sc_config_t *config, sc_projection_plan_t *plan);
qboolean SC_DBApplyProjection(const sc_config_t *config, const char *revision, const char *raw_cfg_text, qboolean allow_destructive);

#endif
