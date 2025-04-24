
# 🎧 Play Loud — the headless UDP audio daemon

`Play Loud` is a minimalist, headless, audio daemon built for command-based playback over local UDP. It works in tandem with small CLI clients like `play` and `q`, allowing audio playback from any launcher, script, or custom tool.

It is engineered for clean folder/file based queue control, channel upmixing, and minimal footprint, running silently in the background without user-facing UI. It relies on [amazing miniaudio](https://github.com/mackron/miniaudio) which is included in the repo for fire and forget compilation.

---

## 🔧 Architecture

```
        +-----------+     UDP     +-----------+
        |  play.exe | ---------> |           |
        |  q.exe    | ---------> |  loud.exe |
        +-----------+           |  (daemon) |
                                +-----------+
```

- `loud.exe`: Listens on UDP port `7001`, receives playback commands, manages audio queue and output.
- `play.exe`: Sends direct playback commands (`play:<file>`, `n`, `p`, `q`).
- `q.exe`: Adds files to the queue (`q:<file>`), or stops/quits.

---

## 🎚️ Features

- **Headless**: Hidden Windows app (`-mwindows`), silent background process.
- **Flexible Input**:
  - Play single files or entire directories (with shuffle).
  - Queue new audio files or folders while playing.
- **Queue-aware**:
  - Tracks history (`n` for next, `p` for previous).
  - Continues next track when tack playback ends.
- **Scrobbling**: Keeps limited playback history for previews.
- **Channel Upmixing**: Automatically adapts audio to current output setup (via `Audio::Player`).
- **Fail-safe Daemon**: Auto-starts `loud.exe` from clients if not already running.
- **Supports `Windows`.

---

## 🚀 Usage

### Start playback:
```bash
play.exe path\to\track.mp3
play.exe path\to\music\folder
```

### Playback controls:
```bash
play.exe n       # Next track
play.exe p       # Previous track
play.exe         # Stops playback (no argument)
```

### Queue files:
```bash
q.exe path\to\track.wav
q.exe path\to\folder
```

### Quit daemon:
```bash
play.exe         # with no argument (stops playback)
play.exe/q.exe q          # sends quit signal to daemon
```

---

## 🛠️ Building

### Requirements
- C++17 or higher
- Windows + MinGW or MSVC

### Build example (MinGW):

```bash
g++ loud.cpp -o loud.exe -std=c++17 -lwinmm -mwindows
g++ play.cpp -o play.exe -std=c++17 -mwindows
g++ q.cpp    -o q.exe    -std=c++17 -mwindows
```

---

## 📝 Notes

- `play.reg` must reference the full absolute path to `play.exe` when registering shell integration or context menu bindings.
- Command-line parsing supports full Unicode paths (via `WideCharToMultiByte`).
- `player.bat` can be used as a sample launcher or shortcut script.
- Default UDP port: `7001`
- Tested file formats include: `.mp3`, `.ogg`, `.flac`

---

## 🛠️ TODO

These enhancements are planned or under consideration:

### 🔗 YouTube Playback Support
- [ ] Add YouTube support in `play.exe`:
  - Detect URL input (e.g. `https://...youtube.com/watch?...`)
  - Resolve and stream audio directly or cache for local play
- [ ] Implement TCP/WebSocket layer for remote (LAN/WAN) control
- [ ] Add basic HTTP/Web UI frontend to control `loud` via browser
- [ ] Enable control from mobile or other PCs on the network
- [ ] On-screen display popup (optional) to show current track
- [ ] Volume control over UDP
- [ ] Playlist persistence between sessions
- [ ] Add support for other files and playlists
- [ ] Add support for systems other than Windows, maybe

PRs welcome. Goals aim to keep it minimal, scriptable, and fast.

---


## 🧠 Credits

Built with prompts by ChatGPT and Cursor, supervised by a human. Designed for composable, scriptable audio control without the bloat. I tried not to read the code, honestly, I tried not to read this readme either.

---

## 📂 Example shell integration (Windows Registry)

Edit `play.reg` to reference your actual `play.exe` path:

```reg
[HKEY_CLASSES_ROOT\*\shell\Play with Loud\command]
@="\"C:\\full\\path\\to\\play.exe\" \"%1\""
```

Then run it via double-click or from your setup script. Adding to PATH works too.

---
