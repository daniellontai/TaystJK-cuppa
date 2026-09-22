#include "g_secretcourses_core.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static sc_apply_plan_t sc_last_apply_plan;

static unsigned int SC_HashText(const char *text)
{
	unsigned int hash = 2166136261u;
	while (text && *text) {
		hash ^= (unsigned char)*text++;
		hash *= 16777619u;
	}
	return hash;
}

static int SC_IsLeapYear(int year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int SC_DaysInMonth(int year, int month)
{
	static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (month < 1 || month > 12) {
		return 0;
	}
	return days[month - 1] + (month == 2 && SC_IsLeapYear(year));
}

int SC_ParseUtcTimestamp(const char *text, time_t *out)
{
	int year, month, day, hour, minute, second;
	long long days = 0;
	int current_year;
	int current_month;

	if (!text || !out || strlen(text) != 20 ||
		sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second) != 6 ||
		text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
		text[16] != ':' || text[19] != 'Z' || year < 1970 || month < 1 ||
		day < 1 || day > SC_DaysInMonth(year, month) || hour > 23 || minute > 59 || second > 59) {
		return 0;
	}

	for (current_year = 1970; current_year < year; current_year++) {
		days += SC_IsLeapYear(current_year) ? 366 : 365;
	}
	for (current_month = 1; current_month < month; current_month++) {
		days += SC_DaysInMonth(year, current_month);
	}
	days += day - 1;
	*out = (time_t)(days * 86400 + hour * 3600 + minute * 60 + second);
	return 1;
}

sc_course_state_t SC_ResolveCourseState(const sc_course_t *course, time_t now)
{
	if (!course || now < course->starts_at) {
		return SC_COURSE_STATE_SCHEDULED;
	}
	if (course->type == SC_COURSE_TYPE_CUP && now >= course->expires_at) {
		return SC_COURSE_STATE_EXPIRED;
	}
	return SC_COURSE_STATE_ACTIVE;
}

int SC_ValidatePathComponent(const char *component)
{
	const unsigned char *character;

	if (!component || !component[0] || strstr(component, "..")) {
		return 0;
	}
	for (character = (const unsigned char *)component; *character; character++) {
		if (*character < 32 || *character == 127 || *character == '/' || *character == '\\' || *character == ':') {
			return 0;
		}
	}
	return 1;
}

int SC_BuildArchivePath(char *out, size_t out_size, const char *season_id,
	const char *course_id, const char *player, const char *filename)
{
	int written;

	if (!out || !out_size || !SC_ValidatePathComponent(season_id) ||
		!SC_ValidatePathComponent(course_id) || !SC_ValidatePathComponent(player) ||
		!SC_ValidatePathComponent(filename)) {
		return 0;
	}
	written = snprintf(out, out_size,
		"demos/races/expired_secret/cup/%s/%s/%s/%s",
		season_id, course_id, player, filename);
	return written >= 0 && (size_t)written < out_size;
}

int SC_JsonWriteEscapedString(char *out, size_t out_size, const char *value)
{
	const unsigned char *character = (const unsigned char *)(value ? value : "");
	size_t used = 0;
	int written;

	if (!out || out_size < 3) {
		return 0;
	}
	out[used++] = '"';
	while (*character) {
		if (*character == '"' || *character == '\\') {
			if (used + 2 >= out_size) return 0;
			out[used++] = '\\';
			out[used++] = (char)*character;
		} else if (*character == '\n' || *character == '\r' || *character == '\t') {
			if (used + 2 >= out_size) return 0;
			out[used++] = '\\';
			out[used++] = *character == '\n' ? 'n' : (*character == '\r' ? 'r' : 't');
		} else if (*character < 32) {
			if (used + 6 >= out_size) return 0;
			written = snprintf(out + used, out_size - used, "\\u%04x", *character);
			if (written != 6) return 0;
			used += 6;
		} else {
			if (used + 1 >= out_size) return 0;
			out[used++] = (char)*character;
		}
		character++;
	}
	if (used + 2 > out_size) return 0;
	out[used++] = '"';
	out[used] = '\0';
	return 1;
}

static void SC_SetValidationError(sc_validation_result_t *result, int line, const char *message)
{
	if (!result) {
		return;
	}
	result->line = line;
	snprintf(result->message, sizeof(result->message), "%s", message);
}

static int SC_CopyToken(char *destination, size_t destination_size, const char *source)
{
	if (!source || strlen(source) >= destination_size) {
		return 0;
	}
	snprintf(destination, destination_size, "%s", source);
	return 1;
}

static int SC_TokenizeLine(const char *line, char tokens[][80], int max_tokens)
{
	int count = 0;
	const char *cursor = line;

	while (*cursor) {
		size_t length = 0;
		int quoted = 0;
		while (isspace((unsigned char)*cursor)) cursor++;
		if (!*cursor || (cursor[0] == '/' && cursor[1] == '/')) break;
		if (*cursor == '"') {
			quoted = 1;
			cursor++;
		}
		if (count == max_tokens) return -1;
		while (*cursor && (quoted ? *cursor != '"' : !isspace((unsigned char)*cursor))) {
			if (length + 1 >= sizeof(tokens[0])) return -1;
			tokens[count][length++] = *cursor++;
		}
		if (quoted && *cursor != '"') return -1;
		if (quoted) cursor++;
		tokens[count][length] = '\0';
		count++;
	}
	return count;
}

static int SC_ParseInteger(const char *text, int *out)
{
	int value = 0;
	const char *cursor = text;

	if (!text || !text[0]) return 0;
	while (*cursor) {
		if (!isdigit((unsigned char)*cursor)) return 0;
		value = value * 10 + (*cursor++ - '0');
	}
	*out = value;
	return 1;
}

static sc_season_t *SC_FindSeason(sc_config_t *config, const char *id)
{
	int index;
	for (index = 0; index < config->num_seasons; index++) {
		if (!strcmp(config->seasons[index].id, id)) return &config->seasons[index];
	}
	return NULL;
}

static int SC_HasCourseIdentity(const sc_config_t *config, const char *id, const char *name)
{
	int index;
	for (index = 0; index < config->num_courses; index++) {
		if (!strcmp(config->courses[index].id, id) || !strcmp(config->courses[index].course_name, name)) return 1;
	}
	return 0;
}

void SC_FreeConfig(sc_config_t *config)
{
	if (config) memset(config, 0, sizeof(*config));
}

int SC_CourseIdentityChanged(const sc_course_t *applied, const sc_course_t *proposed)
{
	if (!applied || !proposed) {
		return 1;
	}
	return strcmp(applied->id, proposed->id) ||
		strcmp(applied->course_name, proposed->course_name) ||
		applied->type != proposed->type;
}

int SC_ConfigRequiresConfirmation(const sc_config_t *applied, const sc_config_t *proposed)
{
	int applied_index;

	if (!applied || !proposed) {
		return 1;
	}
	for (applied_index = 0; applied_index < applied->num_courses; applied_index++) {
		int proposed_index;
		for (proposed_index = 0; proposed_index < proposed->num_courses; proposed_index++) {
			if (!strcmp(applied->courses[applied_index].id, proposed->courses[proposed_index].id)) {
				if (SC_CourseIdentityChanged(&applied->courses[applied_index],
					&proposed->courses[proposed_index])) {
					return 1;
				}
				break;
			}
		}
		if (proposed_index == proposed->num_courses) {
			return 1;
		}
	}
	return 0;
}

void SC_CollectTransitions(const sc_config_t *config, time_t previous, time_t now, sc_transition_set_t *out)
{
	int index;

	if (!out) return;
	memset(out, 0, sizeof(*out));
	if (!config || now < previous) return;
	for (index = 0; index < config->num_courses; index++) {
		const sc_course_t *course = &config->courses[index];
		if (course->type == SC_COURSE_TYPE_CUP && previous < course->expires_at && now >= course->expires_at)
			out->num_expired_cup_courses++;
	}
}

int SC_IsConfiguredCourseActive(const sc_config_t *config, const char *course_name, time_t now)
{
	int index;

	if (!config || !course_name) return 0;
	for (index = 0; index < config->num_courses; index++) {
		if (!strcmp(config->courses[index].course_name, course_name) &&
			SC_ResolveCourseState(&config->courses[index], now) == SC_COURSE_STATE_ACTIVE)
			return 1;
	}
	return 0;
}

int SC_PlanDemoArchive(sc_demo_move_t *out, const char *player_and_file, const char *season_id, const char *course_id)
{
	const char *separator;
	char player[SC_COURSE_NAME_MAX + 1];
	int written;
	size_t player_length;

	if (!out || !player_and_file || !(separator = strchr(player_and_file, '/'))) return 0;
	player_length = (size_t)(separator - player_and_file);
	if (!player_length || player_length >= sizeof(player)) return 0;
	memcpy(player, player_and_file, player_length);
	player[player_length] = '\0';
	if (!SC_ValidatePathComponent(player) || !SC_ValidatePathComponent(separator + 1)) return 0;
	written = snprintf(out->source, sizeof(out->source), "demos/races/secret/%s/%s", player, separator + 1);
	if (written < 0 || (size_t)written >= sizeof(out->source)) return 0;
	return SC_BuildArchivePath(out->destination, sizeof(out->destination), season_id, course_id, player, separator + 1);
}

int SC_RenderLeaderboardJson(char *out, size_t out_size, const sc_course_t *course, sc_course_state_t state, const char *revision)
{
	char id[SC_COURSE_ID_MAX * 2 + 8];
	char name[SC_COURSE_NAME_MAX * 2 + 8];
	char rev[136];
	const char *visibility;
	int written;

	if (!out || !out_size || !course || !SC_JsonWriteEscapedString(id, sizeof(id), course->id) ||
		!SC_JsonWriteEscapedString(name, sizeof(name), course->course_name) ||
		!SC_JsonWriteEscapedString(rev, sizeof(rev), revision ? revision : "")) return 0;
	visibility = state == SC_COURSE_STATE_EXPIRED && course->type == SC_COURSE_TYPE_CUP ? "public" : "private";
	written = snprintf(out, out_size, "{\"schema_version\":1,\"revision\":%s,\"course_id\":%s,\"course_name\":%s,\"demos_visibility\":\"%s\",\"entries\":[]}", rev, id, name, visibility);
	return written >= 0 && (size_t)written < out_size;
}

int SC_ParseConfigText(const char *text, sc_config_t *out, sc_validation_result_t *result)
{
	const char *line_start;
	int line_number = 0;
	int has_version = 0;

	if (!text || !out) return 0;
	SC_FreeConfig(out);
	if (result) memset(result, 0, sizeof(*result));
	line_start = text;
	while (*line_start) {
		const char *line_end = strchr(line_start, '\n');
		char line[512];
		char tokens[8][80];
		size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
		int count;

		line_number++;
		if (length >= sizeof(line)) { SC_SetValidationError(result, line_number, "line is too long"); goto fail; }
		memcpy(line, line_start, length);
		line[length] = '\0';
		count = SC_TokenizeLine(line, tokens, 8);
		if (count < 0) { SC_SetValidationError(result, line_number, "invalid tokens"); goto fail; }
		if (!count) goto next_line;
		if (!strcmp(tokens[0], "secretcourses_version")) {
			if (has_version || count != 2 || strcmp(tokens[1], "1")) { SC_SetValidationError(result, line_number, "unsupported version"); goto fail; }
			has_version = 1;
		} else if (!strcmp(tokens[0], "sc_season")) {
			sc_season_t *season;
			if (count != 5 || out->num_seasons == SC_MAX_SEASONS || SC_FindSeason(out, tokens[1]) || !SC_ValidatePathComponent(tokens[1])) { SC_SetValidationError(result, line_number, "invalid season"); goto fail; }
			season = &out->seasons[out->num_seasons];
			if (!SC_CopyToken(season->id, sizeof(season->id), tokens[1]) || !SC_ParseInteger(tokens[2], &season->number) || !SC_ParseUtcTimestamp(tokens[3], &season->starts_at) || !SC_ParseUtcTimestamp(tokens[4], &season->expires_at) || season->starts_at >= season->expires_at) { SC_SetValidationError(result, line_number, "invalid season schedule"); goto fail; }
			out->num_seasons++;
		} else if (!strcmp(tokens[0], "sc_course")) {
			sc_course_t *course;
			if (count != 5 && count != 7) { SC_SetValidationError(result, line_number, "invalid course fields"); goto fail; }
			if (out->num_courses == SC_MAX_COURSES || SC_HasCourseIdentity(out, tokens[1], tokens[2]) || !SC_ValidatePathComponent(tokens[1])) { SC_SetValidationError(result, line_number, "duplicate or unsafe course"); goto fail; }
			course = &out->courses[out->num_courses];
			if (!SC_CopyToken(course->id, sizeof(course->id), tokens[1]) || !SC_CopyToken(course->course_name, sizeof(course->course_name), tokens[2])) { SC_SetValidationError(result, line_number, "course field is too long"); goto fail; }
			if (!strcmp(tokens[3], "cup") && count == 7) {
				sc_season_t *season = SC_FindSeason(out, tokens[4]);
				course->type = SC_COURSE_TYPE_CUP;
				if (!season || !SC_CopyToken(course->season_id, sizeof(course->season_id), tokens[4]) || !SC_ParseUtcTimestamp(tokens[5], &course->starts_at) || !SC_ParseUtcTimestamp(tokens[6], &course->expires_at) || course->starts_at >= course->expires_at || course->starts_at < season->starts_at || course->expires_at > season->expires_at) { SC_SetValidationError(result, line_number, "invalid cup schedule"); goto fail; }
			} else if (!strcmp(tokens[3], "permanent") && count == 5) {
				course->type = SC_COURSE_TYPE_PERMANENT;
				if (!SC_ParseUtcTimestamp(tokens[4], &course->starts_at)) { SC_SetValidationError(result, line_number, "invalid permanent schedule"); goto fail; }
			} else { SC_SetValidationError(result, line_number, "invalid course type"); goto fail; }
			out->num_courses++;
		} else { SC_SetValidationError(result, line_number, "unknown directive"); goto fail; }
next_line:
		if (!line_end) break;
		line_start = line_end + 1;
	}
	if (!has_version) { SC_SetValidationError(result, line_number, "missing version"); goto fail; }
	return 1;
fail:
	SC_FreeConfig(out);
	return 0;
}

void SC_RememberApplyPlan(sc_apply_plan_t *plan, const char *revision, int destructive, time_t now)
{
	unsigned int hash;

	if (!plan || !revision) return;
	memset(plan, 0, sizeof(*plan));
	hash = SC_HashText(revision) ^ (unsigned int)now;
	snprintf(plan->token, sizeof(plan->token), "%08x%08x", hash, hash ^ 0xa5a5a5a5u);
	SC_CopyToken(plan->revision, sizeof(plan->revision), revision);
	plan->expires_at = now + SC_APPLY_PLAN_TTL;
	plan->destructive = destructive != 0;
	plan->valid = 1;
	sc_last_apply_plan = *plan;
}

int SC_CanConfirmApply(const char *token, const char *revision, time_t now)
{
	return sc_last_apply_plan.valid && sc_last_apply_plan.destructive && token && revision &&
		now <= sc_last_apply_plan.expires_at && !strcmp(token, sc_last_apply_plan.token) &&
		!strcmp(revision, sc_last_apply_plan.revision);
}
