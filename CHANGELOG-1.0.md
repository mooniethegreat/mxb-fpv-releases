# MXB FPV v1.0

**The first stable release. Multiplayer is tested and working on public servers.**

Fly an FPV drone in MX Bikes with a USB FPV radio or gamepad and chase your friends around the track.

## Included

- **Fly from the bike or the pits.** F8 spawns beside your bike. During a race, click Fly FPV on the pit screen to enter drone spectator mode without putting your bike on track. F8 or Escape returns; R resets the drone.
- **Rider and drone map.** Numbered riders, your drone in green, other pilots' drones in orange. M toggles the FPV map while riding or flying. MaxHUD's map and standings remain available.
- **Centered flight display.** Mapped roll, pitch, yaw and throttle at bottom center, with drone speed close to their right in your selected game units.
- **In-game configuration.** Live receiver mapping, DJI FPV radio defaults, five rate models, throttle curves, optional 3D mode and tunable aircraft presets.
- **Multiplayer drones and audio.** Automatic sharing through the existing game connection, spatial motor sound, a drone-position listener and goggles/controller visuals. No separate relay or server modification is required. Each viewer needs the mod.
- **Physics.** Gravity, motor response, terrain contact, bounce and drone crashes against approximate rider/bike collision volumes.
- **Verified updates.** Stable updates by default, optional downloads, preserved radio mappings and installer rollback.

Version 1.0 keeps the reviewed 0.7 flight, physics, map and multiplayer behavior. This release updates version labels, stable update defaults and packaging. It is a regular release, not a prerelease.

## Install or update

[Download the v1.0 installer](https://github.com/mooniethegreat/mxb-fpv-releases/releases/download/v1.0/MXB-FPV-1.0-Setup.exe), close MX Bikes, select the game folder and click Install. Existing radio settings are preserved.

Existing users can open **Settings → FPV Drone → Updates → Check for updates**. Version 1.0 is offered on both existing update channels; enabling early releases is unnecessary. Close the game before running the downloaded installer.

Supported: Windows x64, MX Bikes **beta 21e / Steam build 23566986**. The exact executable is checked before installation and use.

Downloads include the installer, `MXB-FPV-1.0.zip` with loose plugin files, source, SHA-256 checksums and notices. Scenery meshes such as fences and overhead bridges remain outside the collision backend. Drone contacts do not force another player's bike to fall.

Kino audio remains credited to trbflxr and the Kino team. See the [README](README.md) and packaged third-party notices for controls, settings and credits.
