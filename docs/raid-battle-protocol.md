# Twitch Raid Battle protocol

ExcelProtocol FX renders Raid Battle natively in the synchronized desktop overlay and OBS source. The authenticated ExcelProtocol connection advertises the `raid_battle.v1` capability. The bot remains responsible for receiving Twitch EventSub and chat events; the plugin never accepts inbound network connections or Twitch credentials.

All mutation commands use the normal authenticated command envelope and require a unique `event_id` of at most 128 characters.

## Start a raid

```json
{"type":"raid_start","event_id":"<uuid>","viewers":42}
```

`viewers` must be an integer from 1 through 1,000,000. Starting HP is `floor(1000 * viewers^0.7)`. The battle lasts at most three minutes.

## Forward an eligible chat message

```json
{
  "type":"raid_chat",
  "event_id":"<uuid>",
  "username":"DisplayName",
  "role":"sub",
  "message":"ship it 🔥"
}
```

`role` is `mod`, `vip`, `sub`/`subscriber`, or `default`/`viewer`. The plugin assigns the weapon and damage tier. Usernames are trimmed, control characters are removed, and display is capped at 32 characters. Messages are capped at 500 characters.

Each weapon tier has its own sound family: the default Blaster uses a short pulse, the Plasma Rifle uses a low energy sweep, the VIP Railgun uses a charging discharge, and the moderator Ban Hammer uses a bass-heavy impact. Runtime mixing permits one current weapon cue, one enemy cue, and one effect cue, uses priorities when they compete, and clears the mix before result stingers. The Raid tab's master-volume setting scales the bounded mix. The native ExcelProtocol FX source sends this mix directly into OBS, so its level, mute state, and monitoring are available in the OBS Audio Mixer without playing through the Windows desktop device.

The exact messages `break;` and `!git push --force` clear the Infinite Loop and Merge Conflict roadblocks. A fire emoji activates a bounded firewall damage zone. An ice emoji slows bug movement by 50% for five seconds.

Any accepted chat participation guarantees the victory presentation. Participation and viewer count still control battle speed, density, hazards, weapon mix, and leaderboard results. A raid fails only when no valid chat action occurs during the entire window.

The renderer shows one warrior per incoming viewer for raids up to 48 viewers. Larger raids use 48 field units and report how many viewers each unit represents. Enemies enter from off-screen in waves, chat focuses the marked front target, and individual enemies have health, hit, death, and replacement animations.

## End or query

```json
{"type":"raid_end","event_id":"<uuid>","victory":true}
```

An attempted failure result is converted to victory if the raid has accepted participation.

```json
{"type":"get_raid_status"}
```

The status response contains the phase, HP, maximum HP, remaining time, roadblock, hazards, field-unit count, viewers represented per unit, enemy count, defeated-enemy count, interaction count, test mode, result, and Top 5 leaderboard. The dashboard should use the response acknowledgement rather than assuming that a command succeeded.

## Safety and limits

The plugin bounds enemies, warriors, projectiles, audio voices, message sizes, usernames, viewer counts, battle duration, and result duration. Rendering uses the existing 30 fps native surface. The emergency clear hotkey and dock button immediately remove the battle, projectiles, effects, audio, and queued work. Audio buffers are reset, unprepared, and closed during normal cleanup and plugin shutdown.
