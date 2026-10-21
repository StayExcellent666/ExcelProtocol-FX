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

The exact messages `break;` and `!git push --force` clear the Infinite Loop and Merge Conflict roadblocks. A fire emoji activates a bounded firewall damage zone. An ice emoji slows bug movement by 50% for five seconds.

Any accepted chat participation guarantees the victory presentation. Participation and viewer count still control battle speed, density, hazards, weapon mix, and leaderboard results. A raid fails only when no valid chat action occurs during the entire window.

## End or query

```json
{"type":"raid_end","event_id":"<uuid>","victory":true}
```

An attempted failure result is converted to victory if the raid has accepted participation.

```json
{"type":"get_raid_status"}
```

The status response contains the phase, HP, maximum HP, remaining time, roadblock, hazards, warrior count, interaction count, test mode, result, and Top 5 leaderboard. The dashboard should use the response acknowledgement rather than assuming that a command succeeded.

## Safety and limits

The plugin bounds enemies, warriors, projectiles, message sizes, usernames, viewer counts, battle duration, and result duration. Rendering uses the existing 30 fps native surface. The emergency clear hotkey and dock button immediately remove the battle, projectiles, effects, and queued work.
