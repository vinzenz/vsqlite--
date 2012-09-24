LIBVSQLITE_VERSION=`cat VERSION`
ACLOCAL_AMFLAGS= -I m4
lib_LTLIBRARIES = libvsqlitepp.la
libvsqlitepp_la_CPPFLAGS = -I include
libvsqlitepp_la_LDFLAGS = -lsqlite3 -Wl,--as-needed -Wl,-soname -Wl,libvsqlitepp.so.0 -version-info 3:0:0
libvsqlitepp_la_SOURCES = src/sqlite/command.cpp src/sqlite/connection.cpp src/sqlite/execute.cpp src/sqlite/query.cpp src/sqlite/result.cpp src/sqlite/transaction.cpp src/sqlite/view.cpp

#dist_noinst_SCRIPTS = autogen.sh

