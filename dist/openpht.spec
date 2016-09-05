%global _enable_debug_package 0
%global debug_package %{nil}
Autoreq: 0

Summary:            OpenPHT is a community driven fork of Plex Home Theater
Name:               openpht
Version:            1.6.2
Release:            1%{?dist}
License:            GPL-2.0
Group:              Applications/Multimedia
Source:             openpht-%{version}.tar.gz
URL:                https://github.com/RasPlex/OpenPHT
BuildRequires:      cmake
BuildRequires:      make
BuildRequires:      gcc
BuildRequires:      freetype-devel
BuildRequires:      SDL-devel
BuildRequires:      SDL_image-devel
BuildRequires:      libjpeg-turbo-devel
BuildRequires:      sqlite-devel
BuildRequires:      curl-devel
BuildRequires:      lzo-devel
BuildRequires:      tinyxml-devel
BuildRequires:      fribidi-devel
BuildRequires:      fontconfig-devel
BuildRequires:      yajl-devel
BuildRequires:      libmicrohttpd-devel
BuildRequires:      openssl-devel
BuildRequires:      glew-devel
BuildRequires:      avahi-devel
BuildRequires:      flac-devel
BuildRequires:      python-devel
BuildRequires:      libtiff-devel
BuildRequires:      libvorbis-devel
BuildRequires:      libmpeg2-devel
BuildRequires:      libass-devel
BuildRequires:      librtmp-devel
BuildRequires:      libplist-devel
BuildRequires:      shairplay-devel
BuildRequires:      libva-devel
BuildRequires:      libvdpau-devel
BuildRequires:      libcec-devel
BuildRequires:      swig
BuildRequires:      boost-devel
BuildRequires:      libusb-devel
BuildRequires:      systemd-devel
BuildRequires:      nasm
BuildRequires:      libmodplug-devel
BuildRequires:      libcdio-devel
Requires:      freetype
Requires:      SDL
Requires:      SDL_image
Requires:      libjpeg-turbo
Requires:      sqlite
Requires:      curl
Requires:      lzo
Requires:      tinyxml
Requires:      fribidi
Requires:      fontconfig
Requires:      yajl
Requires:      libmicrohttpd
Requires:      openssl
Requires:      glew
Requires:      avahi
Requires:      flac
Requires:      python
Requires:      libtiff
Requires:      libvorbis
Requires:      libmpeg2
Requires:      libass
Requires:      librtmp
Requires:      libplist
Requires:      shairplay
Requires:      libva
Requires:      libvdpau
Requires:      libcec
Requires:      boost
Requires:      libusb
Requires:      systemd
Requires:      libmodplug
Requires:      libcdio


%description
OpenPHT is a community driven fork of Plex Home Theater. It is used as a client
for the Plex Media Server.


%prep
%autosetup -n openpht-%{version}

%build
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=RELEASE \
    -DCMAKE_INSTALL_PREFIX='%{buildroot}/usr' \
    -DCMAKE_C_FLAGS="$CMAKE_C_FLAGS -O2 -mtune=generic -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include" \
    -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS -O2 -mtune=generic -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include" \
    -DCREATE_BUNDLE='TRUE' \
    -DDUMP_SYMBOLS='FALSE' \
    -DENABLE_AUTOUPDATE='FALSE' \
    -DENABLE_PYTHON='TRUE' \
    -DPYTHON_EXEC='/usr/bin/python2' \
    -DUSE_INTERNAL_FFMPEG='TRUE' \
    -DCMAKE_VERBOSE_MAKEFILE='FALSE'
make -j2

%install
cd build
make install
cp ../dist/openpht %{buildroot}/usr/bin/openpht
chmod 755 %{buildroot}/usr/bin/openpht
cp ../dist/openpht.desktop %{buildroot}/usr/share/applications/openpht.desktop
chmod 644 %{buildroot}/usr/share/applications/openpht.desktop

%files
%doc CONTRIBUTORS LICENSE.GPL README
%{_bindir}/*
%{_prefix}/lib/*
%{_datadir}/*
