#include "g_secretcourses_core.h"

#include <cstring>
#include <ctime>
#include <string>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(secret_course_history_locks_identity)
{
	BOOST_CHECK(SC_CourseIdentityCanChange(0));
	BOOST_CHECK(!SC_CourseIdentityCanChange(1));
}

BOOST_AUTO_TEST_CASE(secret_course_expiry_extension_defers)
{
	sc_config_t cfg = {};

	cfg.num_courses = 1;
	std::strcpy(cfg.courses[0].id, "sc1");
	std::strcpy(cfg.courses[0].course_name, "course");
	std::strcpy(cfg.courses[0].season_id, "s1");
	cfg.courses[0].type = SC_COURSE_TYPE_CUP;
	cfg.courses[0].starts_at = 200;
	cfg.courses[0].expires_at = 300;
	BOOST_CHECK_EQUAL(SC_NextConfiguredTransition(&cfg, 199), 200);

	cfg.courses[0].expires_at = 400;
	BOOST_CHECK_EQUAL(SC_NextConfiguredTransition(&cfg, 299), 400);
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_alert_requires_full_amlogin)
{
	BOOST_CHECK(!SC_ShouldNotifyAfterFullAdminLogin(0, 1));
	BOOST_CHECK(!SC_ShouldNotifyAfterFullAdminLogin(1, 0));
	BOOST_CHECK(SC_ShouldNotifyAfterFullAdminLogin(1, 1));
}

BOOST_AUTO_TEST_CASE(secret_course_reused_name_requires_nonoverlapping_schedule)
{
	sc_course_t first = {};
	sc_course_t second = {};

	std::strcpy(first.course_name, "racearena_pro (dash1)");
	first.type = SC_COURSE_TYPE_CUP;
	first.starts_at = 100;
	first.expires_at = 200;
	second = first;
	second.starts_at = 200;
	second.expires_at = 300;
	BOOST_CHECK(!SC_CourseSchedulesOverlap(&first, &second));
	second.starts_at = 199;
	BOOST_CHECK(SC_CourseSchedulesOverlap(&first, &second));
}

BOOST_AUTO_TEST_CASE(secret_course_rejects_expired_cup_at_apply)
{
	sc_course_t cup = {};

	cup.type = SC_COURSE_TYPE_CUP;
	cup.starts_at = 100;
	cup.expires_at = 200;
	BOOST_CHECK(!SC_CourseCanBeApplied(&cup, 200));
	BOOST_CHECK(SC_CourseCanBeApplied(&cup, 199));
}

BOOST_AUTO_TEST_CASE(secret_course_leaderboard_entry_json)
{
	char entry[256];

	BOOST_CHECK(SC_RenderLeaderboardEntryJson(entry, sizeof(entry), "shakur", 1, 10162, 1066, 758, 123));
	BOOST_CHECK(strstr(entry, "\"username\":\"shakur\""));
	BOOST_CHECK(strstr(entry, "\"duration_ms\":10162"));
}

BOOST_AUTO_TEST_CASE(secret_course_export_id_limits)
{
	sc_course_t cup = {};

	strcpy(cup.season_id, "season-1234567890");
	strcpy(cup.id, "course-123");
	cup.type = SC_COURSE_TYPE_CUP;
	BOOST_CHECK(SC_ValidateExportPathIds(&cup));
	strcat(cup.id, "x");
	BOOST_CHECK(!SC_ValidateExportPathIds(&cup));
}

BOOST_AUTO_TEST_CASE(secret_course_status_row_escapes_strings)
{
	sc_course_t course = {};
	char row[1024];

	std::strcpy(course.id, "id");
	std::strcpy(course.course_name, "course\\name");
	std::strcpy(course.season_id, "season");
	course.type = SC_COURSE_TYPE_CUP;
	BOOST_CHECK(SC_AppendStatusCourseJson(row, sizeof(row), &course, "active", "private", "/path"));
	BOOST_CHECK(strstr(row, "course\\\\name"));
}

BOOST_AUTO_TEST_CASE(secret_course_status_season_row_includes_metadata)
{
	sc_season_t season = {};
	char row[512];

	std::strcpy(season.id, "cc-s3");
	std::strcpy(season.name, "Challenge \"Cup\"");
	season.starts_at = 1791000000;
	season.expires_at = 1794000000;

	BOOST_CHECK(SC_AppendStatusSeasonJson(row, sizeof(row), &season));
	BOOST_CHECK(strstr(row, "\"id\":\"cc-s3\""));
	BOOST_CHECK(strstr(row, "\"name\":\"Challenge \\\"Cup\\\"\""));
	BOOST_CHECK(strstr(row, "\"starts_at\":1791000000"));
	BOOST_CHECK(strstr(row, "\"expires_at\":1794000000"));
}

BOOST_AUTO_TEST_CASE(secret_course_status_v2_row_includes_cup_metadata)
{
	sc_course_t course = {};
	char row[1024];

	std::strcpy(course.id, "sumot");
	std::strcpy(course.course_name, "hothchallenges (sumotower)");
	std::strcpy(course.season_id, "cc-s3");
	course.type = SC_COURSE_TYPE_CUP;
	course.starts_at = 1791200000;
	course.expires_at = 1791800000;
	course.activated_at = 1791200000;

	BOOST_CHECK(SC_AppendStatusCourseJson(row, sizeof(row), &course, "active", "private", "/secretcourses/cup/cc-s3/sumot/leaderboard.json"));
	BOOST_CHECK(strstr(row, "\"season_id\":\"cc-s3\""));
	BOOST_CHECK(strstr(row, "\"starts_at\":1791200000"));
	BOOST_CHECK(strstr(row, "\"expires_at\":1791800000"));
	BOOST_CHECK(strstr(row, "\"activated_at\":1791200000"));
	BOOST_CHECK(strstr(row, "\"finalized_at\":null"));
}

BOOST_AUTO_TEST_CASE(secret_course_archive_retry_is_pass_scoped)
{
	BOOST_CHECK(SC_ShouldAttemptArchiveRetry(0, 100));
	BOOST_CHECK(SC_ShouldAttemptArchiveRetry(100, 100));
	BOOST_CHECK(!SC_ShouldAttemptArchiveRetry(101, 100));
}

BOOST_AUTO_TEST_CASE(secret_course_finalized_cup_can_be_retained_after_expiry)
{
	sc_course_t cup = {};

	cup.type = SC_COURSE_TYPE_CUP;
	cup.expires_at = 200;
	BOOST_CHECK(!SC_CourseCanBeApplied(&cup, 200));
	BOOST_CHECK(SC_CourseCanBeAppliedOrRetainFinalized(&cup, 1, 200));
	BOOST_CHECK(!SC_CourseCanBeAppliedOrRetainFinalized(&cup, 0, 200));
}

BOOST_AUTO_TEST_CASE(secret_course_status_row_reserves_json_closure)
{
	BOOST_CHECK(SC_StatusJsonHasRoom(10, 5, 18));
	BOOST_CHECK(!SC_StatusJsonHasRoom(10, 5, 17));
}

BOOST_AUTO_TEST_CASE(secret_course_change_report_names_every_changed_field)
{
	sc_course_t applied = {};
	sc_course_t proposed = {};
	char fields[128];

	std::strcpy(applied.course_name, "old_course");
	std::strcpy(applied.season_id, "old-season");
	applied.type = SC_COURSE_TYPE_CUP;
	applied.starts_at = 100;
	applied.expires_at = 200;
	std::strcpy(proposed.course_name, "new_course");
	proposed.type = SC_COURSE_TYPE_PERMANENT;
	proposed.starts_at = 101;
	BOOST_CHECK(SC_DescribeCourseDifferences(fields, sizeof(fields), &applied, &proposed));
	BOOST_CHECK_EQUAL(fields, "name, type, season, start, expiry");
}

BOOST_AUTO_TEST_CASE(secret_course_change_report_reserves_newline_when_batching)
{
	BOOST_CHECK(SC_ReportLineFits(10, 5, 17));
	BOOST_CHECK(!SC_ReportLineFits(10, 7, 17));
}

BOOST_AUTO_TEST_CASE(secret_course_allows_same_name_for_projection_conflict_reporting)
{
	const char *cfg_text =
		"secretcourses_version 2\n"
		"sc_season s1 \"Season One\" 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z\n"
		"sc_course cup-a \"same_course\" cup s1 2026-09-25T10:00:00Z 2026-09-25T10:30:00Z\n"
		"sc_course cup-b \"same_course\" cup s1 2026-09-25T10:30:00Z 2026-09-25T11:00:00Z\n";
	sc_config_t config = {};
	sc_validation_result_t result = {};

	BOOST_CHECK(SC_ParseConfigText(cfg_text, &config, &result));
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_requires_v2_season_name_migration)
{
	const char *cfg_text =
		"secretcourses_version 1\n"
		"sc_season s1 1 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z\n";
	sc_config_t config = {};
	sc_validation_result_t result = {};

	BOOST_CHECK(!SC_ParseConfigText(cfg_text, &config, &result));
	BOOST_CHECK_EQUAL(result.message, "secretcourses_version 1 requires migration to version 2");
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_rejects_numeric_or_non_ascii_season_names)
{
	const char *numeric_name =
		"secretcourses_version 2\n"
		"sc_season s1 1 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z\n";
	const char *non_ascii_name =
		"secretcourses_version 2\n"
		"sc_season s1 \"Se\xC3\xA1son\" 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z\n";
	sc_config_t config = {};
	sc_validation_result_t result = {};

	BOOST_CHECK(!SC_ParseConfigText(numeric_name, &config, &result));
	BOOST_CHECK(!SC_ParseConfigText(non_ascii_name, &config, &result));
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_requires_the_season_name_token_to_be_quoted)
{
	const char *cfg_text =
		"secretcourses_version 2\n"
		"sc_season s1 Summer 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z // \"not the name\"\n";
	sc_config_t config = {};
	sc_validation_result_t result = {};

	BOOST_CHECK(!SC_ParseConfigText(cfg_text, &config, &result));
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_reports_maximum_course_limit)
{
	std::string cfg_text =
		"secretcourses_version 2\n"
		"sc_season s1 \"Season One\" 2026-09-25T10:00:00Z 2026-09-25T12:00:00Z\n";
	sc_config_t config = {};
	sc_validation_result_t result = {};
	int index;

	for (index = 0; index <= SC_MAX_COURSES; index++) {
		cfg_text += "sc_course course" + std::to_string(index) + " \"Course " + std::to_string(index) + "\" cup s1 2026-09-25T10:00:00Z 2026-09-25T11:00:00Z\n";
	}
	BOOST_CHECK(!SC_ParseConfigText(cfg_text.c_str(), &config, &result));
	BOOST_CHECK_EQUAL(result.message, "too many courses (maximum 64)");
}

BOOST_AUTO_TEST_CASE(secret_course_console_escape_is_ascii_safe)
{
	char escaped[64];
	const unsigned char *cursor;

	BOOST_CHECK(SC_EscapeConsoleText(escaped, sizeof(escaped), "A\"\\\n\xC3"));
	BOOST_CHECK_EQUAL(escaped, "A\\\"\\\\\\n\\x" "C3");
	for (cursor = (const unsigned char *)escaped; *cursor; cursor++) {
		BOOST_CHECK(*cursor >= 0x20 && *cursor <= 0x7e);
	}
}

BOOST_AUTO_TEST_CASE(secret_course_formats_display_and_backup_times)
{
	std::tm calendar = {};
	char display[32];
	char backup[32];

	calendar.tm_year = 126;
	calendar.tm_mon = 9;
	calendar.tm_mday = 4;
	calendar.tm_hour = 20;
	calendar.tm_min = 15;
	calendar.tm_sec = 0;
	BOOST_CHECK(SC_FormatCalendarTime(display, sizeof(display), &calendar));
	BOOST_CHECK_EQUAL(display, "2026-10-04 20:15:00");
	BOOST_CHECK(SC_FormatIsoUtcFilenameTime(backup, sizeof(backup), (time_t)1791137700));
	BOOST_CHECK_EQUAL(backup, "2026-10-04T18-15-00Z");
}

BOOST_AUTO_TEST_CASE(secret_course_formats_each_local_time_independently)
{
	const time_t starts_at = (time_t)1791136800;
	const time_t expires_at = (time_t)1791144000;
	std::tm starts_calendar = *std::localtime(&starts_at);
	std::tm expires_calendar = *std::localtime(&expires_at);
	char expected_start[32];
	char expected_expiry[32];
	char start[32];
	char expiry[32];

	std::strftime(expected_start, sizeof(expected_start), "%Y-%m-%d %H:%M:%S", &starts_calendar);
	std::strftime(expected_expiry, sizeof(expected_expiry), "%Y-%m-%d %H:%M:%S", &expires_calendar);
	BOOST_CHECK(SC_FormatLocalTime(start, sizeof(start), starts_at));
	BOOST_CHECK(SC_FormatLocalTime(expiry, sizeof(expiry), expires_at));
	BOOST_CHECK_EQUAL(start, expected_start);
	BOOST_CHECK_EQUAL(expiry, expected_expiry);
}

BOOST_AUTO_TEST_CASE(secret_course_finds_applied_season_names)
{
	sc_config_t config = {};
	char season_name[SC_SEASON_NAME_MAX + 1];

	config.num_seasons = 1;
	std::strcpy(config.seasons[0].id, "season-1");
	std::strcpy(config.seasons[0].name, "October Cup");
	BOOST_CHECK(SC_CopySeasonName(season_name, sizeof(season_name), &config, "season-1"));
	BOOST_CHECK_EQUAL(season_name, "October Cup");
	BOOST_CHECK(!SC_CopySeasonName(season_name, sizeof(season_name), &config, "missing"));
}

BOOST_AUTO_TEST_CASE(secret_course_expiry_heading_uses_ascii_safe_course_name)
{
	char heading[128];
	const unsigned char *cursor;

	BOOST_CHECK(SC_FormatExpiryHeading(heading, sizeof(heading), "Caf\xC3\xA9\\course"));
	BOOST_CHECK_EQUAL(heading, "Caf\\xC3\\xA9\\\\course expired, final standings");
	for (cursor = (const unsigned char *)heading; *cursor; cursor++) {
		BOOST_CHECK(*cursor >= 0x20 && *cursor <= 0x7e);
	}
}

BOOST_AUTO_TEST_CASE(secret_course_scans_rank_from_final_standings_order)
{
	sc_jka_position_scan_t scan = {};

	SC_InitJkaPositionScan(&scan);
	BOOST_CHECK_EQUAL(SC_ScanJkaPosition(&scan, "current", "cup-a", "alice"), 0);
	BOOST_CHECK_EQUAL(SC_ScanJkaPosition(&scan, "current", "cup-a", "bob"), 0);
	BOOST_CHECK_EQUAL(SC_ScanJkaPosition(&scan, "current", "cup-a", "current"), 3);
	BOOST_CHECK_EQUAL(SC_ScanJkaPosition(&scan, "current", "cup-b", "current"), 1);
}

BOOST_AUTO_TEST_CASE(secret_course_formats_jka_position_states)
{
	char position[64];

	BOOST_CHECK(SC_FormatJkaPosition(position, sizeof(position), 4, 1));
	BOOST_CHECK_EQUAL(position, "#4");
	BOOST_CHECK(SC_FormatJkaPosition(position, sizeof(position), 0, 1));
	BOOST_CHECK_EQUAL(position, "unranked");
	BOOST_CHECK(SC_FormatJkaPosition(position, sizeof(position), 0, 0));
	BOOST_CHECK_EQUAL(position, "position unavailable");
}

BOOST_AUTO_TEST_CASE(secret_course_formats_compact_time_left)
{
	char time_left[32];

	BOOST_CHECK(SC_FormatCompactTimeLeft(time_left, sizeof(time_left), 66));
	BOOST_CHECK_EQUAL(time_left, "1m 6s left");
	BOOST_CHECK(SC_FormatCompactTimeLeft(time_left, sizeof(time_left), 3660));
	BOOST_CHECK_EQUAL(time_left, "1h 1m left");
	BOOST_CHECK(SC_FormatCompactTimeLeft(time_left, sizeof(time_left), 4));
	BOOST_CHECK_EQUAL(time_left, "4s left");
	BOOST_CHECK(SC_FormatCompactTimeLeft(time_left, sizeof(time_left), -1));
	BOOST_CHECK_EQUAL(time_left, "Any second now...");
}

BOOST_AUTO_TEST_CASE(secret_course_formats_list_header_with_row_widths)
{
	char header[128];

	BOOST_CHECK(SC_FormatCourseListHeader(header, sizeof(header), 1));
	BOOST_CHECK_EQUAL((int)(strstr(header, "Secret Until") - header), 36);
	BOOST_CHECK_EQUAL((int)(strstr(header, "JKA position") - header), 75);
	BOOST_CHECK(SC_FormatCourseListHeader(header, sizeof(header), 0));
	BOOST_CHECK(strstr(header, "JKA position") == NULL);
}

BOOST_AUTO_TEST_CASE(secret_course_cfg_snapshot_preserves_raw_text_and_detects_tampering)
{
	sc_cfg_snapshot_t snapshot = {};

	BOOST_CHECK(SC_CaptureCfgSnapshot(&snapshot, "revision-1", "// keep this comment\nsecretcourses_version 2\n"));
	BOOST_CHECK_EQUAL(snapshot.raw_text, "// keep this comment\nsecretcourses_version 2\n");
	BOOST_CHECK(SC_VerifyCfgSnapshot(&snapshot));
	snapshot.raw_text[0] = '#';
	BOOST_CHECK(!SC_VerifyCfgSnapshot(&snapshot));
}

BOOST_AUTO_TEST_CASE(secret_course_restore_confirmation_is_owner_revision_and_time_bound)
{
	sc_restore_plan_t plan = {};

	SC_RememberRestorePlan(&plan, "revision-1", 7, 100);
	BOOST_CHECK(SC_CanConfirmRestore(&plan, plan.token, "revision-1", 7, 159));
	BOOST_CHECK(!SC_CanConfirmRestore(&plan, plan.token, "revision-2", 7, 159));
	BOOST_CHECK(!SC_CanConfirmRestore(&plan, plan.token, "revision-1", 8, 159));
	BOOST_CHECK(!SC_CanConfirmRestore(&plan, plan.token, "revision-1", 7, 160));
}

namespace {
struct streamed_json_sink_t {
	std::string text;
};

int append_streamed_json(void *context, const char *text, size_t length)
{
	streamed_json_sink_t *sink = static_cast<streamed_json_sink_t *>(context);
	sink->text.append(text, length);
	return 1;
}
}

BOOST_AUTO_TEST_CASE(secret_course_streamed_leaderboard_preserves_256_entries)
{
	sc_course_t course = {};
	sc_json_stream_t stream = {};
	streamed_json_sink_t sink;
	int index;

	std::strcpy(course.id, "cup-256");
	std::strcpy(course.course_name, "streamed_course");
	stream.write = append_streamed_json;
	stream.context = &sink;
	BOOST_REQUIRE(SC_BeginLeaderboardJson(&stream, &course, "revision-1"));
	for (index = 0; index < 256; index++) {
		char username[16];
		std::snprintf(username, sizeof(username), "player%03d", index);
		BOOST_REQUIRE(SC_AppendLeaderboardJsonEntry(&stream, username, 1,
			10000 + index, 1000, 750, 1790000000 + index));
	}
	BOOST_REQUIRE(SC_EndLeaderboardJson(&stream));

	BOOST_CHECK_GT(sink.text.size(), 2048u);
	BOOST_CHECK_EQUAL(sink.text.front(), '{');
	BOOST_CHECK_EQUAL(sink.text.back(), '}');
	for (index = 0; index < 256; index++) {
		char expected[32];
		std::snprintf(expected, sizeof(expected), "\"username\":\"player%03d\"", index);
		BOOST_CHECK_EQUAL(sink.text.find(expected), sink.text.rfind(expected));
	}
}

BOOST_AUTO_TEST_CASE(secret_course_status_append_rejects_oversize_row_without_mutation)
{
	char status[32] = "{\"rows\":[";
	char before[sizeof(status)];
	size_t used = std::strlen(status);
	int first = 1;

	BOOST_REQUIRE(SC_AppendStatusJsonRow(status, sizeof(status), &used, &first, "{\"id\":1}"));
	BOOST_CHECK_EQUAL(status, "{\"rows\":[{\"id\":1}");
	BOOST_CHECK_EQUAL(first, 0);
	std::strcpy(before, status);
	{
		size_t previous_used = used;
		int previous_first = first;
		BOOST_CHECK(!SC_AppendStatusJsonRow(status, sizeof(status), &used, &first, "{\"id\":123456789}"));
		BOOST_CHECK_EQUAL(status, before);
		BOOST_CHECK_EQUAL(used, previous_used);
		BOOST_CHECK_EQUAL(first, previous_first);
	}
}
