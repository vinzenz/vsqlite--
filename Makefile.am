LIBVSQLITE_VERSION=0.3.4
ACLOCAL_AMFLAGS= -I m4
lib_LTLIBRARIES = libvsqlitemm.la
libvsqlitemm_la_CPPFLAGS = -I include
libvsqlitemm_la_LDFLAGS = -lsqlite3 -Wl,--as-needed -Wl,-soname,libvsqlitemm.so.0 -version-info 3:0:3
libvsqlitemm_la_SOURCES = src/sqlite/command.cpp src/sqlite/connection.cpp src/sqlite/execute.cpp src/sqlite/query.cpp src/sqlite/result.cpp src/sqlite/transaction.cpp src/sqlite/view.cpp

#dist_noinst_SCRIPTS = autogen.sh

