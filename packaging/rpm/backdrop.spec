Name:           backdrop
Version:        0.1.0
Release:        1%{?dist}
Summary:        Minimal Linux wallpaper rotator

License:        MIT
URL:            https://nexol.io
# Built by scripts/package-rpm.sh from the repository root.
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  pkgconf-pkg-config
BuildRequires:  gtk4-devel
BuildRequires:  libadwaita-devel
BuildRequires:  nlohmann-json-devel
BuildRequires:  systemd-rpm-macros
BuildRequires:  gettext

Requires:       gtk4
Requires:       libadwaita
Requires:       systemd

%description
Backdrop is a minimal wallpaper rotator with a native GTK4/libadwaita UI.
It can change wallpapers on a timer across common Linux desktops.
A systemd --user service keeps rotation running after the window is closed.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
/usr/bin/update-desktop-database %{_datadir}/applications &>/dev/null || :
%systemd_user_post backdrop.service

%preun
%systemd_user_preun backdrop.service

%postun
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
/usr/bin/update-desktop-database %{_datadir}/applications &>/dev/null || :
%systemd_user_postun backdrop.service

%files
%license LICENSE
%{_bindir}/backdrop
%{_datadir}/applications/io.nexol.Backdrop.desktop
%{_datadir}/metainfo/io.nexol.Backdrop.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.nexol.Backdrop.svg
%{_userunitdir}/backdrop.service
%lang(uk) %{_datadir}/locale/uk/LC_MESSAGES/backdrop.mo

%changelog
* Thu Jul 30 2026 Dmytro <dmytro@nexol.io> - 0.1.0-1
- Add gettext i18n (English + Ukrainian)
* Thu Jul 30 2026 Dmytro <dmytro@nexol.io> - 0.1.0-1
- Add systemd --user service for background rotation
* Wed Jul 29 2026 Dmytro <dmytro@nexol.io> - 0.1.0-1
- Initial package
