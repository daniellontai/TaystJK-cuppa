**JKA Defrag Map Maker guide:**

This guide will focus on making defrag maps for jaPRO mod.  I don't know of any other JKA MP mod that does defrag timers except for CTF fast-caps (timers based on ctf flag capture).

**Base concept:**
A defrag course has a start and a finish.  Players spawn before the start line and their timer is started by crossing the start line.  Their timer is stopped when they hit the finish line and their time is recorded if it is a personal best.  Leaderboards exist to see how they compare to other players.  You can have many courses in one map.

**Movement styles:**
Currently there are 19 different movement styles in jaPRO defrag.  A movement style is simply a different set of movement physics the player can choose to use.  Leaderboards are specific to a movement style.  The styles are:

1. **siege**
	This is the style used in siege mode of JKA.  There is no air acceleration.  Players typically use rolling, ramps, and wall-strafe to increase speed.
2. **jka**
	Default JKA style.
3. **qw**
	Inspired by quakeworld but with JKA force jump.  Players can make tighter turns while maintaining their speed.  It also has ramp jumps.  
4. **cpm**
	Inspired by CPM from q3 defrag.  No forcejumps, faster basespeed (320), high air control, rampjumps, and double jumps.
5. **q3**
	Inspired by vanilla mode from q3 defrag.  Like CPM but without increased air control.
6. **pjk**
	Like CPM but with force jump, and regular JKA basespeed.
7. **wsw**
	Inspired by Warsow.  Like CPM but with a dash move and a walljump move, which lets you redirect your speed to any angle without speed loss.  Its version of ramp jump also does not reduce your horizontal speed at all.
8. **rjq3**
	Q3 with rocket launcher.  Players propel them selves with rocket knockback.  
9. **rjcpm**
	RJQ3 with air control.
10. **swoop**
	Riding a swoop bike.  It's modified to give you a grav boost option which lowers your gravity for a few seconds.
11. **jetpack**
	Jetpack but with different jetpack physics and spiderman style grapple hook that lets you properly swing.  It also has detpacks for self knockback boost.
12. **speed**
	JKA but with faster base speed (same as forcespeed).  It only gives you 50force which does not regen.
13. **sp**
	SP inspired physics (higher air accelerate and your speed is halved when you land unless the landing point is lower than your takeoff point).
14. **slick**
	All ground friction is removed and has super fast acceleration while on ground.  No forcejump.
15. **botcpm**
	Same as CPM but it has a button that makes your strafe angles perfect.
16. **coop**
	2 player mode.  JKA physics with weaopns that let you boost eachother.  
17. **ocpm**
	Version of CPM which tries to be mroe accurate to Q3 defrags version of CPM.  Specifically 125fps physics and velocity snapping.
18. **tribes**
	Tries to mimic the skiing and jetpack style of movement from the tribes franchise.  Has a gun that you can self boost with, and a thrust pack that you can trade forcepoints for speed with.
19. **surf**
	Inspired by CS 1.6 physics.  Can be used for surf maps or bhop maps.

Seaons:
Furthermore, leaderboards can be broken down per season.  The current seasons typically lasts 1 year, for example season 5 was 2023.  A global leaderboard still exists which ignores seasons.

On jaPRO, most defrag maps are combined into "racepacks".  A racepack typically has a spawn room with doors that lead to the individual courses.  Inside the courses, there is typically a button to go back to the main room.  These can be combined into one trigger entity to save entity space.  It's a good idea to include a CTF room in a racepack with CTF spawns and CTF flags so it can be played in the CTF gametype as well.  Same for TFFA - a small TFFA room with TFFA spawns will make the map more playable.

**Mapping tips:**
There are a few things to take into account when making a defrag map.  First is the start area.  You don't want players to be able to build up a huge amount of speed before hitting the start trigger, as this is typically seen as boring gameplay called "loop starting".  Areas before the start brush are typically very small and have a patchmesh infront of the start trigger so players cant re-trigger the start once they go past it.  There are other options (trigger_newpush) that let you dynamically cap a players speed in certain directions (to prevent "loop starting") while in the start.  
Checkpoint triggers are popular to scatter throughout the course so players can gauge their speed throughout the run.
Try to make use of noise, message, etc fields on the triggers.  If you have multiple courses, the message field on the finish trigger is mandatory and acts as the courses name.

**New/modified entities:**
See here https://github.com/videoP/jaPRO/blob/master/scripts/japro_entities.def

**Flying respawn checkpoints:**

`df_trigger_checkpoint` enables `/respawn` by default. It captures the run immediately before the checkpoint and replays the recorded movement through it, then returns control at `GO!`. The official race timer continues throughout; it is never rewound. A second `/respawn` during the preparation switches to a stationary checkpoint pose with zero velocity, keeping the original `GO!` deadline and counting as the same respawn. Live movement, jump, and view input held during preparation takes effect at `GO!`.

The following entity keys use integer milliseconds for all timing values:

| Key | Default | Behavior |
| --- | --- | --- |
| `flyingrespawndisable` | `0` | Set exactly `1` to disable flying respawn at this checkpoint. The ordinary checkpoint still works. |
| `flyingrespawnrewind` | Inherit `g_flyingRespawnRewindMs`, default `1500` | Non-negative time before checkpoint contact to replay. `0` starts at contact. Invalid or negative values inherit the server default. |
| `flyingrespawnrelease` | Inherit `g_flyingRespawnReleaseOffsetMs`, default `0` | Non-negative time after checkpoint contact to replay before returning control. Negative values clamp to `0`; invalid values inherit the server default. |

For example, `flyingrespawnrewind 1500` and `flyingrespawnrelease 500` replay approximately 2000 ms of recorded movement: 1500 ms before contact and 500 ms after it. A positive release offset first leaves the new checkpoint **pending** while the player records that post-checkpoint movement. `/respawn` is unavailable until this capture is ready; rejection prints to the console without replacing the countdown. Sampling can extend the interval to the next recorded movement step. Each later checkpoint contact immediately replaces the previous target, including when the new checkpoint is pending or disabled. There is no fallback to an older checkpoint.

Server timing defaults are captured when a run starts. `g_enableFlyingRespawn` defaults to `1`; setting it to `0` rejects new checkpoint replays. `g_flyingRespawnTestMode` defaults to `0` and enables the diagnostic `/replaytest <milliseconds>` command when set to `1`, with a maximum interval of 30000 ms. Start a new run after enabling test mode to retain that much history. Diagnostic replays never submit results or add a respawn count.

Flying respawn restores the player's movement and Force state, not the shared world. Set `flyingrespawndisable 1` on checkpoints whose replay depends on changing movers, breakables, shared doors, one-shot target chains, or other world state that cannot safely repeat. Test the approach and release interval in every supported movement style; replaying a target chain does not rewind that chain's world effects.

`/savepos` is an independent practice bookmark and `/respos` restores it in practice. These commands do not replace the map checkpoint. At completion the normal public result remains unchanged, followed by a private `no-respawn` time and respawn count for the finisher. Valid respawned personal bests keep their count in `LocalRun.respawn_count`; this metadata does not change ranking. A movement/Force mismatch makes the run unranked and stores a diagnostic row in `LocalRunInvalid` with `invalid = 1`, leaving valid personal bests and Secret Course results intact.

**Flying respawn acceptance checks:**

On a test server, enable `g_flyingRespawnTestMode 1`, begin a new run, and record at least 30 seconds of movement. Run `/replaytest 20000` and `/replaytest 30000`; compare the complete original and replayed movement and Force diagnostic lines, including angles, velocity, movement state, Force values/timers, and checkpoint/course progress. Confirm that neither test changes results or the checkpoint respawn count.

Test an ordinary checkpoint replay, then a second press during preparation. Hold forward and jump while changing view during the countdown; the newest input should apply on the first `GO!` tick. Check a disabled checkpoint and a positive-release checkpoint before and after its capture becomes ready. Confirm that neither falls back to an earlier checkpoint. Verify the official timer continues across all respawns, and that bystanders receive only the normal public completion message. Finally, complete one valid respawned run and one deliberately mismatched replay. Inspect `LocalRun` for the valid best's count and `LocalRunInvalid` for the mismatch; the mismatch must not replace a valid best, award unlocks, or change Secret Course results. Disable test mode before recording those two runs, and start new runs so their diagnostic-replay flags are clear.

**Examples:**
Challenge style map: https://www.youtube.com/watch?v=NqDdeNQkCjI

Race map on built on an existing map: https://www.youtube.com/watch?v=m7W-88DdPKk

Climb style map: https://www.youtube.com/watch?v=crhV1V4IndM

Torture style map: https://www.youtube.com/watch?v=WlgNCN6MbtQ

Montage: https://www.youtube.com/watch?v=sy3HdYTx0yE

