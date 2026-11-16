## Process this with automake to create Makefile.in

SUBDIRS = playlist

bin_PROGRAMS = ices70

noinst_HEADERS = icestypes.h definitions.h setup.h log.h stream.h util.h \
	metadata.h in_vorbis.h mp3.h in_mp4.h in_flac.h id3.h signals.h \
	reencode.h replaygain.h ices_config.h

ices70_SOURCES = ices.c log.c setup.c stream.c util.c mp3.c metadata.c \
	id3.c signals.c crossfade.c replaygain.c

EXTRA_ices70_SOURCES = ices_config.c reencode.c in_vorbis.c in_mp4.c in_flac.c

ices70_LDADD = $(ICES_OBJECTS) playlist/libplaylist.a
ices70_DEPENDENCIES = $(ices70_LDADD)

AM_CPPFLAGS = -DICES_ETCDIR=\"$(sysconfdir)\"
