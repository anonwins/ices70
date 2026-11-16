/* metadata.c
 * Copyright (c) 2001-3 Brendan Cully <brendan@xiph.org>
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

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>

#define INITDELAY 500000

extern ices_config_t ices_config;

static char* Artist = NULL;
static char* Title = NULL;
static char* Filename = NULL;

/* Private function declarations */
static char* metadata_clean_filename(const char* path, char* buf,
				     size_t len);
static void metadata_update(int delay);
static int metadata_send(ices_stream_t* stream, const char* song);

/* Global function definitions */

void ices_metadata_get(char* artist, size_t alen, char* title, size_t tlen) {
	if (Artist)
		snprintf(artist, alen, "%s", Artist);
	if (Title)
		snprintf(title, tlen, "%s", Title);
}

void ices_metadata_set(const char* artist, const char* title) {
	ices_util_free(Artist);
	Artist = NULL;
	ices_util_free(Title);
	Title = NULL;

	if (artist && *artist)
		Artist = ices_util_strdup(artist);
	if (title && *title)
		Title = ices_util_strdup(title);
}

void ices_metadata_set_file(const char* filename) {
	char buf[1024];

	ices_util_free(Filename);
	Filename = NULL;

	if (filename && *filename) {
		metadata_clean_filename(filename, buf, sizeof(buf));
		Filename = ices_util_strdup(buf);
	}
}

/* Update metadata on server via fork.
 * Note that the very first metadata update after connection is delayed,
 * because if we try to update our new info to the server and the server has
 * not yet accepted us as a source, the information is lost. */
void ices_metadata_update(int delay) {
	pid_t child;

	if (delay)
		ices_log_debug("Delaying metadata update...");

	if ((child = fork()) == 0) {
		metadata_update(delay ? INITDELAY : 0);
		_exit(0);
	}

	if (child == -1)
		ices_log_debug("Metadata update failed: fork");
}

/* Percent-encode a query value. Slash is left as-is so a mount stays readable. */
static void metadata_url_encode(const char* src, char* dst, size_t dstlen) {
	static const char hex[] = "0123456789ABCDEF";
	size_t used = 0;

	if (!src || dstlen == 0) {
		if (dstlen)
			dst[0] = '\0';
		return;
	}
	for (; *src; src++) {
		unsigned char c = (unsigned char)*src;
		int plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
		size_t need = plain ? 1 : 3;

		if (used + need + 1 > dstlen)
			break;
		if (plain) {
			dst[used++] = (char)c;
		} else {
			dst[used++] = '%';
			dst[used++] = hex[c >> 4];
			dst[used++] = hex[c & 15];
		}
	}
	dst[used] = '\0';
}

static void metadata_base64(const char* src, char* dst, size_t dstlen) {
	static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t len = src ? strlen(src) : 0;
	size_t i = 0;
	size_t o = 0;

	while (i < len && o + 4 < dstlen) {
		unsigned int a = (unsigned char)src[i++];
		int have_b = i < len;
		unsigned int b = have_b ? (unsigned char)src[i++] : 0;
		int have_c = i < len;
		unsigned int c = have_c ? (unsigned char)src[i++] : 0;

		dst[o++] = tab[a >> 2];
		dst[o++] = tab[((a & 3) << 4) | (b >> 4)];
		dst[o++] = have_b ? tab[((b & 15) << 2) | (c >> 6)] : '=';
		dst[o++] = have_c ? tab[c & 63] : '=';
	}
	if (o < dstlen)
		dst[o] = '\0';
	else if (dstlen)
		dst[dstlen - 1] = '\0';
}

/* One request, then return. libshout retries /admin/!POKE forever when Icecast 2.5 answers 400. */
static int metadata_send(ices_stream_t* stream, const char* song) {
	char song_enc[1024];
	char mount_enc[512];
	char raw[768];
	char auth[1024];
	char request[4096];
	char port[16];
	struct addrinfo hints;
	struct addrinfo* res = NULL;
	struct addrinfo* item;
	struct timeval tv;
	int fd = -1;
	int sent;
	int rc = -1;

	if (!stream->host || !stream->user || !stream->password || !stream->mount || !song)
		return -1;

	metadata_url_encode(song, song_enc, sizeof(song_enc));
	metadata_url_encode(stream->mount, mount_enc, sizeof(mount_enc));
	snprintf(raw, sizeof(raw), "%s:%s", stream->user, stream->password);
	metadata_base64(raw, auth, sizeof(auth));
	snprintf(port, sizeof(port), "%d", stream->port);
	snprintf(request, sizeof(request),
		"GET /admin/metadata?mode=updinfo&mount=%s&song=%s HTTP/1.0\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: ices70/" VERSION "\r\n"
		"Authorization: Basic %s\r\n"
		"Connection: close\r\n\r\n",
		mount_enc, song_enc, stream->host, stream->port, auth);

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(stream->host, port, &hints, &res) != 0)
		return -1;

	tv.tv_sec = 2;
	tv.tv_usec = 0;
	for (item = res; item; item = item->ai_next) {
		fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
		if (fd < 0)
			continue;
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		if (connect(fd, item->ai_addr, item->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0)
		return -1;

	sent = 0;
	while (sent < (int)strlen(request)) {
		int n = send(fd, request + sent, strlen(request) - sent, 0);

		if (n <= 0)
			break;
		sent += n;
	}
	if (sent == (int)strlen(request)) {
		char reply[256];
		int got = recv(fd, reply, sizeof(reply) - 1, 0);

		if (got > 0) {
			reply[got] = '\0';
			if (strstr(reply, " 200 "))
				rc = 0;
		}
	}
	close(fd);
	return rc;
}

static void metadata_update(int delay) {
	ices_stream_t* stream;
	char song[1024];
	char* playlist_metadata;
	char* value;

	if (delay)
		usleep(delay);

	if (!(playlist_metadata = ices_playlist_get_metadata())) {
		if (Title) {
			if (Artist)
				snprintf(song, sizeof(song), "%s - %s", Artist, Title);
			else
				snprintf(song, sizeof(song), "%s", Title);
		} else if (Filename)
			snprintf(song, sizeof(song), "%s", Filename);
		else
			song[0] = '\0';

		value = song;
	} else
		value = playlist_metadata;

	for (stream = ices_config.streams; stream; stream = stream->next) {
		if (metadata_send(stream, value) != 0)
			ices_log_error_output("Updating metadata on %s failed.", stream->mount);
		else
			ices_log_debug("Updated metadata on %s to: %s", stream->mount, value);
	}

	ices_util_free(playlist_metadata);
}

/* Cleanup a filename so it looks more like a song name */
static char *metadata_clean_filename(const char* path, char *buf, size_t len) {
	const char *slash;
	char *dot;

	if (!path || !buf || len < 1) {
		ices_log("ERROR: Polluted strings sent to filename cleanup.");
		return NULL;
	}

	/* Find last slash */
	slash = strrchr(path, '/');

	if (slash && slash[0] != '\0')
		snprintf(buf, len, "%s", slash + 1);
	else
		snprintf(buf, len, "%s", path);

	if ((dot = strrchr(buf, '.')))
		*dot = '\0';

	ices_log_debug("Filename cleaned up from [%s] to [%s]", path, buf);
	return buf;
}
