/* reencode.c
 * - Functions for reencoding in ices
 * Copyright (c) 2000 Alexander Haväng
 * Copyright (c) 2001-3 Brendan Cully
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
 *
 */

#include "definitions.h"

#ifdef HAVE_LAME_LAME_H
# include <lame/lame.h>
#else
# include <lame.h>
#endif

extern ices_config_t ices_config;

/* Set once encode has produced bytes, so a copied MP3 can drain the encoder first */
static int encoder_dirty = 0;

/* Build one LAME encoder per stream from the stream rate, channels, and bitrate */
void ices_reencode_initialize(void) {
	ices_stream_t* stream;

	ices_log_debug("Using LAME version %s", get_lame_version());

	// Build the encoder for each stream
	for (stream = ices_config.streams; stream; stream = stream->next) {
		lame_global_flags* lame;

		// Use the configured rate and channels (the crossfader is built for 44100)
		if (stream->out_samplerate <= 0)
			stream->out_samplerate = 44100;
		if (stream->out_numchannels <= 0)
			stream->out_numchannels = 2;

		if (!(stream->encoder_state = lame_init())) {
			ices_log("LAME: error creating encoder.");
			ices_setup_shutdown();
		}
		lame = (lame_global_flags*) stream->encoder_state;
		lame_set_in_samplerate(lame, stream->out_samplerate);
		lame_set_num_channels(lame, stream->out_numchannels);
		lame_set_brate(lame, stream->bitrate);
		if (stream->out_numchannels == 1)
			lame_set_mode(lame, MONO);
		lame_set_out_samplerate(lame, stream->out_samplerate);
		lame_set_original(lame, 0);
		if (lame_init_params(lame) < 0) {
			ices_log("LAME: error setting encoder parameters.");
			lame_close(lame);
			stream->encoder_state = NULL;
			ices_setup_shutdown();
		}
	}
}

/* Shut the encoders down */
void ices_reencode_shutdown(void) {
	ices_stream_t* stream;

	for (stream = ices_config.streams; stream; stream = stream->next)
		if (stream->encoder_state) {
			lame_close((lame_global_flags*) stream->encoder_state);
			stream->encoder_state = NULL;
		}
	encoder_dirty = 0;
}

/* Encode one block of PCM. outlen is the size of outbuf. */
int ices_reencode(ices_stream_t* stream, int nsamples, int16_t* left,
		  int16_t* right, unsigned char *outbuf, int outlen) {
	lame_global_flags* lame = (lame_global_flags*) stream->encoder_state;
	int encoded;

	encoded = lame_encode_buffer(lame, left, right, nsamples, outbuf, outlen);
	if (encoded > 0)
		encoder_dirty = 1;
	return encoded;
}

/* Whether a copied MP3 has to drain the encoder before its own frames */
int ices_reencode_is_dirty(void) {
	return encoder_dirty;
}

/* Drain one encoder and prepare it for a later PCM stretch */
int ices_reencode_flush(ices_stream_t* stream, unsigned char *outbuf, int maxlen) {
	lame_global_flags* lame = (lame_global_flags*) stream->encoder_state;
	int rc;

	if (!lame)
		return 0;
	rc = lame_encode_flush(lame, outbuf, maxlen);
	lame_init_params(lame);
	return rc;
}

/* The drained bytes have been sent */
void ices_reencode_mark_clean(void) {
	encoder_dirty = 0;
}
