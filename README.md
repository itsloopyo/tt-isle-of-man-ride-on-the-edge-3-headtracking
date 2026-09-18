# TT Isle of Man: Ride on the Edge 3 Head Tracking

![TT Isle of Man: Ride on the Edge 3 running with this mod](https://raw.githubusercontent.com/itsloopyo/tt-isle-of-man-ride-on-the-edge-3-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for TT Isle of Man: Ride on the Edge 3 that moves the camera with your head while your keyboard or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **6DOF tracking** - rotation (yaw, pitch, roll) and positional lean (x, y, z)
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [TT Isle of Man: Ride on the Edge 3](https://store.steampowered.com/app/1924170/) on Steam. The mod only runs on game builds it recognizes, and the Steam build is the one it has been set up for; on any other build the mod stays inactive and the game runs normally.
- A head tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam, a VR headset, or a phone app that sends OpenTrack UDP
- Windows 10 or 11, 64-bit

## Installation

### Standalone Installer

1. Download the latest `TT3HeadTracking-v<version>-installer.zip` from [Releases](https://github.com/itsloopyo/tt-isle-of-man-ride-on-the-edge-3-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242` (see [Setting Up OpenTrack](#setting-up-opentrack)).
5. Launch the game.

If the installer can't find your game, either set an environment variable pointing at the game folder:

```powershell
setx TT_ISLE_OF_MAN_3_PATH "D:\SteamLibrary\steamapps\common\TT3"
```

or pass the path directly:

```powershell
install.cmd "D:\SteamLibrary\steamapps\common\TT3"
```

The game folder is the one that contains `TT3.exe`. The installer writes to one copy of the game per run, so if you have more than one install, run it again with the path of the other.

### Manual Installation

The installer ZIP contains everything needed. Copy these into the game folder, next to `TT3.exe`:

1. `vendor/ultimate-asi-loader/dinput8.dll`, renamed to `version.dll` (this is [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)). Back up any existing `version.dll` first.
2. `plugins/TT3HeadTracking.asi`

Mod managers do not deploy this mod. Both files have to sit in the game root beside `TT3.exe`, and a mod manager installs into a subfolder the loader never looks in.

`HeadTracking.ini` is written to the game folder the first time the game runs with the mod.

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack).
2. Pick an **Input** for your tracker (see below).
3. Set **Output** to **UDP over network**.
4. Click the hammer icon next to Output and set the IP to `127.0.0.1` and the port to `4242`.
5. Click **Start**, then launch the game.

Centering is done in the tracker: use OpenTrack's **Center** hotkey, the CENTER button in your phone app, or SteamVR's seated position reset.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. In OpenTrack, set **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1:4242`.

### Webcam Setup

Set OpenTrack's **Input** to **neuralnet tracker**. It tracks your face from any webcam and needs no markers and no IR hardware.

### Phone App Setup

The mod accepts one thing: OpenTrack UDP on port `4242`. A phone app works here if it sends that protocol itself, or ships a PC companion that does. Check your app against that.

For an app that does send it, the wiring depends on how much filtering the app does on the phone before the data leaves it:

- **App filters on-device:** point it straight at your PC's LAN IP address on port `4242`.
- **Raw or lightly filtered feed:** send it to OpenTrack instead (Input: **UDP over network**), and let OpenTrack's filters and curves clean it up before it reaches the game. Also use this route if you want OpenTrack's curve mapping.

To tell which you have, try sending direct, then hold your head still. If the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works the same way.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker running on this same PC if it sends to the PC's LAN address instead of `127.0.0.1`: the mod decides by the address the data arrives from, not by which machine sent it.

## Controls

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle head tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode (rotation and position / rotation only / position only) | `Page Up` | `Ctrl+Shift+G` |

The two columns do the same thing. Use whichever your keyboard has. Both are remappable in `HeadTracking.ini`.

Head tracking is active while riding in free roam and in races. It is paused in menus, the pause screen and the map.

## Configuration

Settings live in `HeadTracking.ini` in the game folder, next to `TT3.exe`. It is created with these defaults the first time the game runs with the mod. Restart the game after editing it.

```ini
[Network]
; UDP port the mod listens on for OpenTrack data (1024-65535).
UdpPort=4242

[General]
; Start with head tracking on (1) or off (0).
EnableOnStartup=1

[Hotkeys]
; Windows virtual key codes, in hex. Each action has a nav-cluster key and a
; Ctrl+Shift+<key> chord, and both fire it.
; Common codes: End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21, PgDn 0x22,
; F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.
ToggleKey=0x23
CycleModeKey=0x21
ChordToggleKey=0x59
ChordCycleModeKey=0x47

[Rotation]
; Smoothing for rotation and position. 0.0 none .. 1.0 heavy.
; LocalSmoothing applies to a tracker sending to 127.0.0.1 on this PC,
; RemoteSmoothing to one sending over the network (a phone on WiFi).
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
; Positional (lean) tracking on (1) or off (0).
Enabled=1
; How far the camera may lean from where the game put it, in meters (0 to 0.50).
LimitX=0.30
LimitY=0.20
; Forward lean limit.
LimitZ=0.40
; Backward lean limit.
LimitZBack=0.10
```

Put comments on their own line, not after a value: an on/off setting with a `;` comment after it is not read, and the log says so.

Field of view is set in the game under Game Settings > Camera. When the game widens the view, the mod scales head movement so it moves the picture as far as it does at that view's normal field of view.

## Troubleshooting

**Mod not loading**
- Check that `version.dll` and `TT3HeadTracking.asi` are both in the game folder next to `TT3.exe`.
- Look for `TT3HeadTracking.log` in the game folder. If it is missing, the loader did not run: re-run `install.cmd`.
- If the log says no usable build profile was found, your game build is one the mod does not recognize yet. The game runs normally without head tracking. Check [Releases](https://github.com/itsloopyo/tt-isle-of-man-ride-on-the-edge-3-headtracking/releases) for an update.

**No tracking response**
- Make sure OpenTrack (or your phone app) is running and sending to port `4242`, and that `UdpPort` in `HeadTracking.ini` matches.
- Press `End` to make sure tracking is not toggled off.
- Tracking only runs while riding. It is paused in menus, the pause screen and the map.
- Check `TT3HeadTracking.log` for a `[udp]` line saying the UDP port failed to bind.

**Jittery or unstable tracking**
- Raise `RemoteSmoothing` for a phone or other network tracker, or `LocalSmoothing` for a tracker sending to `127.0.0.1`.
- Route a phone app through OpenTrack and use its filter (see [Phone App Setup](#phone-app-setup)).
- Improve lighting for webcam tracking.

**Head movement goes the wrong way or feels too strong**
- Axis inversion, sensitivity and response curves are set in OpenTrack or your phone app, not in the mod.
- Recenter in your tracker while looking straight at the screen.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader (`version.dll`) is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Prerequisites: Visual Studio 2022 or newer with the C++ desktop workload, CMake, [pixi](https://pixi.sh) and git.

```bash
git clone --recursive https://github.com/itsloopyo/tt-isle-of-man-ride-on-the-edge-3-headtracking.git
cd tt-isle-of-man-ride-on-the-edge-3-headtracking
pixi run build      # build TT3HeadTracking.asi
pixi run install    # build and deploy to every detected install of the game
pixi run package    # build the installer ZIP into release/
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- RaceWard Studio and Nacon - TT Isle of Man: Ride on the Edge 3
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG - loads the mod into the game
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu - function hooking
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software and the UDP protocol the mod accepts

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by RaceWard Studio or Nacon. Use at your own risk.
