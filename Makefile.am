LIBVSQLITE_VERSION=`cat VERSION`
AUTOMAKE_OPTIONS = subdir-objects foreign
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
AM_CXXFLAGS = -Wall -O3

lib_LTLIBRARIES = libvsqlitepp.la

libvsqlitepp_la_CPPFLAGS = -I include
libvsqlitepp_la_LDFLAGS = -lsqlite3 -Wl,--as-needed -Wl,-soname -Wl,libvsqlitepp.so.3 -version-info 3:0:0
libvsqlitepp_la_SOURCES = \
	src/sqlite/command.cpp \
	src/sqlite/connection.cpp \
	src/sqlite/execute.cpp \
	src/sqlite/query.cpp \
	src/sqlite/result.cpp \
	src/sqlite/transaction.cpp \
	src/sqlite/view.cpp \
	$(NULL)

SUBDIRS=include

EXTRA_DIST=Doxyfile build examples
