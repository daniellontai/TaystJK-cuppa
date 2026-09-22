# Secret Courses

`secretcourses.cfg` is authoritative. Use `secretcourses_version 1`, then
`sc_season "id" number "start" "end"`, cup courses with season/start/end,
or permanent courses with only a start timestamp. Timestamps are UTC
`YYYY-MM-DDTHH:MM:SSZ`.

Run `/sc_validatecfg` to inspect changes, then `/sc_applycfg`. Destructive
retirements require the displayed `/sc_applycfg confirm <token>` command.
Re-adding an ID restores its history; an ID cannot be reused for another map
or type. Full admins receive a private alert when cfg and projection differ.

Active Secret Course times and demos remain private. Cup demos are retained in
`demos/races/secret` and, after expiry, copied below
`demos/races/expired_secret/cup/<season>/<id>/<player>/`.

Expose only these static roots:

```toml
[[static]]
url_path = "/secretcourses"
fs_path = "/.../japro/secretcourses"

[[static]]
url_path = "/secretcourse-demos"
fs_path = "/.../japro/demos/races/expired_secret"
```

Consumers fetch the status manifest first, suppress `race_demos` for private
courses, refresh changed leaderboards, and use demo URLs only from expired
cup leaderboards.
