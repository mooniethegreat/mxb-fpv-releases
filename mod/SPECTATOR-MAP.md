# 0.7 spectator flight and map

Status: reviewed by the user and approved for release. The user confirmed
multiplayer works on public servers. The coding agent traced the new spectator
and map interfaces statically and compiled the implementation without launching
the game. User approval authorized the 0.7 GitHub release and update feed.

## Pit screen and live scene

The existing pit handler's `ID_REPLAY` action calls the native page push with
`chat/replay`, mode 3. The replay initializer explicitly selects the current live
state for mode 3. This works independently of To Track/Join and uses the existing
race connection. The adapter calls this same path after checking executable
identity and push/pop/replay-initializer prologues.

| Native interface | RVA |
|---|---:|
| Page push | EC330 |
| Page pop / return | EC640 |
| Live/replay initializer | C30F0 |
| Live/replay camera ID | 4C9190 |
| Live/replay entry mode | 4C9194 |
| Native live flag (observed, not written by adapter) | E56668 |
| Pit tab selection | 4C9160 |
| Page stack layer / entries | 4CF9A8 / E55CA4 |

`NativeView` checks the current stack for pit + multiplayer pit. It only owns a
return transition after a successful mode-3 live-view entry. F8/Escape, focus
loss or radio disconnect returns through the stock page-pop path. A changed
native page invalidates spectator flight; replay-file cameras are never selected.
The replay tools are hidden while the dedicated FPV view is active.

The custom Fly FPV control uses the normalized gate-row positions observed in
the installed `ui/multiclient.mnu`: ordinary Event Info has Select Gate at
`0.1375, 0.533333 .. 0.2625, 0.566667`; the alternate layout uses y `0.5`.
Fly FPV sits to its right. Other pit tabs use the unused bottom action-bar slot.
The game archive is read for analysis only; no UI/game files are repackaged.

Riding flight still uses `RunTelemetry`. Spectator flight uses elapsed render
time with the same bounded 250 Hz fixed-step solver, terrain and traffic contact
code. The clocks are mutually exclusive. Native telemetry can stop without
freezing spectator flight. The main camera and audio listener select the drone.
An SDK centerline/rider anchor, or verified loaded terrain anchor, provides the
spawn; failure returns to the pits with a short status message.

Cosmetic sync retains the existing version-2 wire packet. A spectator without an
on-track rider uses `race = 900000 + native connection ID`, a reserved range
accepted by 0.6's bounds. This does not change any game rider/connection identity.
Other clients can render and hear the drone without attaching goggles to an
unrelated rider. Existing automatic and manual transport formats remain intact.

## Rider and drone map

`TrackCenterline` consumes the official SDK's 28-byte segment layout. Segment
endpoint comparisons establish coordinate handedness before sampling curves.
`RaceTrackPosition` consumes the official 28-byte rider position layout, with
stride/count/finite-value bounds and stale-entry removal. Player numbers label
the rider markers; FPV packets supply independent drone positions and identities.

M toggles the FPV map in riding and spectator/drone modes. Own drone markers are
green, remote drones orange, riders blue and the own rider yellow. Off-map drone
markers clamp to the map edge so pilots can recover their reference position.

MaxHUD's output is no longer entirely zeroed. Its configured input/joypad/speed/
lean widget regions are filtered during FPV, while its map and standings remain.
Configuration is read only. Background quads reveal actual widget bounds when
available; configured regions provide the fallback when backgrounds are disabled.

Three non-collinear rider-number labels on an existing MaxHUD map can establish
the world-to-map transform, including rotation/zoom. The fit checks residuals and
rejects degenerate/ambiguous observations. Drone markers then share that map's
projection. If it cannot be established, a centerline map with rider/drone markers
uses the same footprint, or the upper right when MaxHUD is absent. Other HUD
plugins remain unmodified and can continue rendering their own maps.

The flight input group is centered at the bottom. The compact speed block sits
close to its right. Geometry scales with both width and height; on 16:9 it stays
within the middle third. Both modes use the same mapped control values.

## Static evidence

Target snapshot SHA256:
`a9607eb02ee9fc1614d1ed5e6c7bcdc112eb85ddf99ba8e4bb52975eb40ba74a`.
Original supported executable SHA256:
`9e792d7aed1dc40df11eea51d1a6d0519daec7be2b65609905df8c9de804a291`.
REA/Ghidra 12.1.4 observations, saved outside the distributable source:
`research/mxbikes-local/spectator-map-evidence.json` and its analysis snapshot.

- Pit action: `ev_988da1e6bf342c7bdbb8dd06482805d650d2fcd3776e07504db2ff2e2963304a`.
- Replay initializer/live mode: `ev_40cece0dba3c6c1bcda509f41357cd7a707b40a8b6215465b82ffe6eb3123029`.
- Live camera builder: `ev_a2fbd67299e80a4d2f27f07bf1e7f98f979635f6f53eb28cd6ecebdf23b14956`.
- Page transition: `ev_376be1eccf217194b4c87d73df05e82df4bfc19154d56bb126f6f81c5a29b7dd`.
- Page return: `ev_baf66ff7e721f1912df38b4883ba6079610df46620a95f6c22e81ae887ca314e`.
- Push signature: `ev_abb675179121a980cc351b3bac185f7458228efae69c81ebcbbca30028c57f71`.
- Pop signature: `ev_4de62ccd0f325e0033217454206b7f12a418c18c7733428a9e25e42f53eefdca`.

These are static observations, not a claim that the coding agent executed the
new paths. UI placement, unusual HUD layouts, map-label fits and live scene
lifecycle behavior remain useful regression cases for user testing.
