# ices70 changes for ICING

Done in this tree and in ICING.

ICING owns station policy: the schedule, jingles, artist skip, library sync, nginx, and certificates. ices70 asks what to play, sends it, and stays connected to Icecast.

## Playback

FFmpeg is the only decoder. LAME only encodes. A matching MP3 is copied: MP3, the header bitrate and the samplerate and channels match the stream, crossfade is off, and ReplayGain has no gain to apply. Every other file is PCM, then LAME. `<Reencode>` is gone.

## Playlist

ICING starts ices70 with `ICING_STATION` set. ices70 starts the playlist module once. One newline asks. The module answers with a path and a title when a file is eligible, and checks the library once a second until then. End of file restarts the helper.

## Silence

While the ask has no answer, ices70 encodes silence on `/autodj`. The crossfader sees those samples, so a withheld tail is not stuck until the next song. An answer that is already waiting is played with no silence in between.

Stream timeout, visitor pings, and the ffmpeg silence feeder are removed. A crash of ices70 drops listeners until the next service pass starts it again.

## Already done

Title updates no longer use libshout. `src/metadata.c` sends one `GET /admin/metadata?mode=updinfo` and exits.
