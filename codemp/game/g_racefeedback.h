#ifndef G_RACEFEEDBACK_H
#define G_RACEFEEDBACK_H

#include "qcommon/q_shared.h"

struct gentity_s;

typedef struct { int elapsedMs, averageSpeed, topSpeed; } raceFeedbackSample_t;
typedef enum {
	RACE_FEEDBACK_SAVEPOINT, RACE_FEEDBACK_CHECKPOINT,
	RACE_FEEDBACK_REQUIRED_CHECKPOINT
} raceFeedbackKind_t;

enum {
	RACE_FEEDBACK_CENTER = 1,
	RACE_FEEDBACK_CONSOLE = 2,
	RACE_FEEDBACK_CHAT = 4
};

qboolean G_RaceFeedback_Sample(const struct gentity_s *source, raceFeedbackSample_t *sample);
void G_RaceFeedback_Send(const struct gentity_s *recipient,
	const raceFeedbackSample_t *sample, raceFeedbackKind_t kind,
	int requiredCheckpoint, unsigned int allowedChannels);

#endif
