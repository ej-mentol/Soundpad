# Soundpad

In-game soundboard for MetaHookSv (GoldSrc / Sven Co-op): a VGUI2 library of
sounds with categories, chat triggers, hotkeys, event reactions and text to
speech. Voice broadcasting is delegated to
[VoiceExtension](https://github.com/ej-mentol/VoiceExtension); Soundpad itself
never touches the engine voice path.

## Requirements

- MetaHookSv loader (client plugin).
- VGUI2Extension — the window is built from its controls.
- VoiceExtension — required only for broadcasting to other players. Without it
  Soundpad still opens, plays sounds locally and reports one warning per
  broadcast attempt.

## Install

1. Copy `Soundpad.dll` to `<game>/metahook/plugins/`.
2. Copy `VoiceExtension.dll` there as well if you want to broadcast.
3. Start the game, open the console and run `soundpad` (or bind a key:
   `bind "p" "soundpad"`).

Only the DLL is distributed; the PDB is a build artifact and is not needed to
run the plugin.

## Window

Four tabs:

- **Soundpad** — category tree on the left, sound list on the right, transport
  and volume/pitch sliders below. List columns: `#`, `Title`, `Size`, `Hotkey`,
  `Chat Trigger`, `Vol`, `Pitch`, `File Path`, `State`.
  - `+ File` / `+ Folder` import WAV/MP3/OGG into the active category; imported
    files are copied to `sound/soundpad/<category>/`.
  - Right click: preview, play (cut), queue, set hotkey, set chat trigger, use
    filename as trigger, clear trigger, move to category, delete.
  - All of the above act on the whole selection, not just the clicked row.
  - `Space` plays a local preview of the selection; in the reactions tab it
    fires the selected trigger instead. Double click follows the
    `Dbl-click = Preview` checkbox.
- **Game Reactions** — event triggers for `Death`, `Damage` and `Kill` with a
  condition filter, chance (0.01 % .. 100 %) and per-trigger cooldown.
- **Macros** — ordered sequences of sounds with repeats, pause and pitch step
  per step. Run them from the window, by hotkey or with `sp_seq_play`.
- **Text-To-Speech (TTS)** — Klatter (built-in formant synthesis) or Windows
  SAPI5, with volume, pitch, whisper and random style options.

The `State` column shows `PLAYING`, `QUEUED`, `MISSING` or stays empty.

## Chat triggers

Each sound can have a chat trigger — the word players type in chat. A trigger
may list alternatives separated by `,` or `;` (`laugh, haha`), and may carry a
trailing pitch number (`laugh 0.8`). When several sounds share a trigger a
random one is played. Two limits apply, both silent by design: the same sound
does not retrigger within 2 seconds, and identical text from the same sender
within 1 second is treated as a duplicate message.

Triggers are matched against player chat (`SayText` / `TextMsg`). Text printed
by the server is ignored unless it looks like player chat and `sp_allow_server`
is non-zero.

## Console commands

| Command | Description |
| --- | --- |
| `soundpad`, `sp_gui` | Toggle the window |
| `spplay <path\|#row> [vol] [pitch] [q]` | Play with cut; `q` queues instead |
| `spqueue <path\|#row> [vol] [pitch]` | Queue behind the current sound |
| `sp_play [path] [vol] [pitch] [q]` | Server/chat path: queues, never cuts |
| `sp_queue [path] [vol] [pitch]` | Explicit queue form |
| `sppreview <path\|#row> [vol] [pitch]` | Local preview only |
| `sp_preview [path] [vol] [pitch]` | Same, with no arguments uses the selection |
| `sp_stop` | Stop playback and clear the queue |
| `sp_panic` | Emergency stop of everything |
| `sprandom` / `sp_random [category]` | Play a random sound |
| `spnext` / `sp_next [category]` | Next sound |
| `spprev` / `sp_prev [category]` | Previous sound |
| `sp_add <path> [category] [title]` | Add a file to the library |
| `sp_addfolder <dir> [category]` | Import a folder |
| `spsave`, `sp_save` | Save the library |
| `sp_reload` | Reload `soundpad.vdf` |
| `sp_dump` | Voice backend, transmitting state, mute and master volume |
| `sp_trigger <death\|damage\|kill\|chat\|cmd\|teamdeath> [param]` | Fire a reaction manually |
| `sp_seq_play <name>` | Run a macro |
| `sp_medic`, `sp_call_medic` | Fire the `Command` reaction named `medic` |
| `sp_cmd <name>` | Fire `Command` reactions by name |
| `tts`, `sp_tts`, `sp_speak <text>` | Speak text locally and/or in voice |
| `sp_aim_tts` | Print the entity under the crosshair |
| `sp_dot_stop [0\|1]` | Toggle “type `.` to stop playback” |

## Console variables

| Variable | Default | Description |
| --- | --- | --- |
| `sp_debug` | `0` | Diagnostic output to the console and DebugView |
| `sp_allow_server` | `1` | Allow server-printed chat to trigger sounds |
| `sp_mic_duck` | `1` | Pause playback while `+voicerecord` is held |
| `sp_pause_on_mic` | `0` | Alias of `sp_mic_duck` |
| `sp_reactions` | `1` | Enable event reactions |
| `sp_autohide` | `1` | Hide the window when the game UI opens |
| `sp_tts_engine` | `klatter` | `klatter` or `sapi` |
| `sp_sapi_voice` | empty | SAPI5 voice name |
| `sp_tts_volume`, `sp_tts_pitch` | `1.0` | TTS level and base pitch |
| `sp_tts_jitter` | `1` | Random pitch jitter |
| `sp_tts_whisper` | `0` | Whisper synthesis |
| `sp_tts_autosay` | `0` | Speak chat lines |
| `sp_tts_random_style` | `0` | Random voice style per phrase |
| `sp_medic_auto` | `0` | Call the medic automatically below the threshold |

All variables are runtime-only (not archived in `config.cfg`).

## Configuration

The library is stored in `svencoop/soundpad/soundpad.vdf` (KeyValues). The file
is created with a small Half-Life template on first run. Audio files added from
the `+ File` button are copied under `svencoop/sound/soundpad/`.

Changes are written to the file as they are made; `Save` in the window or `sp_save` rewrites it explicitly.

## Limitations

- Broadcasting requires VoiceExtension; without it sounds are local only.
- Effect processing (reverb, tremolo, fades) and the timeline slicer are
  disabled in this build. They only ever worked on WAV because Soundpad decodes
  PCM itself; MP3 and OGG play unprocessed.
- Chat triggers are limited by the per-sound cooldown and duplicate filter
  described above; there is no configuration for either value.
- `sp_ban*` and `sp_alias_*` commands are not registered in this build; the
  player list tab is compiled out.

## Build (optional)

Visual Studio 2022, `Release | Win32`, static CRT:

```
build_plugin.bat
```

CMake (standalone) also works and needs the MetaHook and VGUI2Extension source
trees:

```
cmake -S . -B build -A Win32 ^
  -DMETAHOOK_SOURCE_PATH=../../MetaHook ^
  -DVGUI2EXTENSION_SOURCE_PATH=../VGUI2Extension
cmake --build build --config Release
```

## License

MIT — see `LICENSE`. Bundled third-party code: minimp3 (CC0 1.0), see
`THIRD_PARTY_NOTICES.md`.
