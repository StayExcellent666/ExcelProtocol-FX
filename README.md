# ExcelProtocol FX

ExcelProtocol FX is a native Windows x64 plugin for OBS Studio 32.2.2 through 32.x. It renders the same seeded animation frame in a click-through desktop overlay and in an `ExcelProtocol FX` OBS source, so stream viewers and the streamer see synchronized effects.

The plugin includes ten procedural effects, a native Twitch Raid Battle, weighted random and constrained chaos modes, duration/cooldown/intensity limits, a bounded command API, a dock, monitor selection, persistent settings, secure ExcelProtocol Channel Reward triggers, and clear-all hotkeys. It does not inject into games, manipulate input, modify a game renderer, or claim support over exclusive-fullscreen games. Use borderless or windowed display mode when the desktop overlay must remain visible.

## Install

Download the Windows x64 setup executable from the matching GitHub Release and close OBS before running it. Setup locates common OBS installations, validates the selected x64 OBS version, backs up existing plugin-owned files, and refuses installation while any OBS process is open. The portable ZIP has the standard OBS layout and can be extracted into the OBS installation root while OBS is closed.

After installation, open **Docks > ExcelProtocol FX**. Add the `ExcelProtocol FX` source above gameplay in each scene that should show stream effects. The desktop overlay and stream source can be enabled independently. `Ctrl+Shift+F12` clears every effect globally; an OBS hotkey with the same purpose is also available.

To use Channel Reward effects, create a one-time code from **Dashboard > Channel Rewards > ExcelProtocol FX**, then paste it into the plugin's **Connection** tab. The plugin stores its device credential in Windows Credential Manager and makes an outbound authenticated TLS connection; it opens no inbound port. Live effect commands are acknowledged, expire after ten seconds, and are never replayed after a reconnect.

The **Raid** tab includes a local **TEST RAID** button, viewer-count simulation, a master sound-volume control, and the rules shown to chat. An original synthwave battle track loops during combat and ducks beneath distinct low-register Blaster, Plasma Rifle, Railgun, and Ban Hammer sound families. Test raids guarantee moderator Ban Hammers and VIP Railguns, even with a small simulated group. The bounded mixer limits competing cues, gives priority to stronger weapons and result stingers, and keeps headroom when sounds overlap. Audio is emitted by the **ExcelProtocol FX** source into OBS, where its level, mute state, and monitoring are controlled through the normal Audio Mixer; it does not open a Windows desktop playback device. Every viewer is represented individually through 48 viewers; larger raids use representative field units. Enemies enter in waves, take visible hits, break apart on death, and are replaced while the raid focuses the marked target. Production raid and chat events arrive over the paired outbound WSS connection; see [the Raid Battle protocol](docs/raid-battle-protocol.md).

## Updates

Automatic update checks are enabled initially. Automatic installation is opt-in. Verified installers are staged while OBS is open and applied by a small helper only after the exact launching OBS process and all other OBS processes have exited. Downloads require HTTPS, exact official-repository identity, compatible OBS bounds, matching GitHub and manifest SHA-256 values, and fixed asset names. See [updater security and behavior](docs/updater.md).

Stable and beta channels are separate. Beta versions use `vMAJOR.MINOR.PATCH-beta.NUMBER`. The plugin never updates OBS itself.

## Build and test

The project is based on the official `obsproject/obs-plugintemplate` CMake foundation. On an x64 Visual Studio 2022 developer shell:

```powershell
./scripts/prepare-deps.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --test-dir build_x64 -C Release --output-on-failure
cmake --install build_x64 --prefix stage --config Release
```

Dependencies are pinned to OBS 32.2.2 and Qt 6.11.1 and verified by SHA-256. `scripts/test-obs-load.ps1` loads the compiled DLL through libobs and confirms its source registration without touching the user's OBS profile.

## Publish a version

1. Update `VERSION.txt`, source version metadata, documentation, and the release QA record.
2. Run native unit tests, OBS module-load smoke, installer clean-install/upgrade/refusal/uninstall tests, and the manual desktop/stream/safety/performance checklist on the exact commit.
3. Fill `docs/qa/v<version>.json` with the tester, tested commit, timestamps, evidence, and passing results. Stable publication rejects missing, stale, or unapproved evidence.
4. Commit and push. Confirm the branch CI is green.
5. Create and push the exact annotated tag, such as `v1.0.0` or `v1.0.0-beta.1`.

The release workflow uses the repository-scoped `GITHUB_TOKEN` with `contents: write`; no hardcoded secret is required. It rebuilds and tests Windows x64, produces the installer, portable ZIP, checksums, manifest, and release notes, uploads them to a draft, then publishes only after every preceding job succeeds. Stable tags additionally require the checked-in QA attestation tied to the tagged commit.

## Integration status

Version 1.1.0 pairs with the ExcelProtocol dashboard over HTTPS and connects to the production bot using an outbound authenticated TLS WebSocket. Remote commands dispatch into the same bounded effect and Raid Battle manager used by local previews. The plugin advertises command, effect-library, status, and `raid_battle.v1` capabilities, answers correlated requests, sends heartbeats, uses progressive full-jitter reconnects, rejects expired or duplicate mutations, and never queues live commands across a disconnect.

Licensed under GPL-2.0-or-later; see [LICENSE](LICENSE).
