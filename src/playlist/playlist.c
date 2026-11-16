/* playlist.c
 * - Functions for playlist handling
 * Copyright (c) 2000 Alexander Hav�ng
 * Copyright (c) 2001-2 Brendan Cully
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
#include "playlist.h"

extern ices_config_t ices_config;
static int playlist_init = 0;

/* Global function definitions */

/* Wrapper for the playlist handler's next file function.
 * Remember that if this returns non-NULL then the return
 * value is free()ed by the caller. */
char *ices_playlist_get_next(void) {
	// Return the next path (the caller frees it)
	return ices_config.pm.get_next();
}

/* Allows a script to override file metadata if it wants. Returns NULL
 *   to mean 'do it yourself'. Modules need not implement this. */
char*ices_playlist_get_metadata(void) {
	// Return the script title when the module provides one
	if (ices_config.pm.get_metadata)
		return ices_config.pm.get_metadata();

	return NULL; // The script did not provide a title
}

/* Initialize the toplevel playlist handler */
int ices_playlist_initialize(void) {
	int rc;

	ices_log_debug("Initializing playlist handler...");

	// Start the script playlist handler
	rc = ices_playlist_script_initialize(&ices_config.pm);

	// Ensure the script handler started
	if (rc < 1) {
		ices_log("Initialization of playlist handler failed. [%s]", ices_log_get_error());
		ices_setup_shutdown();
	}

	playlist_init = 1;
	return rc;
}

/* Shutdown the playlist handler */
void ices_playlist_shutdown(void) {
	// Shut the script handler down when it was started
	if (playlist_init)
		ices_config.pm.shutdown();
}
