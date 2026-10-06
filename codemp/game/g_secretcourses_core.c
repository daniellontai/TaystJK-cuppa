#include "g_secretcourses_core.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static sc_apply_plan_t sc_last_apply_plan;
static unsigned int sc_restore_token_serial;

static unsigned int SC_HashText(const char *text)
{
	unsigned int hash = 2166136261u;
	while (text && *text) {
		hash ^= (unsigned char)*text++;
		hash *= 16777619u;
	}
	return hash;
}

int SC_CaptureCfgSnapshot(sc_cfg_snapshot_t *snapshot, const char *revision, const char *raw_text)
{
	size_t length, revision_length;
	if (!snapshot || !revision || !raw_text || !(length = strlen(raw_text)) || length > SC_CFG_TEXT_MAX || (revision_length = strlen(revision)) >= sizeof(snapshot->revision)) return 0;
	memset(snapshot, 0, sizeof(*snapshot));
	memcpy(snapshot->revision, revision, revision_length + 1);
	memcpy(snapshot->raw_text, raw_text, length + 1);
	snapshot->raw_length = (int)length;
	snapshot->raw_hash = SC_HashText(raw_text);
	return 1;
}

int SC_VerifyCfgSnapshot(const sc_cfg_snapshot_t *snapshot)
{
	if (!snapshot || snapshot->raw_length < 0 || snapshot->raw_length > SC_CFG_TEXT_MAX) return 0;
	if ((int)strlen(snapshot->raw_text) != snapshot->raw_length) return 0;
	return SC_HashText(snapshot->raw_text) == snapshot->raw_hash;
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

int SC_ValidateExportPathIds(const sc_course_t *course)
{
	if (!course) {
		return 0;
	}
	if (course->type == SC_COURSE_TYPE_CUP) {
		return strlen(course->season_id) + strlen(course->id) <= 27;
	}
	return course->type == SC_COURSE_TYPE_PERMANENT && strlen(course->id) <= 22;
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
	size_t length;
	if (!source || (length = strlen(source)) >= destination_size) {
		return 0;
	}
	memcpy(destination, source, length + 1);
	return 1;
}

int SC_EscapeConsoleText(char *out, size_t out_size, const char *value)
{
	size_t used = 0;
	const unsigned char *cursor = (const unsigned char *)value;
	if (!out || !out_size || !cursor) return 0;
	while (*cursor) {
		const char *replacement = NULL;
		char hex[5];
		size_t length;
		switch (*cursor) {
		case '\\': replacement = "\\\\"; break;
		case '"': replacement = "\\\""; break;
		case '\n': replacement = "\\n"; break;
		case '\r': replacement = "\\r"; break;
		case '\t': replacement = "\\t"; break;
		default:
			if (*cursor < 0x20 || *cursor > 0x7e) {
				snprintf(hex, sizeof(hex), "\\x%02X", *cursor);
				replacement = hex;
			}
			break;
		}
		if (replacement) {
			length = strlen(replacement);
			if (used + length >= out_size) return 0;
			memcpy(out + used, replacement, length);
			used += length;
		} else {
			if (used + 1 >= out_size) return 0;
			out[used++] = (char)*cursor;
		}
		cursor++;
	}
	out[used] = '\0';
	return 1;
}

int SC_FormatCalendarTime(char *out, size_t out_size, const struct tm *calendar)
{
	return out && out_size && calendar && strftime(out, out_size, "%Y-%m-%d %H:%M:%S", calendar) > 0;
}

int SC_FormatLocalTime(char *out, size_t out_size, time_t value)
{
	struct tm *calendar = localtime(&value);
	return SC_FormatCalendarTime(out, out_size, calendar);
}

int SC_FormatIsoUtcFilenameTime(char *out, size_t out_size, time_t value)
{
	struct tm *calendar = gmtime(&value);
	return calendar && out && out_size && strftime(out, out_size, "%Y-%m-%dT%H-%M-%SZ", calendar) > 0;
}

int SC_CopySeasonName(char *out, size_t out_size, const sc_config_t *config, const char *season_id)
{
	int index;
	if (!out || !out_size || !config || !season_id) return 0;
	for (index = 0; index < config->num_seasons; index++) {
		if (!strcmp(config->seasons[index].id, season_id)) {
			return snprintf(out, out_size, "%s", config->seasons[index].name) >= 0 && strlen(config->seasons[index].name) < out_size;
		}
	}
	return 0;
}

int SC_FormatExpiryHeading(char *out, size_t out_size, const char *course_name)
{
	char escaped[SC_COURSE_NAME_MAX * 4 + 1];
	int written;
	if (!SC_EscapeConsoleText(escaped, sizeof(escaped), course_name)) return 0;
	written = snprintf(out, out_size, "%s expired, final standings", escaped);
	return written >= 0 && (size_t)written < out_size;
}

void SC_InitJkaPositionScan(sc_jka_position_scan_t *scan)
{
	if (scan) memset(scan, 0, sizeof(*scan));
}

int SC_ScanJkaPosition(sc_jka_position_scan_t *scan, const char *username, const char *course_id, const char *row_username)
{
	size_t length;
	if (!scan || !username || !course_id || !row_username) return 0;
	if (strcmp(scan->course_id, course_id)) {
		length = strlen(course_id);
		if (length >= sizeof(scan->course_id)) return 0;
		memcpy(scan->course_id, course_id, length + 1);
		scan->rank = 1;
	} else {
		scan->rank++;
	}
	return !strcmp(username, row_username) ? scan->rank : 0;
}

int SC_FormatJkaPosition(char *out, size_t out_size, int rank, int available)
{
	int written;
	if (!out || !out_size) return 0;
	if (!available) written = snprintf(out, out_size, "position unavailable");
	else if (rank > 0) written = snprintf(out, out_size, "#%d", rank);
	else written = snprintf(out, out_size, "unranked");
	return written >= 0 && (size_t)written < out_size;
}

int SC_FormatCompactTimeLeft(char *out, size_t out_size, time_t seconds)
{
	long long hours, minutes, remainder;
	int written;
	if (!out || !out_size) return 0;
	if (seconds < 0) written = snprintf(out, out_size, "Any second now...");
	else {
		hours = (long long)seconds / 3600;
		minutes = ((long long)seconds - hours * 3600) / 60;
		remainder = (long long)seconds - hours * 3600 - minutes * 60;
		if (hours > 0 && minutes > 0) written = snprintf(out, out_size, "%lldh %lldm left", hours, minutes);
		else if (hours > 0) written = snprintf(out, out_size, "%lldh left", hours);
		else if (minutes > 0 && remainder > 0) written = snprintf(out, out_size, "%lldm %llds left", minutes, remainder);
		else if (minutes > 0) written = snprintf(out, out_size, "%lldm left", minutes);
		else written = snprintf(out, out_size, "%llds left", remainder);
	}
	return written >= 0 && (size_t)written < out_size;
}

int SC_FormatCourseListHeader(char *out, size_t out_size, int include_position)
{
	int written;
	if (!out || !out_size) return 0;
	written = include_position ?
		snprintf(out, out_size, "%-35s %-38s %s", "Course Name", "Secret Until", "JKA position") :
		snprintf(out, out_size, "%-35s %s", "Course Name", "Secret Until");
	return written >= 0 && (size_t)written < out_size;
}

static int SC_TokenizeLine(const char *line, char tokens[][80], int quoted_tokens[], int max_tokens)
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
		quoted_tokens[count] = quoted;
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

static int SC_IsAsciiDisplayText(const char *text)
{
	const unsigned char *cursor = (const unsigned char *)text;
	if (!cursor || !*cursor) return 0;
	while (*cursor) {
		if (*cursor < 0x20 || *cursor > 0x7e) return 0;
		cursor++;
	}
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

static int SC_HasCourseId(const sc_config_t *config, const char *id)
{
	int index;
	for (index = 0; index < config->num_courses; index++) {
		if (!strcmp(config->courses[index].id, id)) return 1;
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

int SC_DescribeCourseDifferences(char *out, size_t out_size, const sc_course_t *applied, const sc_course_t *proposed)
{
	const char *fields[5];
	int field_count = 0;
	int index;
	size_t used = 0;

	if (!out || !out_size || !applied || !proposed) return 0;
	if (strcmp(applied->course_name, proposed->course_name)) fields[field_count++] = "name";
	if (applied->type != proposed->type) fields[field_count++] = "type";
	if (strcmp(applied->season_id, proposed->season_id)) fields[field_count++] = "season";
	if (applied->starts_at != proposed->starts_at) fields[field_count++] = "start";
	if (applied->expires_at != proposed->expires_at) fields[field_count++] = "expiry";
	out[0] = '\0';
	for (index = 0; index < field_count; index++) {
		int written = snprintf(out + used, out_size - used, "%s%s", index ? ", " : "", fields[index]);
		if (written < 0 || (size_t)written >= out_size - used) return 0;
		used += (size_t)written;
	}
	return 1;
}

int SC_ReportLineFits(size_t used, size_t line_length, size_t capacity)
{
	return used + line_length + 1 <= capacity;
}

int SC_CourseIdentityCanChange(int has_event_history)
{
	return !has_event_history;
}

time_t SC_NextConfiguredTransition(const sc_config_t *config, time_t now)
{
	time_t next = 0;
	int index;

	if (!config) return 0;
	for (index = 0; index < config->num_courses; index++) {
		const sc_course_t *course = &config->courses[index];
		if (course->starts_at > now && (!next || course->starts_at < next)) next = course->starts_at;
		if (course->type == SC_COURSE_TYPE_CUP && course->expires_at > now &&
			(!next || course->expires_at < next)) next = course->expires_at;
	}
	return next;
}

int SC_ShouldNotifyAfterFullAdminLogin(int gained_full_admin, int revision_mismatch)
{
	return gained_full_admin && revision_mismatch;
}

int SC_CourseSchedulesOverlap(const sc_course_t *first, const sc_course_t *second)
{
	if (!first || !second || strcmp(first->course_name, second->course_name)) return 0;
	if (first->type == SC_COURSE_TYPE_PERMANENT && second->type == SC_COURSE_TYPE_PERMANENT) return 1;
	if (first->type == SC_COURSE_TYPE_PERMANENT) return second->expires_at > first->starts_at;
	if (second->type == SC_COURSE_TYPE_PERMANENT) return first->expires_at > second->starts_at;
	return first->starts_at < second->expires_at && second->starts_at < first->expires_at;
}

int SC_CourseCanBeApplied(const sc_course_t *course, time_t now)
{
	return course && (course->type == SC_COURSE_TYPE_PERMANENT || course->expires_at > now);
}

int SC_CourseCanBeAppliedOrRetainFinalized(const sc_course_t *course, int already_finalized, time_t now)
{
	return already_finalized || SC_CourseCanBeApplied(course, now);
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
	visibility = course->archive_completed_at ? "public" : "private";
	written = snprintf(out, out_size, "{\"schema_version\":1,\"revision\":%s,\"course_id\":%s,\"course_name\":%s,\"demos_visibility\":\"%s\",\"entries\":[]}", rev, id, name, visibility);
	return written >= 0 && (size_t)written < out_size;
}

int SC_RenderLeaderboardEntryJson(char *out, size_t out_size, const char *username, int style, int duration_ms, int topspeed, int average, int end_time)
{
	char escaped_username[SC_COURSE_NAME_MAX * 2 + 8];
	char time_string[32];
	int written;

	if (!out || !out_size || !SC_JsonWriteEscapedString(escaped_username, sizeof(escaped_username), username)) return 0;
	if (duration_ms > 60 * 60 * 1000) {
		snprintf(time_string, sizeof(time_string), "%d:%02d:%02d.%03d", duration_ms / (60 * 60 * 1000), (duration_ms / (60 * 1000)) % 60, (duration_ms / 1000) % 60, duration_ms % 1000);
	} else if (duration_ms > 60 * 1000) {
		snprintf(time_string, sizeof(time_string), "%d:%02d.%03d", (duration_ms / (60 * 1000)) % 60, (duration_ms / 1000) % 60, duration_ms % 1000);
	} else {
		snprintf(time_string, sizeof(time_string), "%.3f", (float)duration_ms * 0.001f);
	}
	written = snprintf(out, out_size, "{\"username\":%s,\"style\":%d,\"time\":\"%s\",\"duration_ms\":%d,\"topspeed\":%d,\"average\":%d,\"end_time\":%d}", escaped_username, style, time_string, duration_ms, topspeed, average, end_time);
	return written >= 0 && (size_t)written < out_size;
}

int SC_AppendStatusSeasonJson(char *out, size_t out_size, const sc_season_t *season)
{
	char escaped_id[SC_SEASON_ID_MAX * 2 + 8];
	char escaped_name[SC_SEASON_NAME_MAX * 2 + 8];
	int written;

	if (!out || !out_size || !season ||
		!SC_JsonWriteEscapedString(escaped_id, sizeof(escaped_id), season->id) ||
		!SC_JsonWriteEscapedString(escaped_name, sizeof(escaped_name), season->name)) return 0;
	written = snprintf(out, out_size, "{\"id\":%s,\"name\":%s,\"starts_at\":%lld,\"expires_at\":%lld}",
		escaped_id, escaped_name, (long long)season->starts_at, (long long)season->expires_at);
	return written >= 0 && (size_t)written < out_size;
}

int SC_AppendStatusCourseJson(char *out, size_t out_size, const sc_course_t *course,
	const char *lifecycle, const char *demos_visibility, const char *leaderboard_path)
{
	char escaped_id[SC_COURSE_ID_MAX * 2 + 8];
	char escaped_name[SC_COURSE_NAME_MAX * 2 + 8];
	char escaped_type[32];
	char escaped_season[SC_SEASON_ID_MAX * 2 + 8];
	char escaped_lifecycle[32];
	char escaped_visibility[32];
	char escaped_path[520];
	char starts_at[32], expires_at[32], activated_at[32], finalized_at[32];
	const char *season = "null";
	int written;

	if (!out || !out_size || !course ||
		!SC_JsonWriteEscapedString(escaped_id, sizeof(escaped_id), course->id) ||
		!SC_JsonWriteEscapedString(escaped_name, sizeof(escaped_name), course->course_name) ||
		!SC_JsonWriteEscapedString(escaped_type, sizeof(escaped_type), course->type == SC_COURSE_TYPE_CUP ? "cup" : "permanent") ||
		!SC_JsonWriteEscapedString(escaped_lifecycle, sizeof(escaped_lifecycle), lifecycle) ||
		!SC_JsonWriteEscapedString(escaped_visibility, sizeof(escaped_visibility), demos_visibility) ||
		!SC_JsonWriteEscapedString(escaped_path, sizeof(escaped_path), leaderboard_path)) return 0;
	if (course->type == SC_COURSE_TYPE_CUP) {
		if (!SC_JsonWriteEscapedString(escaped_season, sizeof(escaped_season), course->season_id)) return 0;
		season = escaped_season;
	}
	snprintf(starts_at, sizeof(starts_at), "%lld", (long long)course->starts_at);
	if (course->type == SC_COURSE_TYPE_CUP) snprintf(expires_at, sizeof(expires_at), "%lld", (long long)course->expires_at);
	else snprintf(expires_at, sizeof(expires_at), "null");
	if (course->activated_at) snprintf(activated_at, sizeof(activated_at), "%lld", (long long)course->activated_at);
	else snprintf(activated_at, sizeof(activated_at), "null");
	if (course->finalized_at) snprintf(finalized_at, sizeof(finalized_at), "%lld", (long long)course->finalized_at);
	else snprintf(finalized_at, sizeof(finalized_at), "null");
	written = snprintf(out, out_size, "{\"id\":%s,\"course_name\":%s,\"type\":%s,\"season_id\":%s,\"starts_at\":%s,\"expires_at\":%s,\"lifecycle\":%s,\"activated_at\":%s,\"finalized_at\":%s,\"demos_visibility\":%s,\"leaderboard_path\":%s}",
		escaped_id, escaped_name, escaped_type, season, starts_at, expires_at, escaped_lifecycle, activated_at, finalized_at, escaped_visibility, escaped_path);
	return written >= 0 && (size_t)written < out_size;
}

int SC_StatusJsonHasRoom(size_t used, size_t row_length, size_t capacity)
{
	return used + row_length + 3 <= capacity;
}

int SC_ShouldAttemptArchiveRetry(time_t retry_at, time_t now)
{
	return !retry_at || retry_at <= now;
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
		int quoted_tokens[8];
		size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
		int count;

		line_number++;
		if (length >= sizeof(line)) { SC_SetValidationError(result, line_number, "line is too long"); goto fail; }
		memcpy(line, line_start, length);
		line[length] = '\0';
		count = SC_TokenizeLine(line, tokens, quoted_tokens, 8);
		if (count < 0) { SC_SetValidationError(result, line_number, "invalid tokens"); goto fail; }
		if (!count) goto next_line;
		if (!strcmp(tokens[0], "secretcourses_version")) {
			if (has_version || count != 2) { SC_SetValidationError(result, line_number, "unsupported version"); goto fail; }
			if (!strcmp(tokens[1], "1")) { SC_SetValidationError(result, line_number, "secretcourses_version 1 requires migration to version 2"); goto fail; }
			if (strcmp(tokens[1], "2")) { SC_SetValidationError(result, line_number, "unsupported version"); goto fail; }
			has_version = 1;
		} else if (!strcmp(tokens[0], "sc_season")) {
			sc_season_t *season;
			int legacy_number;
			if (count != 5 || !quoted_tokens[2] || !SC_IsAsciiDisplayText(tokens[2]) || SC_ParseInteger(tokens[2], &legacy_number) || out->num_seasons == SC_MAX_SEASONS || SC_FindSeason(out, tokens[1]) || !SC_ValidatePathComponent(tokens[1])) { SC_SetValidationError(result, line_number, "invalid season"); goto fail; }
			season = &out->seasons[out->num_seasons];
			if (!SC_CopyToken(season->id, sizeof(season->id), tokens[1]) || !SC_CopyToken(season->name, sizeof(season->name), tokens[2]) || !SC_ParseUtcTimestamp(tokens[3], &season->starts_at) || !SC_ParseUtcTimestamp(tokens[4], &season->expires_at) || season->starts_at >= season->expires_at) { SC_SetValidationError(result, line_number, "invalid season schedule"); goto fail; }
			out->num_seasons++;
		} else if (!strcmp(tokens[0], "sc_course")) {
			sc_course_t *course;
			if (count != 5 && count != 7) { SC_SetValidationError(result, line_number, "invalid course fields"); goto fail; }
			if (out->num_courses == SC_MAX_COURSES) { SC_SetValidationError(result, line_number, "too many courses (maximum 64)"); goto fail; }
			if (SC_HasCourseId(out, tokens[1]) || !SC_ValidatePathComponent(tokens[1])) { SC_SetValidationError(result, line_number, "duplicate or unsafe course"); goto fail; }
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
			if (!SC_ValidateExportPathIds(course)) { SC_SetValidationError(result, line_number, "course ID is too long for its public export path"); goto fail; }
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
	snprintf(plan->token, sizeof(plan->token), "%06x", hash & 0xffffffu);
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

void SC_RememberRestorePlan(sc_restore_plan_t *plan, const char *revision, int owner_client_num, time_t now)
{
	unsigned int hash;
	if (!plan || !revision) return;
	memset(plan, 0, sizeof(*plan));
	hash = SC_HashText(revision) ^ (unsigned int)now ^ ((unsigned int)owner_client_num * 2654435761u) ^ ++sc_restore_token_serial;
	snprintf(plan->token, sizeof(plan->token), "%08x", hash);
	SC_CopyToken(plan->revision, sizeof(plan->revision), revision);
	plan->owner_client_num = owner_client_num;
	plan->expires_at = now + SC_APPLY_PLAN_TTL;
	plan->valid = 1;
}

int SC_CanConfirmRestore(const sc_restore_plan_t *plan, const char *token, const char *revision, int owner_client_num, time_t now)
{
	return plan && plan->valid && token && revision && now < plan->expires_at && owner_client_num == plan->owner_client_num &&
		!strcmp(token, plan->token) && !strcmp(revision, plan->revision);
}
