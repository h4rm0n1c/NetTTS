# NetTTS

<p align="center">
  <img src="app.ico" alt="NetTTS icon" width="96" />
</p>

<p align="center">
  <img src="nettts_main_window.png" alt="NetTTS main window" width="720" />
</p>

<p align="center">
  <strong>Demo video (YouTube)</strong><br />
  <a href="https://www.youtube.com/watch?v=PFJt_EG5f_c">
    <img src="https://img.youtube.com/vi/PFJt_EG5f_c/hqdefault.jpg" alt="NetTTS demo video on YouTube" width="720" />
  </a>
</p>

NetTTS is a small Win32 front end for AT&T's FlexTalk and other SAPI 4.0
voices. It runs on old Windows boxes, modern Windows, and Wine.

FlexTalk is the engine Valve used for the VOX / Black Mesa announcement
samples in Half-Life, and NetTTS has prosody modes based on how those lines
were put together. This repo also keeps FlexTalk itself around so it doesn't
disappear. Fun! :D

What it does:

- Speaks text typed into the window, or sent to it over TCP (e.g. with netcat).
- Clean, HL VOX LQ and HL VOX HQ prosody modes; volume, rate and pitch sliders.
- Saves speech as WAV files (new in 1.1a).
- A status socket that reports when speech starts and stops, for things like
  ducking music in OBS.

Want to make it sing? There's a [NetTTS sing-along gist](https://gist.github.com/h4rm0n1c/2ddaa14c03be25c2072347a1b27e25da).

## Installing

### On Windows

1. Download `nettts_gui.zip` from [Releases](../../releases) and unpack it.
2. Install the SAPI 4.0 runtime (`third_party/Dependencies/spchapi.exe`) if
   you don't already have it.
3. Install FlexTalk from `third_party/Dependencies/flextalk.zip`.
4. Run `nettts_gui.exe`.

### On Linux (Wine)

The setup script builds a Windows XP Wine prefix with SAPI 4.0, FlexTalk and
NetTTS already installed:

```bash
curl -fsSLo setup_nettts_prefix.sh https://raw.githubusercontent.com/h4rm0n1c/NetTTS/main/scripts/winetricks/setup_nettts_prefix.sh \
  && bash setup_nettts_prefix.sh --root-dir "$(pwd)/nettts"
```

or, from a checkout, `./scripts/winetricks/setup_nettts_prefix.sh`.

Everything goes under `~/nettts/` by default (prefix in `~/nettts/wineprefix/`,
log in `~/nettts/wineprefix/drive_c/nettts/nettts.log`). The script uses
winetricks for `winxp`, `vcrun6`, `mfc42` and `riched20`, downloads SAPI,
FlexTalk and the latest NetTTS release, and runs the FlexTalk installer for
you (automation by [@zulc22](https://github.com/zulc22)).

It also installs some helper scripts:

- `~/nettts/bin/nettts-daemon.sh` starts and stops the TCP server.
- `~/nettts/bin/nettts-gui.sh` opens the window.
- `~/nettts/bin/flextalk-controlpanel.sh` opens the FlexTalk control panel.

More detail is in [docs/winetricks.md](docs/winetricks.md).

## Saving speech as WAV

New in 1.1a. There are two ways to do it from the window:

- **Save as WAV...** writes whatever is in the text box to a WAV file you
  choose, instead of playing it. Volume, rate, pitch and the prosody mode
  apply as they do for Speak. When it's done, the line next to the button
  shows the file name and length.
- **Save each utterance as a WAV file in:** (in the Sound box) writes every
  utterance to its own file in that folder instead of playing it, whether it
  came from the Speak button or over TCP. Files are named after their text,
  for example `attention_security.wav`, then `attention_security_2.wav` if
  the same line comes up again. Untick it to go back to the speakers.

The second one is handy for making a batch of lines over the network:

```bash
wine nettts_gui.exe --startserver --wav-dir 'C:\vox_lines'
printf 'Attention, security personnel.\nThe test chamber is now open.\n' | nc -q 0 127.0.0.1 5555
```

TCP clients only send text; they can't choose where files go.

From the command line:

```bash
# one line into one file, then exit
wine nettts_gui.exe --headlessnoconsole --wav-out 'C:\temp\hello.wav' --text "Hello from NetTTS"

# everything said during this run into one file
wine nettts_gui.exe --startserver --headlessnoconsole --wav-out 'C:\temp\session.wav'

# one file per utterance
wine nettts_gui.exe --wav-dir 'C:\vox_lines'
```

NetTTS exits with code 3 if it can't create the `--wav-out` file. `--say`
works as well as `--text`.

The files are in whatever format the voice produces, with no resampling. For
FlexTalk that's mono, 16-bit, 11025 Hz.

## TCP server

Press **Start Server**, or start NetTTS with `--startserver`. It listens on
two ports:

- `5555` (`--port`) takes text, one line per utterance.
- `5556` (`--status-port`, defaults to the text port + 1) sends status lines:
  - `START` when speech begins;
  - `STOP` when it ends (or when the last WAV file is done);
  - `WAV <path>` when a WAV file has been written.

The text connection stays open. To send one line and close:

```bash
printf 'Hello World From Net TTS NetCat TCP Server!\n' | nc -q 0 127.0.0.1 5555
```

BSD netcat uses `-N` instead:

```bash
printf 'Message for Gordon Freeman, you will not escape this time.\n' | nc -N 127.0.0.1 5555
```

Reacting to the status port:

```bash
nc 127.0.0.1 5556 | while read -r line; do
  case "$line" in
    START) echo "duck bgm" ;;   # replace with your OBS control shim
    STOP)  echo "restore" ;;
  esac
done
```

Lines starting with `/` are commands: `/rate N`, `/pitch N`, `/pause ms` and
`/stop`. `[[pause 500]]` inside a line inserts a pause. The Help button lists
these, the FlexTalk `\!` tags, and all the command-line options.

## Choosing the output device

Pick it in the Sound box, or on the command line. `--list-devices` prints the
numbers and names:

```bash
wine nettts_gui.exe --headless --list-devices
```

`--devnum` takes a number or part of a device name (not case-sensitive):

```bash
wine nettts_gui.exe --devnum 2
wine nettts_gui.exe --devnum ATR2USB
```

`DEVICE` in `nettts-daemon.conf` works the same way, and `-1` means the
default device.

## Building

On Linux with MinGW-w64:

```bash
sudo apt-get install -y make mingw-w64 g++-mingw-w64-i686
make -f Makefile.mingw -j"$(nproc)"
```

The exe ends up in `build/nettts_gui.exe`. `make -f Makefile.mingw clean`
removes it.

The build uses the `speech.h` in `third_party/include/`, so you don't need
the old Speech SDK. To use a different one:

```bash
make -f Makefile.mingw INC_DIR="C:/Program Files/Microsoft Speech SDK/Include" -j"$(nproc)"
```

## Credits

Thanks to Valve for the games, and to the classic Win32 UI era that inspired
this project.

AutoHotKey automation by [@zulc22](https://github.com/zulc22).
