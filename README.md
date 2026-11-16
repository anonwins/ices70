# ices0

ices0 is a command-line source client. It asks a playlist script for the next file and sends one continuous MP3 stream to an Icecast server.

This tree is ices 0.4.11, kept so it builds and runs on Ubuntu 26.04 against Icecast 2.5, LAME 3.101, and current libshout. ICING starts it as:

```text
ices -station NAME -c config.xml -S script -M /usr/bin/icing-playlist-module.php
```

`-station` is accepted and ignored. The playlist script reads that name from the process command line. `-S` must be `script`.

The sample configuration is [`conf/ices.conf.dist.in`](conf/ices.conf.dist.in).

## What it plays

ices0 reads MP3, Ogg Vorbis, FLAC, and MP4/M4A. Everything it sends to the server is MP3. Vorbis, FLAC, and MP4 are decoded and encoded with LAME, so `<Reencode>1</Reencode>` has to be set for those files. The stream bitrate, sample rate, and channel count are `Stream/Bitrate`, `Stream/Samplerate`, and `Stream/Channels`.

ReplayGain and crossfading run only while reencoding is on. ReplayGain is read from MP3 (`TXXX:replaygain_track_gain`, and `RVA2` when that TXXX frame is absent), from FLAC and Ogg Vorbis comments, and from the same replay-gain tags in an MP4 file. Crossfade length, a full-volume crossmix, and a minimum track length for the crossfader are `Playlist/Crossfade`, `Playlist/CrossMix`, and `Playlist/MinCrossfade`. `Crossfade` is read before the other two.

The playlist script prints the path on the first line and an optional title on the second. The path is absolute, or starts with `./`. Song titles are sent to Icecast as UTF-8. On an MP3 mount, set `<charset>UTF8</charset>` so Icecast stores them that way. The update is a single `GET /admin/metadata?mode=updinfo` request. libshout's own metadata call asks Icecast 2.5 for `/admin/!POKE`, gets a 400, and retries without pausing, so ices0 does not use it.

`SIGUSR1` skips to the next track. The source protocol is HTTP.

## Build

Install the libraries and a compiler:

```bash
sudo apt-get install build-essential automake autoconf libtool pkg-config
sudo apt-get install libxml2-dev libogg-dev libvorbis-dev libshout3-dev
sudo apt-get install libmp3lame-dev libflac-dev
sudo apt-get install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev
```

MP4 decoding uses FFmpeg. Ubuntu 26.04 does not ship libmp4v2. `configure` stops if libxml2, LAME, Vorbis, FLAC, or the FFmpeg libraries are missing.

```bash
aclocal
autoreconf -fi
automake --add-missing
./configure
make
sudo make install
```

`configure` prints a feature list before it finishes. XML, LAME, Vorbis, FLAC, and MP4 should all be `yes`. The binary is `ices`.

Source files in this repository use LF line endings, including `configure.ac`. A checkout that converts them to CRLF will make `configure` look for `conf/Makefile` with a carriage return in the name and then fail.
