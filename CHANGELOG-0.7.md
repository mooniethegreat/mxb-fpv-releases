# MXB FPV 0.7

**Multiplayer is supported and tested on public servers.** The user confirmed
that 0.6 works perfectly. This update builds on that working connection.

The user reviewed this build and approved it for publication. 0.7 is available
through GitHub Releases and the in-game tester update channel. No game was
launched or mod installed by the coding agent.

## Changes

- **Fly FPV from the pits:** a button beside Select Gate on Event Info, with a
  bottom-bar entry on other pit tabs. F8 also works from the pits. It opens the
  live race scene as a drone spectator without entering the bike into the race.
  F8 or Escape returns to the pits; R resets near the track start.
- **Map while riding or flying:** numbered rider markers and separate drone
  icons. Your drone is green, other pilots' drones orange. M toggles the FPV map.
- **Preserve existing HUD maps:** MaxHUD map/standings stay available. When its
  rider labels establish a reliable projection, drones are drawn on that map.
  Otherwise the current-track map supplies the rider/drone reference.
- **Centered flight display:** roll/pitch/yaw/throttle sit at bottom center;
  compact drone speed sits close to the right, within the middle third.
- **Accurate support wording:** README, settings and installer now describe
  multiplayer as tested on public servers.

Use `MXB-FPV-0.7-Setup.exe` with MX Bikes closed. Saved radio mapping is
preserved. Setup keeps the previous plugin and provides Roll back. Source and
SHA-256 checksums accompany the release package.

New map options are in **Settings → FPV Drone → Audio & display → Map**.
Implementation details and evidence: [spectator and map notes](mod/SPECTATOR-MAP.md).
