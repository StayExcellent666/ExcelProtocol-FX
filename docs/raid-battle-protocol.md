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

An original melodic synthwave battle track loops while combat is active and stops before the victory or failure stinger. Each weapon tier has its own quieter sound family: the default Blaster uses a short pulse, the Plasma Rifle uses a low energy sweep, the VIP Railgun uses a charging discharge, and the moderator Ban Hammer uses a bass-heavy impact. Runtime mixing permits one music track, one current weapon cue, one enemy cue, and one effect cue, uses priorities when they compete, holds music at a stable level, and applies separate headroom to overlapping transient cues. The Raid tab's master-volume setting scales the bounded mix. The native ExcelProtocol FX source sends this mix directly into OBS, so its level, mute state, and monitoring are available in the OBS Audio Mixer without playing through the Windows desktop device.

The exact messages `break;` and `!git push --force` clear the Infinite Loop and Merge Conflict roadblocks. `!fire` or a fire emoji activates a bounded firewall damage zone. `!ice` or an ice emoji slows bug movement by 50% for five seconds. Each viewer can launch one attack every 900 ms; fire, ice, and roadblock commands remain immediately responsive even during that viewer's shot cooldown. The fighting overlay keeps these controls visible in a dedicated instruction panel and uses a translucent vignette so the battle remains legible over bright gameplay.

Any accepted chat participation guarantees the victory presentation. Participation and viewer count still control battle speed, density, hazards, weapon mix, and leaderboard results. A raid fails only when no valid chat action occurs during the entire window.

The total enemy health pool is `floor(1500 * viewers^0.7)`, which is 50% higher than the original balance. Active field units show a larger viewer-and-weapon tag, and the overlay keeps a four-entry live attack feed so viewers remain identifiable when large raids reuse the 48 rendered field units.

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

The plugin bounds enemies, warriors, projectiles, audio voices, message sizes, usernames, viewer counts, battle duration, and result duration. Raid-only rendering uses an adaptive 20 fps native surface while other effects retain 30 fps. Cached fonts and layered shading avoid rebuilding expensive full-screen gradients every frame. The emergency clear hotkey and dock button immediately remove the battle, projectiles, effects, audio, and queued work. Audio buffers are reset, unprepared, and closed during normal cleanup and plugin shutdown.
