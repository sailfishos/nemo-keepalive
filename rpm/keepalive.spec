Name:       libkeepalive
Summary:    CPU and display keepalive and scheduling library
Version:    1.8.4
Release:    2
License:    LGPLv2
URL:        https://github.com/sailfishos/nemo-keepalive
Source0:    %{name}-%{version}.tar.bz2
Requires:   dsme
Requires:   mce >= 1.93.0
Requires:   libiphb >= 1.2.0
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  doxygen
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(mce) >= 1.25.0
BuildRequires:  pkgconfig(dsme) >= 0.58
BuildRequires:  pkgconfig(libiphb) >= 1.2.0
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  sailfish-qdoc-template

%description
CPU and display keepalive and scheduling library

%package devel
Summary:    Development headers for libkeepalive
Requires:   %{name} = %{version}-%{release}

%description devel
Development package for CPU and display keepalive and scheduling library

%package examples
Summary:    Example applications for libkeepalive
Requires:   %{name} = %{version}-%{release}

%description examples
Examples of CPU and display keepalive and scheduling library

%package tests
Summary:    Tests for libkeepalive
Requires:   %{name} = %{version}-%{release}

%description tests
%{summary}.

%package doc
Summary:    Documentation for libkeepalive QML API
BuildArch:  noarch

%description doc
%{summary}.

%package glib
Summary:    CPU and display keepalive and scheduling library

%description glib
CPU and display keepalive and scheduling library

%package glib-tools
Summary:    CPU and display keepalive tools

%description glib-tools
CPU and display keepalive tools

%package glib-devel
Summary:    Development headers for libkeepalive for use with glib
Requires:   %{name}-glib = %{version}-%{release}

%description glib-devel
Development package for CPU and display keepalive and scheduling library

%package    glib-doc
Summary:    Documentation for libkeepalive-glib
BuildArch:  noarch

%description glib-doc
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build
export VERSION=`echo %{version} | sed 's/+.*//'`
%qmake5 VERSION=${VERSION}
%make_build
%make_build -C lib-glib VERS=${VERSION} _LIBDIR=%{_libdir}
%make_build -C tools VERS=${VERSION} _LIBDIR=%{_libdir}

%install
make install INSTALL_ROOT=%{buildroot}
make -C lib-glib install ROOT=%{buildroot} VERS=%{version} _LIBDIR=%{_libdir}
make -C tools install ROOT=%{buildroot} VERS=%{version} _LIBDIR=%{_libdir}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%post glib -p /sbin/ldconfig

%postun glib -p /sbin/ldconfig

%files
%license license.lgpl
%{_libdir}/libkeepalive.so.*
%dir %{_libdir}/qt5/qml/Nemo/
%dir %{_libdir}/qt5/qml/Nemo/KeepAlive
%{_libdir}/qt5/qml/Nemo/KeepAlive/*

%files devel
%{_libdir}/libkeepalive.so
%{_libdir}/pkgconfig/keepalive.pc
%dir %{_includedir}/keepalive
%{_includedir}/keepalive/*.h

%files examples
%{_libdir}/qt5/bin/backgroundactivity_periodic
%{_libdir}/qt5/bin/backgroundactivity_linger
%{_libdir}/qt5/bin/displayblanking
%{_libdir}/qt5/examples/keepalive/*.qml

%files tests
/opt/tests/nemo-keepalive/*

%files doc
%dir %{_datadir}/doc/libkeepalive
%{_datadir}/doc/libkeepalive/libkeepalive.qch

%files glib
%{_libdir}/libkeepalive-glib.so.*

%files glib-tools
%{_bindir}/keepalive-tool

%files glib-devel
%{_libdir}/libkeepalive-glib.so
%{_libdir}/pkgconfig/keepalive-glib.pc
%dir %{_includedir}/keepalive-glib
%{_includedir}/keepalive-glib/*.h

%files glib-doc
%dir %{_datadir}/doc/libkeepalive-glib
%dir %{_datadir}/doc/libkeepalive-glib/html
%dir %{_datadir}/doc/libkeepalive-glib/html/search
%{_datadir}/doc/libkeepalive-glib/*
