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

### Terminology for integrations

A **season** groups related time-limited events. A course whose JSON `type` is
`"cup"` is one time-limited course within that season; it is not the season
itself. A `"permanent"` course has no season and no expiry. This document calls
the former a *time-limited (`cup`) course* where the distinction matters.

### Status manifest

`secretcourses/secretcourses-status.json` is the discovery document for every
applied, non-retired Secret Course. Read it before reading individual
leaderboards. Its keys are:

| Key | Type | Values / meaning |
| --- | --- | --- |
| `schema_version` | number | Always `2`. |
| `revision` | string | The applied cfg revision that produced this export. |
| `seasons` | array | Zero or more season objects. |
| `seasons[].id` | string | Season identifier. |
| `seasons[].name` | string | Player-visible season name. |
| `seasons[].starts_at` | integer | UTC Unix seconds. |
| `seasons[].expires_at` | integer | UTC Unix seconds. |
| `courses` | array | Zero or more course objects. |
| `courses[].id` | string | Course identifier. |
| `courses[].course_name` | string | Race-course name. |
| `courses[].type` | string | `"cup"` or `"permanent"`. |
| `courses[].season_id` | string or `null` | A season ID for `"cup"`; `null` for `"permanent"`. |
| `courses[].starts_at` | integer | UTC Unix seconds. |
| `courses[].expires_at` | integer or `null` | UTC Unix seconds for `"cup"`; `null` for `"permanent"`. |
| `courses[].lifecycle` | string | `"scheduled"`, `"active"`, or `"expired"`. |
| `courses[].activated_at` | integer or `null` | UTC Unix seconds once activation was recorded; otherwise `null`. |
| `courses[].finalized_at` | integer or `null` | UTC Unix seconds once expiry was finalized; otherwise `null`. This is separate from `lifecycle: "expired"`. |
| `courses[].demos_visibility` | string | `"private"` or `"public"`. Cup demos become `"public"` only after archival completes. |
| `courses[].leaderboard_path` | string | Generated leaderboard filesystem path with a leading `/`. |

The following snapshots use one `winter-26` season containing two time-limited
courses. They show the same IDs as the season moves through its lifecycle.

**Scheduled:** both courses are waiting for their start times.

```json
{
  "schema_version": 2,
  "revision": "winter-26-scheduled",
  "seasons": [{"id":"winter-26","name":"Winter Cup","starts_at":1767225600,"expires_at":1772323199}],
  "courses": [
    {
      "id": "cup-dash",
      "course_name": "racearena_pro (dash)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1768003200,
      "expires_at": 1768608000,
      "lifecycle": "scheduled",
      "activated_at": null,
      "finalized_at": null,
      "demos_visibility": "private",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-dash/leaderboard.json"
    },
    {
      "id": "cup-vault",
      "course_name": "racearena_pro (vault)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1769212800,
      "expires_at": 1769817600,
      "lifecycle": "scheduled",
      "activated_at": null,
      "finalized_at": null,
      "demos_visibility": "private",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-vault/leaderboard.json"
    }
  ]
}
```

**Active:** `cup-dash` has activated and `cup-vault` remains scheduled.

```json
{
  "schema_version": 2,
  "revision": "winter-26-active",
  "seasons": [{"id":"winter-26","name":"Winter Cup","starts_at":1767225600,"expires_at":1772323199}],
  "courses": [
    {
      "id": "cup-dash",
      "course_name": "racearena_pro (dash)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1768003200,
      "expires_at": 1768608000,
      "lifecycle": "active",
      "activated_at": 1768003205,
      "finalized_at": null,
      "demos_visibility": "private",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-dash/leaderboard.json"
    },
    {
      "id": "cup-vault",
      "course_name": "racearena_pro (vault)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1769212800,
      "expires_at": 1769817600,
      "lifecycle": "scheduled",
      "activated_at": null,
      "finalized_at": null,
      "demos_visibility": "private",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-vault/leaderboard.json"
    }
  ]
}
```

**Expired:** `cup-dash` has finalized. Its `lifecycle` is `"expired"` and its
separate `finalized_at` timestamp is populated. This example assumes its demo
archive succeeded, so visibility is `"public"`.

```json
{
  "schema_version": 2,
  "revision": "winter-26-expired",
  "seasons": [{"id":"winter-26","name":"Winter Cup","starts_at":1767225600,"expires_at":1772323199}],
  "courses": [
    {
      "id": "cup-dash",
      "course_name": "racearena_pro (dash)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1768003200,
      "expires_at": 1768608000,
      "lifecycle": "expired",
      "activated_at": 1768003205,
      "finalized_at": 1768608003,
      "demos_visibility": "public",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-dash/leaderboard.json"
    },
    {
      "id": "cup-vault",
      "course_name": "racearena_pro (vault)",
      "type": "cup",
      "season_id": "winter-26",
      "starts_at": 1769212800,
      "expires_at": 1769817600,
      "lifecycle": "scheduled",
      "activated_at": null,
      "finalized_at": null,
      "demos_visibility": "private",
      "leaderboard_path": "/secretcourses/cup/winter-26/cup-vault/leaderboard.json"
    }
  ]
}
```

### Time-limited (`cup`) course leaderboards

For the `cup-dash` examples above, the leaderboard is written to
`secretcourses/cup/winter-26/cup-dash/leaderboard.json`. Permanent-course
leaderboards use `secretcourses/permanent/<course-id>/leaderboard.json` and
the same JSON schema.

| Key | Type | Values / meaning |
| --- | --- | --- |
| `schema_version` | number | Always `1`. |
| `revision` | string | The applied cfg revision that produced this export. |
| `course_id` | string | Course identifier, such as `"cup-dash"`. |
| `course_name` | string | Race-course name. |
| `demos_visibility` | string | `"private"` or `"public"`. |
| `entries` | array | Zero or more leaderboard entries. |
| `entries[].username` | string | Player name. |
| `entries[].style` | integer | Movement-style ID. |
| `entries[].time` | string | Formatted duration. |
| `entries[].duration_ms` | integer | Raw duration in milliseconds. |
| `entries[].topspeed` | integer | Recorded top speed. |
| `entries[].average` | integer | Recorded average speed. |
| `entries[].end_time` | integer | UTC Unix seconds for the recorded run. |

The same modeled `cup-dash` course produces these lifecycle snapshots:

**Scheduled:** no event entries exist yet.

```json
{
  "schema_version": 1,
  "revision": "winter-26-scheduled",
  "course_id": "cup-dash",
  "course_name": "racearena_pro (dash)",
  "demos_visibility": "private",
  "entries": []
}
```

**Active:** entries are available, while demos remain private.

```json
{
  "schema_version": 1,
  "revision": "winter-26-active",
  "course_id": "cup-dash",
  "course_name": "racearena_pro (dash)",
  "demos_visibility": "private",
  "entries": [
    {
      "username": "Ayla",
      "style": 1,
      "time": "42.381",
      "duration_ms": 42381,
      "topspeed": 912,
      "average": 701,
      "end_time": 1768089601
    }
  ]
}
```

**Expired after successful archival:** event entries remain and visibility is
public. An expired course may still show `"private"` until archival succeeds.

```json
{
  "schema_version": 1,
  "revision": "winter-26-expired",
  "course_id": "cup-dash",
  "course_name": "racearena_pro (dash)",
  "demos_visibility": "public",
  "entries": [
    {
      "username": "Ayla",
      "style": 1,
      "time": "42.381",
      "duration_ms": 42381,
      "topspeed": 912,
      "average": 701,
      "end_time": 1768089601
    }
  ]
}
```

Entries do not include a demo filename because event rows do not retain one.
