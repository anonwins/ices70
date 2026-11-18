/* stream.c
 * - Functions for streaming in ices
 * Copyright (c) 2000 Alexander Havang
 * Copyright (c) 2001-4 Brendan Cully <brendan@xiph.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "definitions.h"

extern ices_config_t ices_config;
#include "metadata.h"
#include "replaygain.h"
#include "in_ffmpeg.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define INPUT_BUFSIZ 4096
#define OUTPUT_BUFSIZ 32768
#define SILENCE_SAMPLES 1152
/* sleep this long in ms when every stream has errors */
#define ERROR_DELAY 999

static volatile int finish_send = 0;

static int stream_connect(ices_stream_t* stream);
static int stream_send_data(ices_stream_t* stream, unsigned char* buf, size_t len);
static int stream_send_one(ices_stream_t* stream, unsigned char* buf, size_t len);
static int stream_send(ices_config_t* config, input_stream_t* source);
static int stream_send_silence(ices_config_t* config, int* announced);
static void stream_set_reencoding(int reencoding);
static int stream_open_source(ices_config_t* config, input_stream_t* source);
static int stream_mp3_can_copy(ices_config_t* config, input_stream_t* source);

/* Top level streaming function, called once from main() */
void ices_stream_loop(ices_config_t* config) {
	int consecutive_errors = 0;
	int silence_announced = 0;
	input_stream_t source;
	int rc;

	while (1) {
		// Ask once, then wait for a path and a title
		ices_playlist_ask();
		rc = ices_playlist_poll();

		// Helper died: it has been started again. Ask on the next pass
		if (rc < 0)
			continue;

		// No answer yet: keep the mount up with silence
		if (rc == 0) {
			if (stream_send_silence(config, &silence_announced) < 0) {
				consecutive_errors++;
				if (consecutive_errors > 10) {
					ices_log("Exiting after 10 consecutive errors.");
					ices_setup_shutdown();
				}
			}
			continue;
		}

		// Take the path the helper just wrote
		silence_announced = 0;
		memset(&source, 0, sizeof(source));
		source.path = ices_playlist_take();
		source.fd = -1;

		// Ensure the playlist returned a path
		if (!(source.path && source.path[0])) {
			ices_util_free(source.path);
			continue; // Skip file: The helper wrote an empty path
		}

		// Ensure this pass has not failed ten times
		if (consecutive_errors > 10) {
			ices_log("Exiting after 10 consecutive errors.");
			ices_util_free(source.path);
			ices_setup_shutdown();
		}

		// Clear the previous title and remember the filename
		ices_metadata_set(NULL, NULL);
		ices_metadata_set_file(source.path);

		// Open the file
		if (stream_open_source(config, &source) < 0) {
			ices_log("Error opening %s: %s", source.path, ices_log_get_error());
			ices_util_free(source.path);
			consecutive_errors++;
			continue; // Skip file: It did not open
		}

		// Send the file
		rc = stream_send(config, &source);
		source.close(&source);
		if (rc < 0) {
			ices_log("Encountered error while transfering %s: %s", source.path, ices_log_get_error());
			ices_util_free(source.path);
			consecutive_errors++;
			continue; // Skip file: The send failed
		}

		consecutive_errors = 0;
		ices_util_free(source.path);
	}
}

/* set a flag to tell stream_send to finish early */
void ices_stream_next(void) {
	finish_send = 1;
}

/* Send one buffer to one stream, and pause before a reconnect */
static int stream_send_one(ices_stream_t* stream, unsigned char* buf, size_t len) {
	while (stream_send_data(stream, buf, len) < 0) {
		struct timeval delay;

		if (stream->errs > 10) {
			ices_log("Too many stream errors, giving up");
			ices_setup_shutdown();
		}
		ices_log("Error during send: %s", ices_log_get_error());
		delay.tv_sec = ERROR_DELAY / 1000;
		delay.tv_usec = ERROR_DELAY % 1000 * 1000;
		select(1, NULL, NULL, NULL, &delay);
	}
	return 0;
}

/* Drain the encoder into the mount. A copied MP3 follows these bytes. */
static void stream_flush_encoder(ices_config_t* config) {
	ices_stream_t* stream;
	unsigned char buf[OUTPUT_BUFSIZ];
	int len;

	if (!ices_reencode_is_dirty())
		return;
	for (stream = config->streams; stream; stream = stream->next) {
		len = ices_reencode_flush(stream, buf, sizeof(buf));
		if (len > 0)
			stream_send_one(stream, buf, (size_t)len);
	}
	ices_reencode_mark_clean();
}

/* Encode PCM and send it. Crossfade runs on these samples. */
static int stream_send_pcm(ices_config_t* config, int16_t* left, int16_t* right, int samples) {
	ices_stream_t* stream;
	ices_plugin_t* plugin;
	unsigned char obuf[OUTPUT_BUFSIZ];
	int16_t* rightp;
	int olen;

	if (samples <= 0)
		return 0;
	for (plugin = config->plugins; plugin; plugin = plugin->next)
		samples = plugin->process(samples, left, right);
	if (samples <= 0)
		return 0;

	for (stream = config->streams; stream; stream = stream->next) {
		rightp = right;
		olen = ices_reencode(stream, samples, left, rightp, obuf, sizeof(obuf));
		if (olen < -1) {
			ices_log_error("Reencoding error");
			return -1;
		}
		if (olen > 0)
			stream_send_one(stream, obuf, (size_t)olen);
	}
	return 0;
}

/* Copy MP3 frames. Drain the encoder first so its tail is not stuck. */
static int stream_send_copy(ices_config_t* config, input_stream_t* source) {
	ices_stream_t* stream;
	unsigned char ibuf[INPUT_BUFSIZ];
	ssize_t len;

	stream_flush_encoder(config);
	// This track is a copy. The reencoding file must not stay from the previous song
	stream_set_reencoding(0);
	ices_log("Playing %s", source->path);
	ices_metadata_update(0);
	finish_send = 0;
	while (!finish_send) {
		len = source->read(source, ibuf, sizeof(ibuf));
		if (len == 0)
			break;
		if (len < 0) {
			ices_log_error("Read error: %s", ices_log_get_error());
			return -1;
		}
		for (stream = config->streams; stream; stream = stream->next)
			stream_send_one(stream, ibuf, (size_t)len);
	}
	return 0;
}

/* Decode the file to PCM and encode it with LAME */
static int stream_send_decoded(ices_config_t* config, input_stream_t* source) {
	ices_plugin_t* plugin;
	int16_t left[INPUT_BUFSIZ * 4];
	int16_t right[INPUT_BUFSIZ * 4];
	ssize_t samples;

	for (plugin = config->plugins; plugin; plugin = plugin->next)
		plugin->new_track(source);

	// This track is encoded. The file is in place before the title is published
	stream_set_reencoding(1);
	ices_log("Playing %s", source->path);
	ices_metadata_update(0);
	finish_send = 0;
	while (!finish_send) {
		samples = source->readpcm(source, sizeof(left), left, right);
		if (samples == 0)
			break;
		if (samples < 0) {
			ices_log_error("Decode error");
			return -1;
		}
		rg_apply(left, (int)samples);
		rg_apply(right, (int)samples);
		if (stream_send_pcm(config, left, right, (int)samples) < 0)
			return -1;
	}
	return 0;
}

/* Send one file. A PCM reader is encoded. An MP3 reader is copied. */
static int stream_send(ices_config_t* config, input_stream_t* source) {
	if (source->readpcm)
		return stream_send_decoded(config, source);
	return stream_send_copy(config, source);
}

/* One frame of silence through the crossfader and LAME. The first frame clears the title. */
static int stream_send_silence(ices_config_t* config, int* announced) {
	int16_t left[SILENCE_SAMPLES];
	int16_t right[SILENCE_SAMPLES];

	memset(left, 0, sizeof(left));
	memset(right, 0, sizeof(right));

	// Clear the title once, when the wait starts
	if (!*announced) {
		ices_playlist_clear_metadata();
		ices_metadata_set(NULL, NULL);
		ices_metadata_set_file(NULL);
		stream_set_reencoding(0);
		// The source may not be accepted yet. The non-zero argument waits
		ices_metadata_update(1);
		*announced = 1;
	}

	// Do not call new_track. An empty file would make the crossfader skip the next fade
	return stream_send_pcm(config, left, right, SILENCE_SAMPLES);
}

/* Leave BaseDirectory/reencoding present only while the current track is encoded. */
static void stream_set_reencoding(int reencoding) {
	char path[1024];
	FILE *file;

	// Ensure the base directory can hold the file name
	if (!ices_config.base_directory ||
	    strlen(ices_config.base_directory) > sizeof(path) - strlen("/reencoding") - 1)
		return;

	snprintf(path, sizeof(path), "%s/reencoding", ices_config.base_directory);
	if (!reencoding) {
		unlink(path);
		return;
	}

	// Create the empty marker. A failure leaves the previous file and playback continues
	file = fopen(path, "a");
	if (!file) {
		ices_log_error("Could not record reencoding");
		return;
	}
	fclose(file);
}

/* A known-safe CBR MP3 is copied. Anything uncertain is encoded. */
static int stream_mp3_can_copy(ices_config_t* config, input_stream_t* source) {
	ices_stream_t* stream;

	if (config->plugins)
		return 0;
	if (rg_get_track_gain() != 0.0)
		return 0;
	if (!source->bitrate)
		return 0; // VBR, or a header bitrate the stream does not use
	for (stream = config->streams; stream; stream = stream->next) {
		// An unset rate or channel count is not a match
		if (stream->out_samplerate <= 0 || stream->out_numchannels <= 0)
			return 0;
		if (source->bitrate > (unsigned int)stream->bitrate)
			return 0; // The file would send more than the stream bitrate
		if (source->samplerate != (unsigned int)stream->out_samplerate)
			return 0;
		if (source->channels != (unsigned int)stream->out_numchannels)
			return 0;
	}
	return 1;
}

/* Open path. Copy a matching MP3. Decode everything else with FFmpeg. */
static int stream_open_source(ices_config_t* config, input_stream_t* source) {
	char buf[INPUT_BUFSIZ];
	ssize_t len;
	int fd;
	int rc;

	source->filesize = 0;
	source->bytes_read = 0;
	source->channels = 2;
	source->bitrate = 0;
	source->read = NULL;
	source->readpcm = NULL;
	source->close = NULL;
	rg_set_track_gain(0.0);

	if ((fd = open(source->path, O_RDONLY)) < 0) {
		ices_util_strerror(errno, buf, sizeof(buf));
		ices_log_error("Error opening: %s", buf);
		return -1;
	}
	source->fd = fd;

	if (ices_util_is_regular_file(fd) != 1) {
		close(fd);
		source->fd = -1;
		return -1;
	}

	if ((rc = lseek(fd, 0, SEEK_END)) >= 0) {
		source->filesize = (size_t)rc;
		lseek(fd, 0, SEEK_SET);
	}

	len = read(fd, buf, sizeof(buf));
	if (len <= 0) {
		ices_log_error("Error reading header of %s", source->path);
		close(fd);
		source->fd = -1;
		return -1;
	}

	// Try the MP3 frame reader. A match is copied. Anything else is decoded.
	rc = ices_mp3_open(source, buf, (size_t)len);
	if (rc == 0 && stream_mp3_can_copy(config, source)) {
		source->readpcm = NULL;
		return 0;
	}
	if (rc == 0 && source->close)
		source->close(source);
	else if (source->fd >= 0) {
		close(source->fd);
		source->fd = -1;
	}

	// Decode from the start. The MP3 attempt may have set ReplayGain already.
	rg_set_track_gain(0.0);
	source->fd = -1;
	return ices_ffmpeg_open(source);
}

/* wrapper for shout_send_data, shout_sleep with error handling */
static int stream_send_data(ices_stream_t* stream, unsigned char* buf, size_t len) {
	if (shout_get_connected(stream->conn) != SHOUTERR_CONNECTED) {
		stream_connect(stream);
		if (shout_get_connected(stream->conn) == SHOUTERR_CONNECTED)
			ices_metadata_update(1);
		else
			return -1;
	}

	shout_sync(stream->conn);
	if (shout_send(stream->conn, buf, len) == SHOUTERR_SUCCESS) {
		stream->errs = 0;
		return 0;
	}

	ices_log_error("Libshout reported send error, disconnecting: %s", shout_get_error(stream->conn));
	shout_close(stream->conn);
	stream->errs++;
	return -1;
}

static int stream_connect(ices_stream_t* stream) {
	time_t now = time(NULL);
	const char* mount = shout_get_mount(stream->conn);

	if (stream->connect_delay > now)
		return -1;

	if (shout_open(stream->conn) != SHOUTERR_SUCCESS) {
		ices_log_error("Mount failed on http://%s:%d%s%s, error: %s",
			       shout_get_host(stream->conn), shout_get_port(stream->conn),
			       (mount && mount[0] == '/') ? "" : "/", ices_util_nullcheck(mount),
			       shout_get_error(stream->conn));
		stream->connect_delay = now + 1;
		stream->errs++;
		return -1;
	}

	ices_log("Mounted on http://%s:%d%s%s", shout_get_host(stream->conn),
		 shout_get_port(stream->conn),
		 (mount && mount[0] == '/') ? "" : "/", ices_util_nullcheck(mount));
	return 0;
}
