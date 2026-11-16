# configure script for ices70
# $Id: configure.ac 8764 2005-01-16 18:47:32Z brendan $

m4_define(ICES_MAJOR, 70)
m4_define(ICES_MINOR, 0)
m4_define(ICES_MICRO, 0)
m4_define(ICES_VERSION, ICES_MAJOR.ICES_MINOR.ICES_MICRO)

AC_INIT([ices70], ICES_VERSION, [icecast@xiph.org])
AC_PREREQ(2.52)
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/ices.c])

AC_CONFIG_FILES([Makefile src/Makefile src/playlist/Makefile conf/Makefile
 conf/ices70.conf.dist])

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
    AC_CHECK_LIB(mp3lame, lame_init, [
        have_LAME="yes"
        LIBS="$LIBS -lmp3lame"
        LIBM="-lm"
        ICES_OBJECTS="$ICES_OBJECTS reencode.o"
        AC_DEFINE(HAVE_LIBLAME, 1, [Define if you have the LAME MP3 library])
      ], [have_LAME="no"], -lm)
  fi
fi

if test "$have_LAME" != "yes"
then
  AC_MSG_ERROR([Could not find a valid LAME library installation])
fi

dnl Every file that is not a copied MP3 is decoded with FFmpeg.
if pkg-config --exists libavformat libavcodec libswresample libavutil
then
  CFLAGS="$CFLAGS `pkg-config --cflags libavformat libavcodec libswresample libavutil`"
  LIBS="$LIBS `pkg-config --libs libavformat libavcodec libswresample libavutil`"
  ICES_OBJECTS="$ICES_OBJECTS in_ffmpeg.o"
else
  AC_MSG_ERROR([FFmpeg libraries were not found (libavformat, libavcodec, libswresample, libavutil).])
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
AC_MSG_RESULT([  FFmpeg  : yes])
