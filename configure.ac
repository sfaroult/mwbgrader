#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([mwbgrader], [1.0], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_SRCDIR([src])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
#AC_SEARCH_LIBS([sqlite3_open], [sqlite3], [], [
                  #AC_MSG_ERROR([unable to find the sqlite3 library])
                  #])
#AC_SEARCH_LIBS([xmlTextReaderRead], [xml2], [], [
                  #AC_MSG_ERROR([unable to find the libxml2 library])
                  #])
PKG_CHECK_MODULES([SQLITE], [sqlite3 >= 3])
PKG_CHECK_MODULES([LIBXML2], [libxml-2.0 >= 2])


# Checks for header files.
AC_CHECK_HEADERS([locale.h stdlib.h string.h unistd.h utime.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_HEADER_STDBOOL

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset setlocale sqrt strcasecmp strchr strdup utime])

AC_OUTPUT(Makefile src/Makefile)
