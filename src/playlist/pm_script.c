/* pm_script.c
 * - playlist module for external scripts (� la IceS 2.0)
 * - based on playlist_script.c from IceS 2.0 by
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * Copyright (C) 2005 Ville Koskinen <ville.koskinen@iki.fi>
 * Copyright (c) 2009 Steve Blinch <centova.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define STR_BUFFER 1024

static char *playlist_metadata = NULL;
static char *cmd = NULL;
static pid_t child_pid = -1;
static int read_fd = -1;
static int write_fd = -1;
static int asked = 0;
static char accum[8192];
static size_t accum_len = 0;
static char *ready_path = NULL;

extern ices_config_t ices_config;

/* Private function declarations */
static char* playlist_script_get_next(void);
static void playlist_script_shutdown(void);
static char* playlist_script_get_metadata(void);
static int playlist_script_start(void);
static void playlist_script_stop(void);
static int playlist_script_extract(void);
static char **brk_string(register char *str, int *store_argc);

/* Global function definitions */

/* Initialize the script playlist handler */
int ices_playlist_script_initialize(playlist_module_t* pm) {
	char *bindir = NULL;

	ices_log_debug("Initializing script playlist handler...");

	// Ensure a playlist script was configured
	if (!pm->module) {
		ices_log_error("No playlist script defined");
		return 0; // No playlist script
	}

	// Resolve the script path (absolute, or next to the ices70 binary)
	cmd = pm->module;
	if (cmd[0] != '/' && !(cmd[0] == '.' && (cmd[1] == '/' || (cmd[1] == '.' && cmd[2] == '/')))) {
	    bindir = ices_util_get_bindir();
	    ices_log_debug("bindir is %s",bindir);
	    if (bindir) {
			cmd = malloc(strlen(pm->module) + strlen(bindir) + 2);
			if (cmd)
				sprintf(cmd, "%s/%s", bindir, pm->module);
	    		ices_log_debug("cmd is %s",cmd);
		}
	} else {
		cmd = strdup(pm->module);
   		ices_log_debug("cmd is unmodified as %s",cmd);
	}

	// Ensure the script path was stored
	if (!cmd) {
		ices_log_error_output("Could not allocate memory for playlist path");
		return 0; // The script path was not stored
	}

	// Register the script callbacks
	pm->get_next = playlist_script_get_next;
	pm->get_metadata = playlist_script_get_metadata;
	pm->shutdown = playlist_script_shutdown;

	// Start the helper once. A dead helper is started again from the stream loop
	if (!playlist_script_start())
		return 0;
	return 1;
}

static void playlist_script_stop(void) {
	if (child_pid > 0) {
		kill(child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	child_pid = -1;
	if (read_fd >= 0)
		close(read_fd);
	if (write_fd >= 0)
		close(write_fd);
	read_fd = -1;
	write_fd = -1;
	asked = 0;
	accum_len = 0;
	ices_util_free(ready_path);
	ready_path = NULL;
}

/* Start the helper and keep both pipes. Stderr stays the station log. */
static int playlist_script_start(void) {
	int to_child[2];
	int from_child[2];
	pid_t pid;
	char **execargv;
	int execargc;

	if (pipe(to_child) < 0 || pipe(from_child) < 0) {
		ices_log_error("Could not create the playlist pipes");
		return 0;
	}
	pid = fork();
	if (pid < 0) {
		ices_log_error("Could not start the playlist helper");
		return 0;
	}
	if (pid == 0) {
		execargv = brk_string(cmd, &execargc);
		close(to_child[1]);
		close(from_child[0]);
		dup2(to_child[0], STDIN_FILENO);
		dup2(from_child[1], STDOUT_FILENO);
		close(to_child[0]);
		close(from_child[1]);
		execv(execargv[0], execargv);
		_exit(127);
	}

	close(to_child[0]);
	close(from_child[1]);
	fcntl(from_child[0], F_SETFL, O_NONBLOCK);
	child_pid = pid;
	write_fd = to_child[1];
	read_fd = from_child[0];
	asked = 0;
	accum_len = 0;
	return 1;
}

/* Pull two complete lines out of the accumulator. Returns 1 when a path is ready. */
static int playlist_script_extract(void) {
	char *first;
	char *second;
	size_t path_len;
	size_t title_len;
	size_t used;

	first = memchr(accum, '\n', accum_len);
	if (!first)
		return 0;
	second = memchr(first + 1, '\n', accum_len - (size_t)(first + 1 - accum));
	if (!second)
		return 0;

	path_len = (size_t)(first - accum);
	title_len = (size_t)(second - (first + 1));
	if (path_len > 0 && accum[path_len - 1] == '\r')
		path_len--;
	if (title_len > 0 && first[title_len] == '\r')
		title_len--;

	ready_path = malloc(path_len + 1);
	if (!ready_path)
		return 0;
	memcpy(ready_path, accum, path_len);
	ready_path[path_len] = '\0';

	ices_util_free(playlist_metadata);
	playlist_metadata = malloc(title_len + 1);
	if (playlist_metadata) {
		memcpy(playlist_metadata, first + 1, title_len);
		playlist_metadata[title_len] = '\0';
	}

	used = (size_t)(second + 1 - accum);
	memmove(accum, second + 1, accum_len - used);
	accum_len -= used;
	asked = 0;
	if (!ready_path[0]) {
		ices_log_error("Playlist helper returned an empty path");
		ices_util_free(ready_path);
		ready_path = NULL;
		return 0;
	}
	return 1;
}

/* Write one newline when a track is needed and no answer is waiting */
void ices_playlist_ask(void) {
	char newline = '\n';

	if (child_pid <= 0 && !playlist_script_start())
		return;
	if (asked || ready_path)
		return;
	if (write(write_fd, &newline, 1) == 1)
		asked = 1;
	else if (errno == EPIPE) {
		playlist_script_stop();
		playlist_script_start();
	}
}

/* Read whatever the helper has written. 1 = a path is ready, 0 = still waiting, -1 = helper restarted. */
int ices_playlist_poll(void) {
	ssize_t got;

	if (ready_path)
		return 1;
	if (child_pid <= 0 && !playlist_script_start())
		return -1;

	got = read(read_fd, accum + accum_len, sizeof(accum) - accum_len - 1);
	if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return playlist_script_extract();
	if (got < 0)
		return 0;
	if (got == 0) {
		// End of file: the helper died
		playlist_script_stop();
		playlist_script_start();
		return -1;
	}
	accum_len += (size_t)got;
	accum[accum_len] = '\0';
	return playlist_script_extract();
}

/* The path from the last ready answer. The caller frees it. */
char *ices_playlist_take(void) {
	char *path = ready_path;

	ready_path = NULL;
	return path;
}

/* Drop the saved title so silence does not keep the previous song name */
void ices_playlist_clear_metadata(void) {
	ices_util_free(playlist_metadata);
	playlist_metadata = NULL;
}

static char *playlist_script_get_next(void) {
	ices_playlist_ask();
	if (ices_playlist_poll() == 1)
		return ices_playlist_take();
	return NULL;
}

static char *playlist_script_get_metadata(void) {
	if (playlist_metadata)
		return playlist_metadata;
	return NULL;
}

static void playlist_script_shutdown(void) {
	playlist_script_stop();
	ices_util_free(cmd);
	cmd = NULL;
	ices_playlist_clear_metadata();
}

/*-
 * Credits for brk_string() function:
 *
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * brk_string --
 *	Fracture a string into an array of words (as delineated by tabs or
 *	spaces) taking quotation marks into account.  Leading tabs/spaces
 *	are ignored.
 *
 * returns --
 *	Pointer to the array of pointers to the words.  To make life easier,
 *	the first word is always the value of the .MAKE variable.
 */
static char **brk_string(register char *str, int *store_argc) {
	static int argmax, curlen;
	static char **argv, *buf;
	register int argc, ch;
	register char inquote, *p, *start, *t;
	int len;

	/* save off pmake variable */
	if (!argv) {
		argv = (char **)malloc((argmax = 50) * sizeof(char *));
	}

	/* skip leading space chars. */
	for (; *str == ' ' || *str == '\t'; ++str);

	/* allocate room for a copy of the string */
	if ((len = strlen(str) + 1) > curlen)
		buf = malloc(curlen = len);

	/*
	 * copy the string; at the same time, parse backslashes,
	 * quotes and build the argument list.
	 */
	argc = 0;
	inquote = '\0';
	for (p = str, start = t = buf;; ++p) {
		switch(ch = *p) {
		case '"':
		case '\'':
			if (inquote)
				if (inquote == ch)
					inquote = '\0';
				else
					break;
			else
				inquote = ch;
			continue;
		case ' ':
		case '\t':
			if (inquote)
				break;
			if (!start)
				continue;
			/* FALLTHROUGH */
		case '\n':
		case '\0':
			/*
			 * end of a token -- make sure there's enough argv
			 * space and save off a pointer.
			 */
			*t++ = '\0';
			if (argc == argmax) {
				argmax *= 2;		/* ramp up fast */
				argv = (char **)realloc(argv,argmax * sizeof(char *));
			}
			argv[argc++] = start;
			start = (char *)NULL;
			if (ch == '\n' || ch == '\0')
				goto done;
			continue;
		case '\\':
			switch (ch = *++p) {
			case '\0':
			case '\n':
				/* hmmm; fix it up as best we can */
				ch = '\\';
				--p;
				break;
			case 'b':
				ch = '\b';
				break;
			case 'f':
				ch = '\f';
				break;
			case 'n':
				ch = '\n';
				break;
			case 'r':
				ch = '\r';
				break;
			case 't':
				ch = '\t';
				break;
			}
			break;
		}
		if (!start)
			start = t;
		*t++ = ch;
	}
done:	argv[argc] = (char *)NULL;
	*store_argc = argc;
	return(argv);
}