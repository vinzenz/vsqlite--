Name:		vsqlite++
Version:	0.3.0
Release:	1%{?dist}
Summary:	Well designed C++ sqlite 3.x wrapper library

Group:		Development/Libraries
License:	BSD
URL:		https://github.com/vinzenz/vsqlite--
Source0:	https://github.com/downloads/vinzenz/vsqlite--/%{name}-%{version}.tar.bz2

BuildRequires: premake, boost-devel, sqlite-devel
#Requires:	
#Provides:


%description
TODO

%package devel
Summary:        Development files for %{name}
Group:          Development/Libraries
Requires:       %{name} = %{version}-%{release}

%description devel
This package contains development files for %{name}.

%package static
Summary:        Static library for %{name}
Group:          Development/Libraries
Requires:       %{name} = %{version}-%{release}

%description static
This package contains the static version of libvsqlite++ for %{name}.



%prep
%setup -q


%build
cd build
premake4 gmake
make %{?_smp_mflags} config=release
./packaging.sh

%install
# static
mkdir -p %{buildroot}%{_libdir}
install -m 644 build/libvsqlite++.a %{buildroot}%{_libdir}
# devel 
mkdir -p %{buildroot}%{_libdir}
install -m 755 build/libvsqlite++.so %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}/sqlite
install -m 644 include/sqlite/*.hpp %{buildroot}%{_includedir}/sqlite 
# base
mkdir -p %{buildroot}%{_libdir}
install -m 755 build/libvsqlite++.so.* %{buildroot}%{_libdir}

%files static
%{_libdir}/libvsqlite++.a

%files devel
%{_libdir}/libvsqlite++.so
%{_includedir}/sqlite

%files
%doc Changelog README LICENSE TODO VERSION
%{_libdir}/libvsqlite++.so.0*

%changelog

