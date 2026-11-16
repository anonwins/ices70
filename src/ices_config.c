/* parse.c
 * - Functions for xml file parsing
 * Copyright (c) 2000 Alexander Havang
 * Copyright (c) 2001-4 Brendan Cully
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
 * $Id: ices_config.c 8704 2005-01-09 23:10:32Z brendan $
 */

#include "definitions.h"

#define xmlstrcmp(a,b) strcmp((char *)a, b)
#define xmlstrcasecmp(a,b) strcasecmp((char *)a,b)

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

/* Private function declarations */
static int parse_file(const char *configfile, ices_config_t *ices_config);
static void parse_playlist_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur, ices_config_t *ices_config);
static void parse_execution_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur, ices_config_t *ices_config);
static void parse_server_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur,
			      ices_stream_t *ices_config);
static void parse_stream_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur, ices_stream_t *stream);
static char* ices_xml_read_node(xmlDocPtr doc, xmlNodePtr node);

/* Global function definitions */

/* Top level XML configfile parser */
int ices_xml_parse_config_file(ices_config_t *ices_config, const char *configfile) {
	char namespace[1024];

	// Ensure the config file can be opened
	if (!ices_util_verify_file(configfile)) {
		ices_log_error("XML Parser Error: Could not open configfile. [%s]  Error: [%s]", configfile, ices_util_strerror(errno, namespace, 1024));
		return 0; // The config file did not open
	}

	// Parse the file
	LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);
	return parse_file(configfile, ices_config);
}

/* Parse the configuration document */
static int parse_file(const char *configfile, ices_config_t *ices_config) {
	xmlDocPtr doc;
	xmlNsPtr ns;
	xmlNodePtr cur;
	int nstreams = 0;
	ices_stream_t* stream = ices_config->streams;

	// Parse the document
	if (!(doc = xmlParseFile(configfile))) {
		ices_log_error("XML Parser: Error while parsing %s", configfile);
		return 0; // The document did not parse
	}

	// Get the root element
	if (!(cur = xmlDocGetRootElement(doc))) {
		ices_log_error("XML Parser: Empty documenent %s", configfile);
		xmlFreeDoc(doc);
		return 0; // The document was empty
	}

	// Ensure the ices namespace is present
	if (!(ns = xmlSearchNsByHref(doc, cur, (unsigned char *) "http://www.icecast.org/projects/ices"))) {
		ices_log_error("XML Parser: Document of invalid type, no ices namespace found");
		xmlFreeDoc(doc);
		return 0; // The ices namespace was missing
	}

	// Ensure the root is Configuration
	if (!cur->name || (xmlstrcmp(cur->name, "Configuration"))) {
		ices_log_error("XML Parser: Document of invalid type, root node is not 'Configuration'");
		xmlFreeDoc(doc);
		return 0; // The root was not Configuration
	}

	// Walk the top-level nodes
	cur = cur->xmlChildrenNode;

#ifdef HAVE_LIBXML_PARSER_H
	while (cur && xmlIsBlankNode(cur))
		cur = cur->next;

#endif

	if (cur == NULL) {
		xmlFreeDoc(doc);
		return 1; // The document had no configuration
	}

	for (; cur != NULL; cur = cur->next) {
		if (cur->type == XML_COMMENT_NODE)
			continue; // Skip comment: It is not a setting
		if (xmlstrcmp(cur->name, "Stream") == 0) {
			// Ensure this is the only stream
			if (nstreams) {
				ices_log("Only one Stream is supported");
				xmlFreeDoc(doc);
				ices_setup_shutdown();
			}

			parse_stream_node(doc, ns, cur->xmlChildrenNode, stream);
			nstreams++;
		} else if (xmlstrcmp(cur->name, "Playlist") == 0)
			parse_playlist_node(doc, ns, cur->xmlChildrenNode, ices_config);
		else if (xmlstrcmp(cur->name, "Execution") == 0)
			parse_execution_node(doc, ns, cur->xmlChildrenNode, ices_config);
		else
			ices_log("Unknown Node: %s", cur->name);
	}

	// Free the document
	xmlFreeDoc(doc);
	return 1;
}

/* Parse the stream specific configuration */
static void parse_stream_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur,
			      ices_stream_t *stream) {
	for (; cur; cur = cur->next) {
		if (cur->type == XML_COMMENT_NODE)
			continue; // Skip comment: It is not a setting

		if (xmlstrcmp(cur->name, "Server") == 0)
			parse_server_node(doc, ns, cur->xmlChildrenNode, stream);
		else if (xmlstrcmp(cur->name, "Name") == 0) {
			ices_util_free(stream->name);
			stream->name = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Genre") == 0) {
			ices_util_free(stream->genre);
			stream->genre = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Description") == 0) {
			ices_util_free(stream->description);
			stream->description = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "URL") == 0) {
			ices_util_free(stream->url);
			stream->url = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Mountpoint") == 0) {
			ices_util_free(stream->mount);
			stream->mount = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Bitrate") == 0)
			stream->bitrate = atoi(ices_xml_read_node(doc, cur));
		else if (xmlstrcmp(cur->name, "Public") == 0)
			stream->ispublic = atoi(ices_xml_read_node(doc, cur));
		else if (xmlstrcmp(cur->name, "Reencode") == 0) {
			int res = atoi(ices_xml_read_node(doc, cur));

#ifndef HAVE_LIBLAME
			if (res == 1) {
				ices_log("Support for reencoding with liblame was not found. You can't reencode this.");
				ices_setup_shutdown();
			}
#endif

			stream->reencode = res;
		} else if (xmlstrcmp(cur->name, "Samplerate") == 0)
			stream->out_samplerate = atoi(ices_xml_read_node(doc, cur));
		else if (xmlstrcmp(cur->name, "Channels") == 0)
			stream->out_numchannels = atoi(ices_xml_read_node(doc, cur));
		else
			ices_log("Unknown Stream keyword: %s", cur->name);
	}
}

/* Parse the server specific configuration */
static void parse_server_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur,
			      ices_stream_t *ices_config) {
	for (; cur; cur = cur->next) {
		if (cur->type == XML_COMMENT_NODE)
			continue; // Skip comment: It is not a setting

		if (xmlstrcmp(cur->name, "Port") == 0)
			ices_config->port = atoi(ices_xml_read_node(doc, cur));
		else if (xmlstrcmp(cur->name, "Hostname") == 0) {
			ices_util_free(ices_config->host);
			ices_config->host = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Username") == 0) {
			ices_util_free(ices_config->user);
			ices_config->user = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Password") == 0) {
			ices_util_free(ices_config->password);
			ices_config->password = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Protocol") == 0) {
			unsigned char *str = (unsigned char *)ices_xml_read_node(doc, cur);

			// Ensure the protocol is HTTP
			if (!str || xmlstrcasecmp(str, "http") != 0) {
				ices_log("Protocol must be http");
				ices_setup_shutdown();
			}
		} else
			ices_log("Unknown Server keyword: %s", cur->name);
	}
}

/* Parse the execution specific options */
static void parse_execution_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur,
				 ices_config_t *ices_config) {
	for (; cur; cur = cur->next) {
		if (cur->type == XML_COMMENT_NODE)
			continue; // Skip comment: It is not a setting

		// Ignore CueFile (configs already on disk still contain it)
		if (xmlstrcmp(cur->name, "CueFile") == 0) {
		} else if (xmlstrcmp(cur->name, "BaseDirectory") == 0) {
			if (ices_config->base_directory)
				ices_config->base_directory =
					ices_util_strdup(ices_xml_read_node(doc, cur));
		} else
			ices_log("Unknown Execution keyword: %s", cur->name);
	}
}

/* Parse the playlist specific configuration */
static void parse_playlist_node(xmlDocPtr doc, xmlNsPtr ns, xmlNodePtr cur, ices_config_t *ices_config) {
	int i;

	for (; cur; cur = cur->next) {
		if (cur->type == XML_COMMENT_NODE)
			continue; // Skip comment: It is not a setting

		if (xmlstrcmp(cur->name, "Module") == 0) {
			ices_util_free(ices_config->pm.module);
			ices_config->pm.module = ices_util_strdup(ices_xml_read_node(doc, cur));
		} else if (xmlstrcmp(cur->name, "Crossfade") == 0) {
			if ((i = atoi(ices_xml_read_node(doc, cur))) > 0)
				ices_config->plugins = crossfade_plugin(i);
			/* TODO: stack plugins */
		} else if (xmlstrcmp(cur->name, "MinCrossfade") == 0) {
			if ((i = atoi(ices_xml_read_node(doc, cur))) > 0) {
				ices_plugin_t *plugin = ices_config->plugins;
				while (plugin) {
					if (strcmp(plugin->name, "crossfade") == 0) {
						int plugret = plugin->options(CFOPT_FADEMINLEN, &i);
						if (plugret < 0) ices_log("Invalid plugin option value for: %s", cur->name);
						break;
					}
					plugin = plugin->next;
				}
				if (plugin == NULL) ices_log("Option specified for non-registered plugin: %s", cur->name);
			}
		} else if (xmlstrcmp(cur->name, "CrossMix") == 0) {
			if ((i = atoi(ices_xml_read_node(doc, cur))) > 0) {
				ices_plugin_t *plugin = ices_config->plugins;
				while (plugin) {
					if (strcmp(plugin->name, "crossfade") == 0) {
						int plugret = plugin->options(CFOPT_CROSSMIX, &i);
						if (plugret < 0) ices_log("Invalid plugin option value for: %s", cur->name);
						break;
					}
					plugin = plugin->next;
				}
				if (plugin == NULL) ices_log("Option specified for non-registered plugin: %s", cur->name);
			}
		} else
			ices_log("Unknown playlist keyword: %s", cur->name);
	}
}

static char* ices_xml_read_node(xmlDocPtr doc, xmlNodePtr node) {
	return (char *) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
}
