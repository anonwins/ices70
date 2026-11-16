# configure script for ices
# $Id: configure.ac 8764 2005-01-16 18:47:32Z brendan $

m4_define(ICES_MAJOR, 0)
m4_define(ICES_MINOR, 4)
m4_define(ICES_MICRO, 11)
m4_define(ICES_VERSION,
  m4_if(ICES_MICRO, 0, ICES_MAJOR.ICES_MINOR, ICES_MAJOR.ICES_MINOR.ICES_MICRO))

AC_INIT([ices], ICES_VERSION, [icecast@xiph.org])
AC_PREREQ(2.52)
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/ices.c])

AC_CONFIG_FILES([Makefile src/Makefile src/playlist/Makefile conf/Makefile
 conf/ices.conf.dist])

VERSION=ICES_VERSION

dnl -- Finally, actual autoconf stuff --

AC_ARG_PROGRAM

dnl Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

if test "$GCC" = yes
then
  CFLAGS="$CFLAGS -Wall"
fi

dnl -- System check --

AC_CANONICAL_HOST
AC_USE_SYSTEM_EXTENSIONS

case $host in
  *hpux*)
    CFLAGS="$CFLAGS -D_HPUX_SOURCE -DHPUX"
    if test "$CC" = cc
    then
      CFLAGS="$CFLAGS -Aa"
    fi
  ;;
esac

dnl -- Compiler feature check --

AC_C_CONST
AC_C_BIGENDIAN
AC_C_INLINE

dnl -- System header check --

AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([errno.h fcntl.h signal.h sys/signal.h sys/socket.h \
  sys/stat.h sys/time.h sys/types.h unistd.h])

AC_TYPE_PID_T
AC_TYPE_SIZE_T

dnl -- System function check --

AC_FUNC_STRFTIME
AC_CHECK_FUNCS([vsnprintf setsid setlinebuf])

dnl -- Build system init --

AM_INIT_AUTOMAKE([1.6 foreign])
AM_MAINTAINER_MODE
AC_CONFIG_HEADERS([config.h])
LT_INIT
AC_SUBST(LIBTOOL_DEPS)

dnl -- Required extra libraries --

XIPH_PATH_SHOUT(, [AC_MSG_ERROR([Could not find a usable libshout])])
CFLAGS="$CFLAGS $SHOUT_CPPFLAGS $SHOUT_CFLAGS"
LIBS="$LIBS $SHOUT_LIBS"

dnl -- Optional features --
AC_CACHE_SAVE

AC_ARG_WITH(xml-config,
  [  --with-xml-config=PATH  use xml-config in PATH to find libxml ])

have_xml="no"
if test "$with_xml_config" != "no"
then
  if test -n "$with_xml_config" -a "$with_xml_config" != "yes"
  then
    XMLCONFIG="$with_xml_config"
    if ! test -x "$XMLCONFIG"
    then
      AC_MSG_ERROR([$with_xml_config cannot be run])
    fi
  else
    AC_CHECK_PROGS(XMLCONFIG, [xml2-config xml-config])
  fi

  if test -n "$XMLCONFIG"
  then
    AC_DEFINE(HAVE_LIBXML, 1, [Define if you have the GNOME XML library])
    LIBS="$LIBS `$XMLCONFIG --libs`"
    CPPFLAGS="$CPPFLAGS `$XMLCONFIG --cflags`"
    ICES_OBJECTS="$ICES_OBJECTS ices_config.o"
    have_xml="yes"
  else
    AC_MSG_ERROR([libxml2 is required])
  fi
fi

if test "$have_xml" != "yes"
then
  AC_MSG_ERROR([libxml2 is required])
fi

AC_ARG_WITH(lame,
  [[  --with-lame[=DIR]       enable support for reencoding with lame [in DIR]]])

have_LAME="no"
if test "$with_lame" != "no"
then
  if test -n "$with_lame" -a "$with_lame" != "yes"
  then
    CPPFLAGS="$CPPFLAGS -I$with_lame/include"
    LDFLAGS="$LDFLAGS -L$with_lame/lib"
  fi

  AC_CHECK_HEADER(lame/lame.h, [
    have_LAME="maybe",
    AC_DEFINE(HAVE_LAME_LAME_H, 1, [Use <lame/lame.h> instead of <lame.h>])
    ], [
    AC_CHECK_HEADER(lame.h, [
    have_LAME="maybe"])])

  if test "$have_LAME" != "no"
  then
    AC_CHECK_LIB(mp3lame, lame_encode_flush_nogap, [
      AC_CHECK_LIB(mp3lame, hip_decode_init, [
        have_LAME="yes"
        LIBS="$LIBS -lmp3lame"
        LIBM="-lm"
        ICES_OBJECTS="$ICES_OBJECTS reencode.o"
        AC_DEFINE(HAVE_LIBLAME, 1, [Define if you have the LAME MP3 library])
      ], [have_LAME="no"], -lm)
      ], [have_LAME="no"], -lm)
  fi
fi

if test "$have_LAME" != "yes"
then
  AC_MSG_ERROR([Could not find a valid LAME library installation])
fi

AC_ARG_WITH(vorbis,
  [[  --with-vorbis[=DIR]     support for vorbis transcoding using libvorbisfile [in DIR]]])

have_vorbis="no"
if test "$with_vorbis" != "no"
then
  if test -n "$with_vorbis" -a "$with_vorbis" != "yes"
  then
    CPPFLAGS="$CPPFLAGS -I$with_vorbis/include"
    LDFLAGS="$LDFLAGS -L$with_vorbis/lib"
  fi

  AC_CHECK_HEADER(vorbis/vorbisfile.h, have_vorbis="maybe")
  if test "$have_vorbis" != "no"
  then
    AC_CHECK_LIB(vorbisfile, ov_open, [
      LIBS="$LIBS -lvorbisfile -lvorbis -logg"
      AC_DEFINE(HAVE_LIBVORBISFILE, 1, [Define if you have libvorbisfile])
      ICES_OBJECTS="$ICES_OBJECTS in_vorbis.o"
      have_vorbis="yes"
    ],[have_vorbis="no"],-lvorbis -logg)
  fi
fi

if test "$have_vorbis" != "yes"
then
  AC_MSG_ERROR([Could not find libvorbisfile])
fi

AC_ARG_WITH(faad,
  [[  --with-faad[=DIR]     support for MP4 audio decoding using FFmpeg]])

if test "$have_LAME" != "yes" -a "$with_faad" != "no"
then
  AC_MSG_ERROR([MP4 decoding requires LAME])
fi

have_faad="no"
if test "$with_faad" != "no"
then
  dnl MP4 audio is demuxed and decoded with FFmpeg. libmp4v2 is not in Ubuntu.
  if pkg-config --exists libavformat libavcodec libswresample libavutil
  then
    have_faad="yes"
    CFLAGS="$CFLAGS `pkg-config --cflags libavformat libavcodec libswresample libavutil`"
    LIBS="$LIBS `pkg-config --libs libavformat libavcodec libswresample libavutil`"
    AC_DEFINE(HAVE_LIBFAAD, 1, [Define if MP4 decoding via FFmpeg is available])
    ICES_OBJECTS="$ICES_OBJECTS in_mp4.o"
  else
    have_faad="no"
  fi
fi

if test "$with_faad" != "no" -a "$have_faad" != "yes"
then
  AC_MSG_ERROR([FFmpeg libraries for MP4 decoding were not found (libavformat, libavcodec, libswresample, libavutil). Refusing to build ices0 without MP4 support.])
fi

AC_ARG_WITH(flac,
  [[  --with-flac[=DIR]     support for FLAC transcoding using libFLAC [in DIR]]])

have_flac="no"
if test "$with_flac" != "no"
then
  if test -n "$with_flac" -a "$with_flac" != "yes"
  then
    CPPFLAGS="$CPPFLAGS -I$with_flac/include"
    LDFLAGS="$LDFLAGS -L$with_flac/lib"
  fi

  AC_CHECK_HEADER([FLAC/stream_decoder.h], have_flac="maybe")
  if test "$have_flac" != "no"
  then
    AC_CHECK_LIB(FLAC, FLAC__stream_decoder_new, [
      LIBS="$LIBS -lFLAC"
      AC_DEFINE(HAVE_LIBFLAC, 1, [Define if you have libFLAC])
      ICES_OBJECTS="$ICES_OBJECTS in_flac.o"
      have_flac="yes"
    ],[have_flac="no"])
  fi
fi

if test "$have_flac" != "yes"
then
  AC_MSG_ERROR([Could not find libFLAC])
fi

dnl -- and finish up --

LIBS="$LIBS $LIBM $LIBDL"
AC_SUBST(ICES_OBJECTS)

AC_OUTPUT

AC_MSG_RESULT([Compiling with: $CPPFLAGS $CFLAGS])
AC_MSG_RESULT([Linking with: $LDFLAGS $LIBS])
AC_MSG_RESULT([Features:])
AC_MSG_RESULT([  XML     : $have_xml])
AC_MSG_RESULT([  LAME    : $have_LAME])
AC_MSG_RESULT([  Vorbis  : $have_vorbis])
AC_MSG_RESULT([  MP4     : $have_faad])
AC_MSG_RESULT([  FLAC    : $have_flac])
