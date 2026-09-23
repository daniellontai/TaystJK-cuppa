#ifndef G_SECRETCOURSES_H
#define G_SECRETCOURSES_H

#include "g_secretcourses_core.h"

typedef struct {
	int additions;
	int retirements;
	int corrections;
	int changes;
} sc_projection_plan_t;

qboolean SC_ReadAndValidateConfig(sc_config_t *config, char revision[65], sc_validation_result_t *error);
void SC_ValidateConfig_f(gentity_t *ent);
void SC_ApplyConfig_f(gentity_t *ent);
void SC_NotifyConfigRevisionAfterAdminLogin(gentity_t *ent);
void SC_ReconcileSchedule(time_t now);
void SC_WriteStaticExports(void);

qboolean SC_DBGetAppliedRevision(char revision[65]);
qboolean SC_DBHasEventHistory(const char *id);
qboolean SC_DBSnapshotLocalRuns(const char *id, const char *course_name);
qboolean SC_DBGetLifecycleRows(sc_course_t *out, int capacity, int *count);
qboolean SC_DBPlanProjection(const sc_config_t *config, sc_projection_plan_t *plan);
qboolean SC_DBApplyProjection(const sc_config_t *config, const char *revision, qboolean allow_destructive);

#endif
