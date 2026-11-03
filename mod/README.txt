MX Bikes FPV v1.0

Multiplayer is supported and tested on public servers. Version 1.0 is the stable
release, with pit-screen spectator flight, rider/drone map markers, a centered
flight display and in-game settings. It preserves the reviewed flight behavior.

INSTALL
Close MX Bikes. Run MXB-FPV-Setup.exe, select the game folder, click Install.
Existing plugins/mxb_fpv.ini and your DJI FPV radio mapping are preserved.
Start MX Bikes yourself. Open Settings, then FPV Drone [F9]. All configuration
is inside the game; no separate controller window is required.
F8 / your bound radio switch: enter FPV beside the bike, or return to the bike.
R: reset a crashed drone beside the bike. Escape/pause/focus loss exits FPV.
From multiplayer pits, click Fly FPV beside Select Gate on Event Info. Other tabs
show the button in the bottom action bar. F8 from the pits also starts this mode.
This enters the live race view as a drone spectator without putting your bike
on the track. F8 or Escape returns to the pits. R resets near the start of the
track; radio/gamepad controls, physics, sound and remote visibility stay active.
The local bike receives zero throttle, full front/rear brake and full clutch
while flying. It brakes to a stop through normal physics, rather than teleporting.

SETTINGS
Receiver: axis binding/inversion, live mapped values, deadband and mode switch.
DJI defaults: Roll 0, Pitch 1, Yaw 3, Throttle 2, no inversion, USB DirectInput.
Rates & throttle: independent roll/pitch/yaw curves and graph; Betaflight,
Raceflight, KISS, Actual and Quick models. Throttle mid, expo and scaled limit.
3D mode reverses thrust below center throttle; above center is positive thrust.
Center is a configurable neutral deadband. Normal mode remains the default.
Aircraft: freestyle/race 5-inch 6S, freestyle 3-inch 4S, cinewhoop 3-inch 6S,
Pavo20-inspired 2-inch 3S and DJI FPV-inspired 5.3-inch 6S. Editable mass, motor
thrust, props, cells, spool, drag, camera and collision tuning. Presets are
inspired approximations, not measured replicas. Rates remain separate from props.
Audio & display: local/remote sound volume, audible range and FPV display switches.
Multiplayer: drone sound/body/pose sharing connection. Rider: goggles/controller fit.
Updates: check GitHub, choose stable or early releases, download a verified installer.
Click Save to retain changes. Settings entry uses a flat gray/blue menu with pages.

UPDATES
Official releases: https://github.com/mooniethegreat/mxb-fpv-releases/releases
Settings > FPV Drone > Updates checks in the background, at most once per six
hours across game launches. Manual Check for updates always refreshes the result.
Version 1.0 is available in the stable channel. New installations check stable
releases by default; existing update preferences are preserved. Manual Check for
updates bypasses the cache. You can disable checks or opt into early releases.
Nothing is downloaded until you click Download verified installer. Downloads use
HTTPS, the exact published file size and SHA256, and the supported game-build hash.
Files are saved in %LOCALAPPDATA%/MXB-FPV/Downloads. Open that folder, close MX Bikes,
then run the installer. No installer is launched automatically from the game.
Your existing plugins/mxb_fpv.ini is preserved. Setup saves the previous recognized
plugin at plugins/mxb_fpv/previous/mxb_fpv.dlo; Roll back restores that binary.
An offline/error response leaves flight and installed files alone. No login needed.

AUDIO AND DISPLAY
The original Kino drone bank's six motor/air samples are embedded as mono PCM.
A native mixer crossfades layers with motor output, control activity and airspeed.
This ports the sound assets; its mixing is not an exact reproduction of the FMOD
event. There is no FMOD runtime dependency. Audio attenuation/panning is in 3D.
While flying, the native game's listener position, orientation, velocity and
distance-priority audio culling use the drone. Nearby bikes should be heard from
the drone position. Returning to the bike restores its ordinary audio perspective.
The LCD drone indicator and seven-segment speed read actual drone velocity. Unit
selection follows the current MX Bikes profile: metric, US or UK.
The FPV controls use exactly the same mapped input as flight. Roll/pitch share a
crosshair, yaw has a horizontal indicator, and throttle has one vertical bar.
Normal throttle fills upward from the bottom. 3D shows signed thrust about center.
Controls are centered at the bottom; the smaller speed display is close to their
right, inside the middle third on a normal widescreen display.
MaxHUD's bike input/joypad/speed/lean widgets are filtered during FPV. Its map,
standings and unrelated widgets are retained. Its INI is read for configured
positions, never edited. Disable filtering in Audio & display if desired.

MAP
M toggles the FPV map while riding or flying. Riders have numbered markers;
your drone is green, and other pilots' drones are orange with their numbers.
The map uses the current track centerline and the game's official rider data.
On an active MaxHUD map, three non-collinear rider labels can establish its
live projection, including rotation and range. Drone icons use that projection.
If it cannot be established, a track/rider/drone map uses the same footprint,
or the upper-right corner if MaxHUD is absent. It never projects from a guessed
world transform. Map settings and key are under Audio & display > Map.

COLLISIONS
The 250 Hz flight loop keeps gravity, motor spool, air drag and native terrain
heightfield contacts. Swept capsule volumes approximate each loaded bike and
rider, including the local bike. Moving vehicles contribute impact velocity.
A bike/rider impact cuts drone motors, kicks it away and starts a tumble, then
gravity and ground bounce/friction bring it down. Press R or re-enter FPV to reset.
This is drone-side physical response: it does not force another player's bike
to fall or alter server-authoritative rider physics. The approximations are not
pixel-exact character/vehicle meshes. Separate scenery, fences and overhead
bridge meshes remain outside the collision backend.

MULTIPLAYER DRONE VISIBILITY AND SOUND
Default connection: Automatic / current game server. Install FPV 0.6 or later on each
viewer's PC and join the same MX Bikes multiplayer session. No separate relay,
IP address, extra port, room code or modification on the game server is needed.
Upgrade migration enables this new automatic mode even if old UDP sync was off.
You can disable sharing in Settings > FPV > Multiplayer, then Save.

MX Bikes already forwards typed peer data through its normal connection.
The mod discovers current riders and exchanges a capability handshake. Only
confirmed FPV peers receive the 20 Hz drone stream. Clients without this mod
ignore the unknown discovery type and receive no continuous FPV stream.
The payload carries FPV mode, drone position/orientation/size, motor output,
velocity and control activity. Receivers draw the quad and play spatial sound.
The nearest eight remote drones can be heard at once. Stale sound stops and
peer visuals expire after four seconds. Idle clients send only slow heartbeats.

The game host does NOT need the mod unless that player wants to fly or see/hear
other drones. Host-only installation cannot add a renderer/sound code to stock
viewers. Every person who wants drone visuals or audio needs 0.6 or later on their client.
Camera chase still works alongside riders who do not install the mod.

The native forwarding path and unknown-type handling were traced in the pinned
game binary. The user subsequently tested 0.6 on public servers and confirmed
multiplayer works. The reviewed build is now published as the stable v1.0 release.
Server content checks, bike replication and race results are unchanged.

Settings > FPV > Multiplayer shows discovered FPV peers versus connected riders.
If a custom server filters this channel, Manual UDP relay remains available:
one PC hosts port 48127; others use its reachable IPv4 address, port and room.
That fallback needs LAN/VPN/port forwarding, as in 0.5. Its version-2 payload
remains compatible with 0.5 when both sides explicitly select/use manual UDP.
Automatic sync is wire-compatible with 0.6. Use 0.7 on each viewer to get map
markers. Spectator drones use a reserved cosmetic identity rather than borrowing
another rider's identity; this does not alter the server's rider roster.

SUPPORT / UNINSTALL
Supported mxbikes.exe SHA256:
9e792d7aed1dc40df11eea51d1a6d0519daec7be2b65609905df8c9de804a291
Private hooks are build-pinned and signature checked; an updated game needs
adapter revalidation. Diagnostics: plugins/mxb_fpv.log.
Close the game, run Setup and choose Uninstall. Mappings/logs and other mods stay.
No game executable or stock game asset is replaced. Source and audio provenance
accompany this candidate. Third-party credits are in THIRD-PARTY-NOTICES.txt.
Build with VS2022 C++ Build Tools: mod/build.ps1 -SkipTests.
The separate bike-switch mod is packaged independently and unchanged here.
