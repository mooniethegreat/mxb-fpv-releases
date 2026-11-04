# MXB FPV v1.1 implementation

Multiplayer was confirmed by the user on public servers. Version 1.1 addresses the
subsequent rider visibility/drone sharing regression, adds view smoothing, uses
MaxHUD's current map projection and clarifies update state. See
[v1.1 technical evidence and limits](V1.1-NETWORK-VIEW.md). No game was launched or
attached to, and this release has not been gameplay-tested by the coding agent.

SDK: data version 8, interface 9. Pinned executable SHA256:
`9e792d7aed1dc40df11eea51d1a6d0519daec7be2b65609905df8c9de804a291`.
Addresses below are RVAs, rebased from the executable module. Hook entry bytes
are checked before enabling them; unsupported builds leave FPV disabled.

| Interface | RVA | Purpose |
|---|---:|---|
| Camera setter / FOV | 255D60 / 255D30 | FPV view |
| Riding camera builder | B7E0 | Complete rider representation |
| Rider graphics update | 523E0 | Goggles / controller / wrist IK |
| Native audio mixer | 289C90 | Listener and source-distance priority |
| Audio listener block | 399608 | Position, velocity, forward, up; 12 floats |
| Bike physics step | 1ADA40 | Parking controls for local bike only |
| Native bike resolver | 1BF1A0 | Resolve own vehicle engine handle |
| Native dashboard | 11FA0 | FPV-only stock instrument replacement |
| Native text renderer | 209E30 | Gear glyph interception in compatibility mode |
| Profile units | E557D4 | 0 metric; 1 US, 2 UK use mph |
| Vehicle records | F4EE20 | 50 records, stride 5B24 |
| Native snapshot sender | 2A3350 | Serialize a drone-centered observer, retain bike state |
| Native observer | 5A2004 | Entity ID plus six position/velocity floats |
| Sparse playback comparison | 2AA8CB | Owned limit: 2 s FPV, 0.3 s riding |
| Own vehicle index | E62710 | Record index + 1 |
| Render physics | vehicle + 5054 | World matrix at +4; head matrix at +544 |
| Terrain sampler / slots | 1F1720 / 568488 | Native heightfield, slot stride 6160 |
| Engine bus | 566C48 | SDK/internal dispatch and existing wrist IK |
| Settings page layer / stack / table | 4CF9A8 / E55CA4 / 3954F0 | Settings-only UI |

`RunTelemetry` runs flight and terrain/traffic queries on the simulation callback
thread. All aircraft contact queries use bounded sweeps. Native rider/bike world
transforms supply conservative capsules, with bounded finite-difference velocity.
Collision normal impulses act on the drone relative to moving targets. An impact
cuts thrust and adds tumbling; no remote bike impulse is injected.

The local bike resolver is compared to each bike physics step's object. While
FPV is active, the source controls at +270 (steer), +278 (throttle), +280/+288
(brakes), +290 (clutch), and +298/+29C (gear requests) are overridden for that step.
The source block is restored after the step. Physical controls are consumed by
the game's normal simulator; no telemetry output is mistaken for physics input.

The mixer hook supplies the drone listener before native voice priority/culling,
then lets the native mixer set OpenAL position, velocity and orientation. It uses
the same coordinate conversion as the game (negative AL z). Six shared buffers
and bounded per-pilot voices use the current game AL context on the mixer thread.
The loop resources are embedded in the DLO. Submit and Process exchange snapshots
under a mutex; source cleanup is limited to this mod's own IDs/context. Sound
stales after 1.5 s, and only eight nearby remote voices are allocated.

Native dashboard mode works from a local render-data copy. Default FPV mode
replaces bike instruments and MaxHUD's public Draw output during FPV, displaying
the LCD drone icon/speed and actual mapped controls. Other mods are not modified.
MaxHUD's callbacks still run; only its returned drawing counts are zeroed during
FPV. Its settings remain untouched. The native profile-unit global is read live.

`settings_ui.inl` separates Receiver, Rates, Aircraft, Audio/display, Multiplayer,
Rider and Updates pages. `flight.h` has independently expressed mathematical responses
for Betaflight/Raceflight/KISS/Actual/Quick; actual curve plots call the exact
flight rate function. Three-dimensional thrust is signed around neutral, with
configurable deadband and throttle shaping. Axis mapping remains in Controller.

Automatic sync uses the native typed peer-message channel, not Steam or an
extra UDP socket. Native sender RVA `2A3920` (`bus 359`) accepts subtype,
payload pointer, byte length and destination connection ID. It creates opcode
`1F`. The stock server's `29F1E0` handler forwards nonzero-destination messages
as opcode `20`, carrying the authenticated sender, unchanged subtype and bytes.
The destination client checks sender membership before delivering event 10.
The stock game's receiver `5BB10` ignores nonzero unknown peer subtypes.

We reserve subtype `46505650` with a versioned 108-byte FPVN envelope. A hello /
ack capability exchange precedes state streaming. Unknown clients receive only
backed-off discovery (2 s, 8 s, 30 s, then 60 s). Confirmed peers receive 20 Hz
flight updates or 2 Hz idle updates, with an immediate FPV-state transition.
Connection membership, envelope/header/payload bounds, finite values, session
nonce, sequence and stale-data expiry are checked. There are at most 50 native
roster entries; server source identity is used rather than a payload source ID.

Inline hook `120890` wraps the native client event dispatcher, preserving other
event callbacks. Event `13` runs inside native update `2A61D0`, after receive
processing. All sends occur there, on the game's network thread. The render
hook only submits an own-state snapshot under a mutex. Event 10 consumes our
subtype before the stock message ring. Events `0D` and `12` reset discovery.
The existing bike-switch callback wrapper can still delegate to this function.
Hook and sender prologues are checked in addition to executable SHA256.

Native client state RVA `598904` must equal 7. Logical local connection ID is
at `598908` (also exposed by bus `34F`), not the earlier transport handshake ID
at `5986CC`. Native peer records begin at `599044`, 50 entries of stride `2E0`,
with state at +0 and logical ID at +4. Only ready peers other than self are used.
The server may be entirely unmodified. Every viewing client needs the renderer,
audio code and native receiver; host-only installation cannot supply those.

Default `Multiplayer.Transport=0`, `Automatic=1` migrates existing users without
requiring their old UDP Enabled flag. Transport 1 retains the manual UDP relay
as a fallback and keeps its v2 wire packet compatible with 0.5. Automatic sync
does not use the manual room or port. See MULTIPLAYER-NATIVE.md for evidence.

Static evidence: research/mxbikes-local/audio-hud-parking-evidence.json and its
analysis snapshot. A candidate 0x50 matrix setter was investigated and rejected:
its downstream consumer is graphical, not the audio listener. No hook was added
there. Audio was traced to AL listener imports and native mixer state instead.
Sound provenance: assets/audio/PROVENANCE.txt. No game, Unity, Rewired or FMOD
executable is in the deliverables. Generated procedural accessories remain native.

Native sync evidence: research/mxbikes-local/native-sync-evidence.json.

0.7 spectator/map architecture and static evidence: SPECTATOR-MAP.md. The pit
entry uses the existing live race-view mode, not To Track/Join. Spectator flight
has its own render clock and main-camera selection. Riding flight keeps the
telemetry clock; both call the same fixed-step simulation. The SDK map callbacks
provide track geometry and rider identities. MaxHUD output is filtered by widget
bounds instead of blanking all drawing. The map and standings remain available.
