/* setup.c
 * - Functions for initialization in ices
 * Copyright (c) 2000 Alexander Havang
 * Copyright (c) 2002-4 Brendan Cully <brendan@xiph.org>
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
#include "metadata.h"

/* Local function declarations */
static void ices_setup_parse_options(ices_config_t *ices_config);
static void ices_setup_parse_defaults(ices_config_t *ices_config);
#ifdef HAVE_LIBXML
static void ices_setup_parse_config_file(ices_config_t *ices_config, const char *configfile);
#endif
static void ices_setup_parse_command_line(ices_config_t *ices_config, char **argv, int argc);
static void ices_setup_parse_command_line_for_new_configfile(ices_config_t *ices_config, char **argv, int argc);
static void ices_setup_activate_libshout_changes(const ices_config_t *ices_config);
static void ices_setup_usage(void);
static void ices_free_all(ices_config_t *ices_config);

extern ices_config_t ices_config;

/* Global function definitions */

/* Top level initialization function for ices.
 * It will parse options and initialize modules. */
void ices_setup_initialize(void) {
	ices_stream_t* stream;
	ices_plugin_t* plugin;

	shout_init();

	// Install the signal handlers
	ices_signals_setup();

	// Read the config file and the command line
	ices_setup_parse_options(&ices_config);

	// Open the logfile
	ices_log_initialize();

	// Create one libshout connection
	for (stream = ices_config.streams; stream; stream = stream->next) {
		if (!(stream->conn = shout_new())) {
			ices_log("Could not create shout interface");
			ices_setup_shutdown();
		}
	}

	// Copy the settings onto the libshout connection
	ices_setup_activate_libshout_changes(&ices_config);

	// Start the playlist handler
	ices_playlist_initialize();

#ifdef HAVE_LIBLAME
	// Initialize liblame for reencoding
	ices_reencode_initialize();

	// Start the crossfade plugin when one was configured
	while (ices_config.plugins && ices_config.plugins->init() < 0)
		ices_config.plugins = ices_config.plugins->next;

	for (plugin = ices_config.plugins; plugin && plugin->next; plugin = plugin->next)
		if (plugin->next->init() < 0)
			plugin->next = plugin->next->next;
#endif

	ices_log_debug("Startup complete\n");
}

/* Top level ices shutdown function.
 * This is the _only_ way out of here */
void ices_setup_shutdown(void) {
	ices_stream_t* stream;
	ices_plugin_t* plugin;

	// Disconnect libshout
	for (stream = ices_config.streams; stream; stream = stream->next)
		if (stream->conn)
			shout_close(stream->conn);

#ifdef HAVE_LIBLAME
	// Shut the crossfade plugin down
	for (plugin = ices_config.plugins; plugin; plugin = plugin->next)
		plugin->shutdown();

	// Shut the reencoding engine down
	ices_reencode_shutdown();
#endif

	// Shut the playlist handler down
	ices_playlist_shutdown();

	// Free the configuration
	ices_free_all(&ices_config);

	// Report the exit and close the logfile
	ices_log("Ices Exiting...");
	ices_log_shutdown();

	shout_shutdown();

	exit(1);
}

/* Local function definitions */

/* Top level option parsing function.
 * Sets of options object (ices_config), with:
 * - Hardcoded defaults
 * - Configfile settings
 * - Command line options */
static void ices_setup_parse_options(ices_config_t *ices_config) {
	// Apply the hardcoded defaults
	ices_setup_parse_defaults(ices_config);

	// Find -c before the config file is read
	ices_setup_parse_command_line_for_new_configfile(ices_config, ices_util_get_argv(), ices_util_get_argc());

#ifdef HAVE_LIBXML
	// Read the config file
	ices_setup_parse_config_file(ices_config, ices_config->configfile);
#endif

	// Apply the command line over the config file
	ices_setup_parse_command_line(ices_config, ices_util_get_argv(), ices_util_get_argc());
}

/* Function for placing hardcoded defaults in the
 * options object (ices_config) */
static void ices_setup_parse_defaults(ices_config_t *ices_config) {
	// Apply the process defaults
	ices_config->configfile = ices_util_strdup(ICES_DEFAULT_CONFIGFILE);
	ices_config->base_directory = ices_util_strdup(ICES_DEFAULT_BASE_DIRECTORY);
	ices_config->verbose = ICES_DEFAULT_VERBOSE;
	ices_config->reencode = ICES_DEFAULT_REENCODE;
	ices_config->pm.module = ices_util_strdup(ICES_DEFAULT_MODULE);

	// Allocate the single stream
	ices_config->streams = (ices_stream_t*) malloc(sizeof(ices_stream_t));
	ices_setup_parse_stream_defaults(ices_config->streams);
}

/* Place hardcoded defaults into an ices_stream_t object */
void ices_setup_parse_stream_defaults(ices_stream_t* stream) {
	// Apply the stream defaults
	stream->conn = NULL;
	stream->host = ices_util_strdup(ICES_DEFAULT_HOST);
	stream->port = ICES_DEFAULT_PORT;
	stream->user = ices_util_strdup(ICES_DEFAULT_USER);
	stream->password = ices_util_strdup(ICES_DEFAULT_PASSWORD);

	stream->mount = ices_util_strdup(ICES_DEFAULT_MOUNT);

	stream->name = ices_util_strdup(ICES_DEFAULT_NAME);
	stream->genre = ices_util_strdup(ICES_DEFAULT_GENRE);
	stream->description = ices_util_strdup(ICES_DEFAULT_DESCRIPTION);
	stream->url = ices_util_strdup(ICES_DEFAULT_URL);
	stream->ispublic = ICES_DEFAULT_ISPUBLIC;

	stream->bitrate = ICES_DEFAULT_BITRATE;
	stream->reencode = ICES_DEFAULT_REENCODE;
	stream->out_numchannels = -1;
	stream->out_samplerate = -1;

	stream->encoder_state = NULL;
	stream->connect_delay = 0;

	stream->next = NULL;
}

/* Frees ices_stream_t data (but not the object itself) */
static void ices_setup_free_stream(ices_stream_t* stream) {
	// Free the libshout connection and the stream strings
	if (stream->conn)
		shout_free(stream->conn);
	ices_util_free(stream->host);
	ices_util_free(stream->user);
	ices_util_free(stream->password);

	ices_util_free(stream->mount);

	ices_util_free(stream->name);
	ices_util_free(stream->genre);
	ices_util_free(stream->description);
	ices_util_free(stream->url);
}

/* Function to free() all allocated memory when ices shuts down. */
static void ices_free_all(ices_config_t *ices_config) {
	ices_stream_t *stream, *next;

	// Free the process strings
	ices_util_free(ices_config->configfile);
	ices_util_free(ices_config->base_directory);
	ices_util_free(ices_config->pm.module);

	// Free each stream
	for (stream = ices_config->streams; stream; stream = next) {
		next = stream->next;

		ices_setup_free_stream(stream);
		ices_util_free(stream);
	}
}

#ifdef HAVE_LIBXML
/* Tell the xml module to parse the config file. */
static void ices_setup_parse_config_file(ices_config_t *ices_config, const char *configfile) {
	char namespace[1024];
	const char *realname = NULL;
	int ret;

	// Find the config file (the given path, or the same name under the system directory)
	if (ices_util_verify_file(configfile))
		realname = configfile;
	else {
		sprintf(namespace, "%s/%s", ICES_ETCDIR, configfile);
		if (ices_util_verify_file(namespace))
			realname = &namespace[0];
	}

	// Read the file when it exists
	if (realname) {
		ret = ices_xml_parse_config_file(ices_config, realname);

		if (ret == -1)
			ices_log_debug("%s", ices_log_get_error());
		else if (ret == 0)
			ices_log("%s", ices_log_get_error());
	}
}
#endif

/* This function looks through the command line options for a new
 * configfile. */
static void ices_setup_parse_command_line_for_new_configfile(ices_config_t *ices_config, char **argv, int argc) {
	int arg;
	char *s;

	arg = 1;

	// Find -c (only the first letter is checked, so -station is not -c)
	while (arg < argc) {
		s = argv[arg];

		if (s[0] == '-') {
			switch (s[1]) {
			case 'c':
				// Ensure -c has a path
				if (arg + 1 >= argc) {
					fprintf(stderr, "Option c requires an argument!\n");
					ices_setup_shutdown();
				}
				arg++;
				if (ices_config->configfile)
					ices_util_free(ices_config->configfile);
				ices_config->configfile = ices_util_strdup(argv[arg]);
#ifndef HAVE_LIBXML
				fprintf(stderr, "Cannot use config file (no XML support).\n");
				ices_setup_shutdown();
#endif
				break;
			}
		}
		arg++;
	}
}

/* This function parses the command line options */
static void ices_setup_parse_command_line(ices_config_t *ices_config, char **argv,
					  int argc) {
	int arg;
	char *s;

	arg = 1;

	while (arg < argc) {
		s = argv[arg];

		if (s[0] == '-') {
			// Skip -station (the playlist module reads the name from this command line)
			if (strcmp(s, "-station") == 0) {
				if (arg + 1 >= argc) {
					fprintf(stderr, "Option -station requires an argument!\n");
					ices_setup_usage();
					ices_setup_shutdown();
				}
				arg++;
			} else {
				switch (s[1]) {
				case 'c':
					// Skip the config path (the early pass already stored it)
					if (arg + 1 >= argc) {
						fprintf(stderr, "Option c requires an argument!\n");
						ices_setup_usage();
						ices_setup_shutdown();
					}
					arg++;
					break;
				case 'M':
					// Save the playlist script path
					if (arg + 1 >= argc) {
						fprintf(stderr, "Option M requires an argument!\n");
						ices_setup_usage();
						ices_setup_shutdown();
					}
					arg++;
					ices_util_free(ices_config->pm.module);
					ices_config->pm.module = ices_util_strdup(argv[arg]);
					break;
				case 'S':
					// Ensure the playlist type is script
					if (arg + 1 >= argc) {
						fprintf(stderr, "Option S requires an argument!\n");
						ices_setup_usage();
						ices_setup_shutdown();
					}
					arg++;
					if (strcmp(argv[arg], "script") != 0) {
						fprintf(stderr, "Playlist type must be script.\n");
						ices_setup_shutdown();
					}
					break;
				default:
					ices_setup_usage();
					ices_setup_shutdown();
					break;
				}
			}
		}
		arg++;
	}
}

/* This function takes all the new configuration and copies it to the
   libshout object. */
static void ices_setup_activate_libshout_changes(const ices_config_t *ices_config) {
	ices_stream_t* stream;
	shout_t* conn;
	int streamno = 0;
	char useragent[64];
	char bitrate[8];

	// Build the source agent string
	snprintf(useragent, sizeof(useragent), "ices/" VERSION " libshout/%s",
		 shout_version(NULL, NULL, NULL));

	// Copy each stream onto its libshout connection
	for (stream = ices_config->streams; stream; stream = stream->next) {
		conn = stream->conn;

		shout_set_host(conn, stream->host);
		shout_set_port(conn, stream->port);
		shout_set_user(conn, stream->user);
		shout_set_password(conn, stream->password);
		shout_set_content_format(conn, SHOUT_FORMAT_MP3, SHOUT_USAGE_AUDIO, NULL);
		shout_set_protocol(conn, SHOUT_PROTOCOL_HTTP);
		shout_set_meta(conn, SHOUT_META_NAME, stream->name);
		shout_set_meta(conn, SHOUT_META_URL, stream->url);
		shout_set_meta(conn, SHOUT_META_GENRE, stream->genre);
		shout_set_meta(conn, SHOUT_META_DESCRIPTION, stream->description);

		snprintf(bitrate, sizeof(bitrate), "%d", stream->bitrate);
		shout_set_audio_info(conn, SHOUT_AI_BITRATE, bitrate);

		shout_set_public(conn, stream->ispublic);
		shout_set_mount(conn, stream->mount);
		shout_set_agent(conn, useragent);

		ices_log_debug("Sending following information to libshout:");
		ices_log_debug("Stream: %d", streamno);
		ices_log_debug("Host: %s:%d (protocol: http)", shout_get_host(conn),
			       shout_get_port(conn));
		ices_log_debug("Mount: %s, User: %s, Password: %s", shout_get_mount(conn), shout_get_user(conn), shout_get_password(conn));
		ices_log_debug("Name: %s\tURL: %s",
			       shout_get_meta(conn, SHOUT_META_NAME),
			       shout_get_meta(conn, SHOUT_META_URL));
		ices_log_debug("Genre: %s\tDesc: %s",
			       shout_get_meta(conn, SHOUT_META_GENRE),
			       shout_get_meta(conn, SHOUT_META_DESCRIPTION));
		ices_log_debug("Bitrate: %s\tPublic: %d", shout_get_audio_info(conn, SHOUT_AI_BITRATE),
			       shout_get_public(conn));
		streamno++;
	}
}

/* Display the command line options ICING uses */
static void ices_setup_usage(void) {
	// Show the accepted command line
	printf("This is ices " VERSION "\n"
	       "ices -station <name> -c <configfile> -S script -M <script>\n"
	       "Options:\n"
	       "\t-station <name> (ignored here; the playlist script reads it)\n"
	       "\t-c <configfile>\n"
	       "\t-S script\n"
	       "\t-M <playlist script>\n");
}
