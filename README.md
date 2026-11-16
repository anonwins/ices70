# ices70

ices70 is a command-line source client. It asks a playlist script for the next file and sends one continuous MP3 stream to an Icecast server.

This tree is ices70 70.0.0, kept so it builds and runs on Ubuntu 26.04 against Icecast 2.5, LAME 3.101, FFmpeg, and current libshout. ICING starts it with `ICING_STATION` set in the environment:

```text
ICING_STATION=NAME ices70 -c config.xml -S script -M /usr/bin/icing-playlist-module.php
```

`-S` must be `script`. The playlist module reads `ICING_STATION`. ices70 does not parse the name.

The sample configuration is [`conf/ices70.conf.dist.in`](conf/ices70.conf.dist.in).

## What it plays

FFmpeg opens every file. Everything sent to the server is MP3 at `Stream/Bitrate`, `Stream/Samplerate`, and `Stream/Channels`.

A file is copied through when it is MP3, its bitrate, samplerate, and channels already match the stream, crossfade is off, and ReplayGain has nothing to apply. A VBR file whose header bitrate differs from the stream is encoded. Every other file is decoded to PCM and encoded with LAME. Crossfade and ReplayGain need samples, so a matching MP3 is encoded when either of those is on.

ReplayGain is read from the file tags. Crossfade length, a full-volume crossmix, and a minimum track length for the crossfader are `Playlist/Crossfade`, `Playlist/CrossMix`, and `Playlist/MinCrossfade`. `Crossfade` is read before the other two.

The playlist module stays running. ices70 writes one newline when it needs a track. The module answers with a path and a title when a file is eligible, and writes nothing until then. While that answer is missing, ices70 stays mounted and sends silence. The path is absolute, or starts with `./`.

Song titles are sent to Icecast as UTF-8. On an MP3 mount, set `<charset>UTF8</charset>` so Icecast stores them that way. The update is a single `GET /admin/metadata?mode=updinfo` request. libshout's own metadata call asks Icecast 2.5 for `/admin/!POKE`, gets a 400, and retries without pausing, so ices70 does not use it.

`SIGUSR1` skips to the next track. The source protocol is HTTP.

## Build

Install the libraries and a compiler:

```bash
sudo apt-get install build-essential automake autoconf libtool pkg-config
sudo apt-get install libxml2-dev libogg-dev libshout3-dev libmp3lame-dev
sudo apt-get install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev
```

`configure` stops if libxml2, LAME, or the FFmpeg libraries are missing.

```bash
aclocal
autoreconf -fi
automake --add-missing
./configure
make
sudo make install
```

`configure` prints a feature list before it finishes. XML, LAME, and FFmpeg should all be `yes`. The binary is `ices70`.

Source files in this repository use LF line endings, including `configure.ac`. A checkout that converts them to CRLF will make `configure` look for `conf/Makefile` with a carriage return in the name and then fail.
