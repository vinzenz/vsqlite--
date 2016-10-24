AC_INIT([libvsqlite++],[0.3.13], [evilissimo@gmail.com], [vsqlite++], [http://github.com/vinzenz/vsqlite--])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CXX
AC_PROG_LIBTOOL
LT_INIT
dnl -lboost_system -lboost_filesystem

BOOST_REQUIRE([1.33.0])
BOOST_FILESYSTEM

dnl -------------------------------------------
dnl test "$prefix" = "NONE" && prefix=/usr/local
dnl test "$exec_prefix" = "NONE" && exec_prefix=/usr/local
dnl test "$sharedstatedir" = '${prefix}/com' && sharedstatedir=/var
dnl test "$localstatedir" = '${prefix}/var' && localstatedir=/var
dnl
dnl if test "$prefix" = /usr; then
dnl    if test "${sysconfdir}" = "\${prefix}/etc"; then
dnl        sysconfdir=/etc
dnl    fi
dnl    if test "$sharedstatedir" = '\${prefix}/com'; then
dnl        sharedstatedir=/var
dnl    fi
dnl    if test "$localstatedir" = '\${prefix}/var'; then
dnl        localstatedir=/var
dnl    fi
dnl fi
dnl
dnl if test "$libdir" = "\${exec_prefix}/lib"; then
dnl     if test -e ${exec_prefix}/lib64; then
dnl         libdir='${exec_prefix}/lib64'
dnl     fi
dnl fi
dnl -------------------------------------------

AC_CONFIG_FILES([Makefile include/Makefile])

AC_OUTPUT

dnl -------------------------------------------
dnl echo "prefix..........."${prefix}
dnl echo "exec_prefix......"${exec_prefix}
dnl echo "bindir..........."${bindir}
dnl echo "libdir..........."${libdir}
dnl echo "sharedstatedir..."${sharedstatedir}
dnl echo "localstatedir...."${localstatedir}
dnl echo "sysconfdir......."${sysconfdir}
dnl echo "oldsysconfdir...."${old_sys_conf_dir}
dnl -------------------------------------------
