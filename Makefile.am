LIBVSQLITE_VERSION=`cat VERSION`
AUTOMAKE_OPTIONS = subdir-objects foreign
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
AM_CXXFLAGS = -W -Wall -pedantic -W -std=c++0x

lib_LTLIBRARIES = libvsqlitepp.la

libvsqlitepp_la_CXXFLAGS = -I $(srcdir)/include $(AM_CXXFLAGS)
libvsqlitepp_la_LDFLAGS = -Wl,--no-as-needed -lsqlite3 -lboost_system -lboost_filesystem -Wl,-soname -Wl,libvsqlitepp.so.3 -version-info 3:0:0 
libvsqlitepp_la_SOURCES = \
	src/sqlite/backup.cpp \
	src/sqlite/command.cpp \
	src/sqlite/connection.cpp \
	src/sqlite/savepoint.cpp \
	src/sqlite/execute.cpp \
	src/sqlite/query.cpp \
	src/sqlite/result.cpp \
	src/sqlite/transaction.cpp \
	src/sqlite/view.cpp \
	$(NULL)

noinst_PROGRAMS=vsqlitepp_example
vsqlitepp_example_CXXFLAGS=-I $(srcdir)/include $(AM_CXXFLAGS)
vsqlitepp_example_LDADD = libvsqlitepp.la
vsqlitepp_example_SOURCES = \
	examples/sqlite_wrapper.cpp \
	$(NULL)

SUBDIRS=include

EXTRA_DIST=Doxyfile build examples
