# CHIM Dialogue TTS Bridge

An SKSE plugin that makes CHIM's player TTS for dialogue options work with any dialogue menu UI.

CHIM can voice the line you pick in a vanilla dialogue menu. That feature lives in CHIM's own `Interface/dialoguemenu.swf`. Any UI mod that replaces that file (Vel'dun UI, Untarnished UI, Dear Diary Dark Mode and others) removes it, and the player stays silent. This plugin does the same job from native code, so it does not matter which `dialoguemenu.swf` wins in your load order.

## Tested with

- Skyrim SE 1.6.1170, SKSE 2.2.6
- CHIM 3.4.2
- Vel'dun UI's dialogue menu

## Install

Build the plugin (see below) and put `CHIMDialogueTTSBridge.dll` in `Data/SKSE/Plugins/`. Load order does not matter. In CHIM's MCM, keep player TTS for traditional dialogue switched on.

If the dialogue menu in use already has CHIM's hook, the plugin detects that and does nothing.

## How it works

CHIM's SWF does two things when you click a topic. It sends the mod event `PlayMenuTopic` with the topic text, and it holds the game's `TopicClicked` call until CHIM invokes `_root.DialogueMenu_mc.startTopicClickedTimer`, which CHIM does once your line has finished playing. The NPC therefore waits for you.

The plugin reproduces both halves:

1. It hooks `DialogueMenu::Accept` and swaps the native `TopicClicked` callback for its own. Every dialogue menu SWF ends a click by calling that callback.
2. On a click it reads the topic text from the menu's topic list, with the game's own topic list as a fallback, and sends `PlayMenuTopic` with the same text clean-up CHIM's SWF applies.
3. It adds a native `startTopicClickedTimer` function to `_root.DialogueMenu_mc` and holds the click.
4. When CHIM calls that function, the plugin replays the held click and dialogue continues.

If CHIM never answers, the plugin releases the click after 20 seconds plus 0.4 seconds per word, so dialogue cannot get stuck.

## Log

`Documents/My Games/Skyrim Special Edition/SKSE/CHIMDialogueTTSBridge.log`. A working click looks like this:

```text
installed startTopicClickedTimer on the dialogue menu
topic 0 selected, sending PlayMenuTopic 'Who_are_the_Black-Briars_'
resuming topic 0 (CHIM reply)
```

`resuming topic N (no reply from CHIM)` means the fallback timer fired and CHIM did not pick up the event.

## Limits

- **Menu structure.** The menu must keep the vanilla `_root.DialogueMenu_mc` clip. A dialogue UI built without it still gets the event, but the NPC will not wait for the player's line.
- **CHIM internals.** The event name and the callback function are CHIM's own, read from CHIM 3.4.2. A CHIM update that renames either breaks the plugin.

## Build

Needs Visual Studio 2022, CMake 3.21+ and vcpkg with `VCPKG_ROOT` set. CommonLibSSE NG is built from source:

```powershell
git clone -b ng --recurse-submodules https://github.com/alandtse/CommonLibVR.git extern/CommonLibVR
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The DLL is written to `Compile/SKSE/Plugins/`. To use an existing CommonLib checkout, pass `-DCOMMONLIBSSE_DIR=<path>` to the first `cmake` command.

## Credits

[CHIM](https://www.nexusmods.com/skyrimspecialedition/mods/126330) by Dwemer Dynamics. This project contains none of CHIM's code or its SWF.
