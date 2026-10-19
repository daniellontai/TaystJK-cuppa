# Secret Courses

Secret Courses are cfg-owned race events. Normal jaPRO records remain normal
records; each Secret Course ID owns an independent event leaderboard.

## Configuration

The server reads `secretcourses.cfg`. A missing or empty file is bootstrapped
with a commented template. Timestamps are UTC ISO-8601 values ending in `Z`.

```cfg
secretcourses_version 2

// sc_season <season-id> "<name>" <starts-at> <expires-at>
sc_season summer-1 "Summer Cup" 2026-06-01T00:00:00Z 2026-08-31T23:59:59Z

// sc_course <course-id> "<course-name>" cup <season-id> <starts-at> <expires-at>
sc_course cup-dash1 "racearena_pro (dash1)" cup summer-1 2026-06-10T18:00:00Z 2026-06-17T18:00:00Z

// sc_course <course-id> "<course-name>" permanent <starts-at>
sc_course permanent-jump "racearena_pro (jump3)" permanent 2026-06-01T00:00:00Z
```

Version 1 cfgs are rejected. Season names are quoted, player-visible labels;
season IDs and course IDs remain filesystem-safe identifiers. Cup intervals
must fit inside their season. A new cup already expired when applied is
rejected.

## Applying changes

1. Run `/sc_validatecfg` for the detailed, batched report.
2. Run `/sc_applycfg` for the concise apply result.
3. Retirements and no-history identity corrections require
   `/sc_applycfg confirm <token>` within 60 seconds. Apply tokens are six
   hexadecimal characters; restore tokens are eight.

An apply writes the validated cfg as the applied projection and records its
exact raw cfg snapshot. Comments, ordering, and cfg-only metadata are part of
that snapshot. Unapplied edits do not affect lifecycle, exports, or `/sc`.

`/sc_restorecfg` restores the exact snapshot of the current applied revision
after a separate confirmation. It first creates a timestamped backup of the
editable cfg, then overwrites that cfg only. Restore does not apply the cfg or
change lifecycle, exports, or the applied revision. Snapshots exist only for
cfgs applied after this feature was deployed; older applied revisions report
that no saved snapshot is available.

`/sc_validatecfg`, `/sc_applycfg`, and `/sc_restorecfg` require the Secret
Course administrator permission and appear in the gated SecretCourse section
of `/aminfo`.

## Identity and retirement

Before an ID has event-run history, its name or type may be corrected with a
destructive apply confirmation. Once history exists, its name and type are
immutable. Once finalized, its cfg-visible name, type, season, and schedule
are locked.

Schedules may otherwise be adjusted before finalization. A display name may be
used by multiple IDs only when their schedules do not overlap. When an old
course is no longer needed in the applied projection, remove it from cfg to
retire its ID. A retired ID with event history cannot be reactivated; a
retired, non-finalized ID without event history may be reactivated by adding
it back to cfg.

## Lifecycle

Cup courses report `scheduled`, `active`, then `expired`; their finalization
timestamp records the one-time expiry handling. Permanent courses report
`scheduled` then `active` and do not expire.

At cup activation, the best valid normal `LocalRun` result for each
player/style is copied into the event table. Subsequent active-course finishes
continue to update normal records and independently update the event record.

At cup expiry, the server finalizes once: it copies matching demos from
`demos/races/secret/<player>/` to
`demos/races/expired_secret/cup/<season-id>/<course-id>/<player>/<filename>`
without deleting the originals, broadcasts and logs JKA final standings,
regenerates exports, and removes secret masking. Permanent-course demos remain
private.

In race mode, `/sc` lists active cups grouped by season plus active permanent
courses. Logged-in players also see their JKA cup position, `unranked`, or
`position unavailable` if the lookup fails.

## Exports

The status manifest is `secretcourses/secretcourses-status.json`. Its
`schema_version` is the number `2`; it contains the string `revision`, a
`seasons` array, and a `courses` array. A season has string `id` and `name`
plus integer Unix-second `starts_at` and `expires_at` values. A course has
string `id`, `course_name`, `type`, `lifecycle`, `demos_visibility`, and
`leaderboard_path`; string-or-`null` `season_id`; integer `starts_at`; and
integer-or-`null` `expires_at`, `activated_at`, and `finalized_at`. Type is
`cup` or `permanent`; lifecycle is `scheduled`, `active`, or `expired`.
`leaderboard_path` is the same generated path with a leading `/`.

Cup leaderboards are written to
`secretcourses/cup/<season-id>/<course-id>/leaderboard.json`; permanent
leaderboards are written to
`secretcourses/permanent/<course-id>/leaderboard.json`. Each leaderboard has
numeric `schema_version` `1`, string `revision`, string `course_id` and
`course_name`, string `demos_visibility`, and an `entries` array. Every entry
has string `username` and `time`, plus integer `style`, `duration_ms`,
`topspeed`, `average`, and `end_time`. `demos_visibility` changes from
`private` to `public` after a successful cup archive. Entries do not include a
demo filename because event rows do not retain one.

## Operator checks

- Confirm `/sc_validatecfg` before every apply and use confirmation only for
  intentional retirements or corrections.
- Confirm activation snapshots expected normal PBs and an active run updates
  both normal and event records.
- Extend a cup before expiry and verify only the new expiry finalizes it.
- Confirm final standings appear once and archives preserve the private demo.
- Confirm `/sc_restorecfg` creates a backup and restores only the editable cfg.
- Confirm a fresh full `/amlogin` alone receives cfg-drift feedback.
- Confirm idle logs do not repeatedly reload Secret Courses.
