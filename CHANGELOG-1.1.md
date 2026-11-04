# MXB FPV v1.1

This update focuses on flying together, keeping riders visible away from your
parked bike, and using one track map.

- Rider update priority follows the drone while flying. The stock server gets
  the drone's observer position separately from your bike's vehicle state, so
  the bike stays parked. Returning to the bike restores the normal observer.
- FPV playback accepts gaps of up to two seconds between rider snapshots instead
  of hiding riders at the stock 0.3-second cutoff. Normal riding keeps its stock
  limit. This helps sparse updates; a completely disconnected player still expires.
- Drone sharing recovers from missed or reset handshakes, retries discovery sooner,
  and accepts current flight state during discovery. The existing wire format is
  retained. Bodies, sound and map icons all use the same remote state.
- Remote drones interpolate position and rotation; rider position smoothing applies
  only to the FPV view. Both sliders are in **Multiplayer**, and 0 ms disables them.
- With the supported MaxHUD build, FPV draws drone icons over its existing map using
  its actual scale, rotation, range and viewport. It no longer needs three rider
  labels to align the icons and does not add a second track map. The standalone map
  remains available without MaxHUD or with the integration turned off.
- Updates show **Running plugin**, **Latest in selected channel**, **Up to date**,
  **Update available**, or **Ready to install**. Downloading is explicitly separate
  from installing. A version change refreshes the old six-hour cache, and the
  current version is no longer offered as an update download.
- Windows file properties now identify the plugin and installer version. Repeating
  the same installation preserves the useful previous-version rollback.

Close MX Bikes before installing. Both pilots should update to v1.1. Your saved
radio mapping and aircraft settings stay in place. Nothing installs while you fly.

Built successfully with Visual Studio 2022. The offline two-client sharing check
passed with dropped handshakes, spectator IDs, a view reset and flight exit; motion
interpolation and prediction bounds also passed. Native adapters were checked against
the saved beta21e game snapshot and the installed MaxHUD artifact, without attaching
to or launching the running game. This release has not been gameplay-tested by the
coding agent. Server lag, complete packet loss and native animation jitter cannot
be eliminated by visual interpolation.
