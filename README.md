# Cyberpunk VR

## NOTE: this mod is **EXPERIMENTAL!** Backup your saves before playing as using it can cause your character to get stuck (which will persist after saving and reloading).
---

**Cyberpunk VR** is a fork of [Cyber Engine Tweaks](https://github.com/maximegmd/CyberEngineTweaks) with added VR support. It's more of a personal proof of concept and is not actively maintained. That being said, there are optimizations and additional features that *might* come in the future.

Current version works with Cyberpunk 2077 version 1.62.

## Installing the mod
- Download the current [release](https://github.com/toreskovic/CyberpunkVR/releases/download/v0.1.0/CyberpunkVR-v0.1.0.zip)
- Extract the .zip in your Cyberpunk 2077 folder
- Add a custom resolution (e.g. via the nvidia control panel)
    - The resolution must match your headset's aspect ratio for one eye (usually 1:1)
- Start the game
- Change the game to windowed mode with your custom resolution
- Enjoy

## Known Issues
- On low framerates, the player can get stuck or invisible collisions may appear out of nowhere. If this happens, lower your graphics settings and / or reload your last save. DO NOT SAVE THE GAME WHILE IN THIS STATE.

## TODO
- [ ] Configurable UI scaling
- [ ] Submitting both eye textures every frame (should increase perceived FPS)
- [ ] Built-in support for providing custom resolutions to the game
- [ ] Properly hiding the head mesh when driving
- [ ] VR Controller support (emulating a standard controller)
- [ ] QoL motion controls
- [ ] Variable Rate Shading

## Removing the mod
- Delete `CyberpunkVR.archive` from `Cyberpunk 2077\archive\pc\mod`
- Delete `openvr_api.dll` from `Cyberpunk 2077\bin\x64\plugins`
- Delete `cyber_engine_tweaks.asi` from `Cyberpunk 2077\bin\x64\plugins` or replace it with the original `cyber_engine_tweaks.asi`
- Delete `Cyberpunk 2077\bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVR`

## Contributing

As this repo is not actively maintained, tackling issues and PRs will be sporadic or might not happen at all.