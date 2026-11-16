## Process this with automake to create Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src conf

EXTRA_DIST = COPYING README.md

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
