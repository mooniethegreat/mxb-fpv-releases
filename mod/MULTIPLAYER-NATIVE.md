# Automatic drone sharing: findings and implementation

Target: MX Bikes Steam 655500, build 23566986 / beta21e.
Original executable SHA256:
`9e792d7aed1dc40df11eea51d1a6d0519daec7be2b65609905df8c9de804a291`.
Read-only runtime snapshot SHA256:
`a9607eb02ee9fc1614d1ed5e6c7bcdc112eb85ddf99ba8e4bb52975eb40ba74a`.
All analysis used this existing snapshot. No game or server was launched.

## Answer

The separate relay is not a requirement of MX Bikes. Its shipped network code
already forwards typed, bounded peer messages over the game-server connection.
Version 0.6 implements automatic FPV discovery and state sharing on that channel.
The game server does not need an FPV plugin. Each client displaying drones still
needs FPV code/assets, so installing only on the host cannot make stock viewers
render or hear a custom drone.

This is a static finding and a compiled implementation. Successful two-client
runtime operation remains unverified and belongs to the user's testers.

## Observed native path

1. Engine command `359` calls native RVA `2A3920` with message subtype, pointer,
   byte count, and destination logical connection ID. The sender writes opcode
   `1F`, destination, subtype, length, and bytes into the existing reliable queue.
2. Server handler RVA `29F1E0`, case `1F`, reads the bounded payload (max `44C`
   bytes). Destination 0 invokes server game code; a nonzero destination instead
   creates opcode `20` with the authenticated sender ID, subtype, length, bytes.
   It calls RVA `298C60`, which finds that ready target and queues the message.
   This branch does not require custom server application handling for our type.
3. Client handler RVA `2A4AC0`, case `20`, verifies the sender is an active native
   peer and raises native event 10 with source/type/length/payload.
4. Native event adapter RVA `120890` ordinarily copies that into its bounded
   message ring and queues a game event. Stock peer receiver RVA `5BB10` checks
   the peer type and returns for nonzero types it does not handle. FPV's reserved
   type is nonzero and has no chat rendering path.
5. The mod intercepts only reserved type `46505650` before the stock message
   ring. Other events and types retain their normal paths.

Evidence IDs and procedure addresses are indexed in
`research/mxbikes-local/native-sync-evidence.json` in the development workspace.
Tool-generated pseudocode is analysis output, not original game source.

| Observation | REA Evidence ID |
|---|---|
| Sender `2A3920` | `ev_fd0a3e7f50fcbf68480fd56de7d1b6515151b72c518aee88ca4ece368d359df0` |
| Server forwarding `29F1E0` | `ev_bab728b8dc23ce2e338bc1435ff4ebb0b48dc71a9d477d3591daf559ed2aaca1` |
| Recipient lookup `298C60` | `ev_d4e04b07907c460d4e76e4d55e0e4c1de594cd01ce1465bc4d24c67a1cfe1c09` |
| Client receive `2A4AC0` | `ev_295baa1e5920cda707d874367e2a63203087dd53e8dd74895aee73ba136c83e3` |
| Event dispatch `120890` | `ev_fac33f7e3d52f4648749ca4f3a22e10e254fde51778e633f849e1cbaa7bb4f75` |
| Network update `2A61D0` | `ev_a8babe79f9514bf99b9f832af3a6ce999876cf348dd4a0a4297404f1e4e95fc2` |
| Stock unknown-type ignore `5BB10` | `ev_ca9c1b17633b4a5dca32235ad0575639db570060c87a34bcc66cec49f88b579e` |

## Threading and discovery

Sends run from event `13` inside native client update RVA `2A61D0`. RenderFrame
only submits a flight-state snapshot, so there are no render-thread calls into
the game's native reliable queue. The dispatcher hook preserves unrelated
callbacks, including the separate bike-switch mod's delegation.

Client state `598904` must be 7. Logical local ID `598908` is corroborated by the
getter exposed as command `34F`. The native roster at `599044` has 50 records,
stride `2E0`; state and logical ID are at +0/+4. This is distinct from the
transport handshake identifier at `5986CC`.

Ready peers receive a capability hello. Full drone streams begin only after a
matching FPV handshake. A stock client therefore receives occasional bounded
discovery, never the continuous flight stream. Unknown-client probing backs off
to one message per minute. State is 20 Hz while flying and 2 Hz while idle.
Nonce, sequence, room-independent native protocol tag, sender roster membership,
finite payload values, byte limits and expiry are checked. Native session/world
reset events clear state. Stale sound and visuals are removed.

Automatic mode is enabled by default on upgrades and needs no port, IP or room.
Manual UDP is retained for a custom server that filters the native channel.

## Kino comparison

In the supplied Kino.Core artifact, the FPV sync class registers receivers for
`Sync.Type.DroneState` and `Sync.Type.DroneData`. It constructs those packets and
calls `Kino.ModCore.Sync.SendTo`. `Sync.MakePacket` creates a Smartfox data package
with packet ID 14 and its Kino protocol identifier. `Sync.SendTo` calls the
existing CarX `SmartfoxRoomClient.Send`, broadcasting or selecting player IDs.
This explains the lack of a manually hosted relay there: it uses the game's
room transport. It is not evidence that Kino's CarX transport can run in MX Bikes.

Analysis-only references:
`research/kino-release/decompiled/FpvNetwork.readable.cs` and
`research/kino-release/decompiled/KinoSync.readable.cs`.

MX Bikes' bundled Steam DLL also exposes legacy Steam P2P networking and lobby
APIs. That was investigated as an alternative, but is not used by this build:
the existing native server forwarding path avoids adding a second matchmaking
system or dependence on Steam relay availability.

## Runtime questions for the testers

- Two 0.6 clients on the same stock server discover each other automatically.
- Each sees/hears the other's flying drone; returning to bike removes it.
- This also works when neither the dedicated server nor other riders has FPV.
- Client rejoin, map change and host migration clear old drone/pose/sound state.
- No discovery text or UI noise appears on stock clients, and 20 Hz state traffic
  coexists with normal riding/livery messages without significant queue delay.

These checks have not been run by the coding agent, as requested.
