/* in_ffmpeg.c
 * Demux and decode any audio file with FFmpeg.
 * Output is stereo or mono s16 at the stream sample rate.
 */

#include "config.h"
#include "in_ffmpeg.h"
#include "metadata.h"
#include "replaygain.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

extern ices_config_t ices_config;

typedef struct {
	AVFormatContext *fmt;
	AVCodecContext *codec;
	SwrContext *swr;
	AVPacket *pkt;
	AVFrame *frame;
	int stream_index;
	int draining;
	int eof;
	int channels;
	int16_t *pcm;
	int pcm_capacity;
	int pcm_samples;
	int pcm_pos;
} ffmpeg_in_t;

static void ices_ffmpeg_read_metadata(AVFormatContext *fmt, int stream_index);
static int ices_ffmpeg_convert(ffmpeg_in_t *ff);
static int ices_ffmpeg_pull(ffmpeg_in_t *ff);
static ssize_t ices_ffmpeg_readpcm(input_stream_t *self, size_t olen, int16_t *left, int16_t *right);
static int ices_ffmpeg_close(input_stream_t *self);

static void ices_ffmpeg_log_av_error(const char *what, int err) {
	char buf[128];

	av_strerror(err, buf, sizeof(buf));
	ices_log_error("%s: %s", what, buf);
}

static const char *ices_ffmpeg_dict_value(const AVDictionary *dict, const char *key) {
	const AVDictionaryEntry *entry;

	if (!dict)
		return NULL;
	entry = av_dict_get(dict, key, NULL, 0);
	return entry ? entry->value : NULL;
}

/* Read artist, title, and ReplayGain from the container and the audio stream */
static void ices_ffmpeg_read_metadata(AVFormatContext *fmt, int stream_index) {
	const AVDictionary *dicts[2];
	const char *artist = NULL;
	const char *title = NULL;
	const char *gain = NULL;
	int i;

	dicts[0] = fmt->metadata;
	dicts[1] = fmt->streams[stream_index]->metadata;
	for (i = 0; i < 2; i++) {
		if (!artist)
			artist = ices_ffmpeg_dict_value(dicts[i], "artist");
		if (!title)
			title = ices_ffmpeg_dict_value(dicts[i], "title");
		if (!gain)
			gain = ices_ffmpeg_dict_value(dicts[i], "REPLAYGAIN_TRACK_GAIN");
		if (!gain)
			gain = ices_ffmpeg_dict_value(dicts[i], "replaygain_track_gain");
	}

	if (artist || title)
		ices_metadata_set(artist, title);
	if (gain)
		rg_set_track_gain(atof(gain));
}

/* The rate and channel count LAME was built for */
static void ices_ffmpeg_output_format(int *rate, int *channels) {
	ices_stream_t *stream = ices_config.streams;

	*rate = 44100;
	*channels = 2;
	if (!stream)
		return;
	if (stream->out_samplerate > 0)
		*rate = stream->out_samplerate;
	if (stream->out_numchannels > 0)
		*channels = stream->out_numchannels;
}

static int ices_ffmpeg_convert(ffmpeg_in_t *ff) {
	int out_samples;
	uint8_t *out_planes[1];
	int converted;

	out_samples = swr_get_out_samples(ff->swr, ff->frame->nb_samples);
	if (out_samples < 0) {
		ices_ffmpeg_log_av_error("Could not size the PCM buffer", out_samples);
		return -1;
	}
	if (out_samples > ff->pcm_capacity) {
		int16_t *grown = realloc(ff->pcm, (size_t)out_samples * (size_t)ff->channels * sizeof(int16_t));

		if (!grown) {
			ices_log_error("Malloc failed in ices_ffmpeg_readpcm");
			return -1;
		}
		ff->pcm = grown;
		ff->pcm_capacity = out_samples;
	}

	out_planes[0] = (uint8_t *)ff->pcm;
	converted = swr_convert(ff->swr, out_planes, out_samples,
				(const uint8_t **)ff->frame->extended_data,
				ff->frame->nb_samples);
	av_frame_unref(ff->frame);
	if (converted < 0) {
		ices_ffmpeg_log_av_error("Resample failed", converted);
		return -1;
	}
	ff->pcm_samples = converted;
	ff->pcm_pos = 0;
	return converted;
}

/* Decode until one block of PCM is ready, or the file ends */
static int ices_ffmpeg_pull(ffmpeg_in_t *ff) {
	int ret;

	while (!ff->eof) {
		ret = avcodec_receive_frame(ff->codec, ff->frame);
		if (ret == 0)
			return ices_ffmpeg_convert(ff);
		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			ices_ffmpeg_log_av_error("Decode failed", ret);
			return -1;
		}
		if (ff->draining) {
			uint8_t *out_planes[1];
			int flushed;

			if (ff->pcm_capacity < 1) {
				ff->eof = 1;
				return 0;
			}
			out_planes[0] = (uint8_t *)ff->pcm;
			flushed = swr_convert(ff->swr, out_planes, ff->pcm_capacity, NULL, 0);
			ff->eof = 1;
			if (flushed < 0) {
				ices_ffmpeg_log_av_error("Resample flush failed", flushed);
				return -1;
			}
			ff->pcm_samples = flushed;
			ff->pcm_pos = 0;
			return flushed;
		}

		ret = av_read_frame(ff->fmt, ff->pkt);
		if (ret == AVERROR_EOF) {
			avcodec_send_packet(ff->codec, NULL);
			ff->draining = 1;
			continue;
		}
		if (ret < 0) {
			ices_ffmpeg_log_av_error("Read failed", ret);
			return -1;
		}
		if (ff->pkt->stream_index != ff->stream_index) {
			av_packet_unref(ff->pkt);
			continue;
		}
		ret = avcodec_send_packet(ff->codec, ff->pkt);
		av_packet_unref(ff->pkt);
		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			ices_ffmpeg_log_av_error("Decode failed", ret);
			return -1;
		}
	}

	return 0;
}

/* Open path for decode. Returns 0, or -1 when the file cannot be decoded. */
int ices_ffmpeg_open(input_stream_t *self) {
	ffmpeg_in_t *ff = NULL;
	AVFormatContext *fmt = NULL;
	const AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;
	SwrContext *swr = NULL;
	AVChannelLayout out_layout;
	int stream_index;
	int out_rate;
	int out_channels;
	int ret;

	memset(&out_layout, 0, sizeof(out_layout));
	ices_ffmpeg_output_format(&out_rate, &out_channels);

	// Open the file by path (FFmpeg does not use the fd the caller probed with)
	ret = avformat_open_input(&fmt, self->path, NULL, NULL);
	if (ret < 0) {
		ices_ffmpeg_log_av_error("Could not open the file", ret);
		return -1;
	}
	ret = avformat_find_stream_info(fmt, NULL);
	if (ret < 0) {
		ices_ffmpeg_log_av_error("Could not read streams", ret);
		goto errFormat;
	}

	stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (stream_index < 0 || !codec) {
		ices_log_error("No audio track found in %s", self->path);
		goto errFormat;
	}

	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		ices_log_error("Could not allocate a decoder");
		goto errFormat;
	}
	ret = avcodec_parameters_to_context(codec_ctx, fmt->streams[stream_index]->codecpar);
	if (ret < 0) {
		ices_ffmpeg_log_av_error("Could not copy stream parameters", ret);
		goto errCodec;
	}
	ret = avcodec_open2(codec_ctx, codec, NULL);
	if (ret < 0) {
		ices_ffmpeg_log_av_error("Could not open the decoder", ret);
		goto errCodec;
	}
	if (codec_ctx->sample_rate <= 0 || codec_ctx->ch_layout.nb_channels <= 0) {
		ices_log_error("Audio track has no sample rate or channels");
		goto errCodec;
	}

	av_channel_layout_default(&out_layout, out_channels);
	ret = swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, out_rate,
				  &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
				  0, NULL);
	av_channel_layout_uninit(&out_layout);
	if (ret < 0 || !swr) {
		ices_log_error("Could not set up PCM output");
		goto errCodec;
	}
	if (swr_init(swr) < 0) {
		ices_log_error("Could not set up PCM output");
		goto errSwr;
	}

	if (!(ff = calloc(1, sizeof(ffmpeg_in_t)))) {
		ices_log_error("Malloc failed in ices_ffmpeg_open");
		goto errSwr;
	}
	ff->pkt = av_packet_alloc();
	ff->frame = av_frame_alloc();
	if (!ff->pkt || !ff->frame) {
		ices_log_error("Malloc failed in ices_ffmpeg_open");
		goto errData;
	}

	ices_ffmpeg_read_metadata(fmt, stream_index);
	ices_log_debug("Decoding %s, codec %s, %d Hz %d ch -> %d Hz %d ch",
		       self->path, codec->name, codec_ctx->sample_rate, codec_ctx->ch_layout.nb_channels,
		       out_rate, out_channels);

	if (self->fd >= 0) {
		close(self->fd);
		self->fd = -1;
	}
	self->samplerate = (unsigned int)out_rate;
	self->channels = (unsigned int)out_channels;
	if (codec_ctx->bit_rate > 0)
		self->bitrate = (unsigned int)(codec_ctx->bit_rate / 1000);

	ff->fmt = fmt;
	ff->codec = codec_ctx;
	ff->swr = swr;
	ff->stream_index = stream_index;
	ff->channels = out_channels;
	self->type = ICES_INPUT_MP4;
	self->data = ff;
	self->read = NULL;
	self->readpcm = ices_ffmpeg_readpcm;
	self->close = ices_ffmpeg_close;
	return 0;

 errData:
	if (ff) {
		av_packet_free(&ff->pkt);
		av_frame_free(&ff->frame);
		free(ff);
	}
 errSwr:
	swr_free(&swr);
 errCodec:
	avcodec_free_context(&codec_ctx);
 errFormat:
	avformat_close_input(&fmt);
	return -1;
}

static ssize_t ices_ffmpeg_readpcm(input_stream_t *self, size_t olen, int16_t *left, int16_t *right) {
	ffmpeg_in_t *ff = (ffmpeg_in_t *)self->data;
	int max_samples;
	int available;
	int count;
	int i;

	max_samples = (int)(olen / sizeof(int16_t));
	if (max_samples < 1)
		return 0;

	while (ff->pcm_pos >= ff->pcm_samples) {
		int pulled = ices_ffmpeg_pull(ff);

		if (pulled < 0)
			return -1;
		if (pulled == 0)
			return 0;
	}

	available = ff->pcm_samples - ff->pcm_pos;
	count = available < max_samples ? available : max_samples;
	for (i = 0; i < count; i++) {
		int base = (ff->pcm_pos + i) * ff->channels;

		left[i] = ff->pcm[base];
		right[i] = ff->channels > 1 ? ff->pcm[base + 1] : ff->pcm[base];
	}
	ff->pcm_pos += count;
	return count;
}

static int ices_ffmpeg_close(input_stream_t *self) {
	ffmpeg_in_t *ff = (ffmpeg_in_t *)self->data;

	swr_free(&ff->swr);
	avcodec_free_context(&ff->codec);
	avformat_close_input(&ff->fmt);
	av_packet_free(&ff->pkt);
	av_frame_free(&ff->frame);
	free(ff->pcm);
	free(ff);
	if (self->fd >= 0) {
		close(self->fd);
		self->fd = -1;
	}
	return 0;
}
