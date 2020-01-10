Name:           glade
Version:        3.22.1
Release:        1%{?dist}
Summary:        User Interface Designer for GTK+

# - /usr/bin/glade is GPLv2+
# - /usr/bin/glade-previewer is LGPLv2+
# - libgladeui-2.so, libgladegtk.so, and libgladepython.so all combine
#   GPLv2+ and LGPLv2+ code, so the resulting binaries are GPLv2+
License:        GPLv2+ and LGPLv2+
URL:            http://glade.gnome.org/
Source0:        http://ftp.gnome.org/pub/GNOME/sources/glade/3.22/glade-%{version}.tar.xz

BuildRequires:  chrpath
BuildRequires:  desktop-file-utils
BuildRequires:  docbook-style-xsl
BuildRequires:  gettext
BuildRequires:  gtk3-devel
BuildRequires:  intltool
BuildRequires:  itstool
BuildRequires:  libxml2-devel
BuildRequires:  pygobject3-devel
BuildRequires:  python2-devel
BuildRequires:  webkitgtk4-devel
BuildRequires:  /usr/bin/appstream-util
BuildRequires:  /usr/bin/xsltproc

Requires:       %{name}-libs%{?_isa} = %{version}-%{release}
# The gtk3 version of glade was packaged under the name of 'glade3' for a
# while. However, following upstream naming, 'glade3' package is now the gtk2
# version and 'glade' package is the gtk3 one. The obsoletes are here to
# provide seamless upgrade path from the gtk3 based 'glade3'.
Obsoletes:      glade3 < 1:3.11.0-3

%description
Glade is a RAD tool to enable quick and easy development of user interfaces for
the GTK+ toolkit and the GNOME desktop environment.

The user interfaces designed in Glade are saved as XML, which can be used in
numerous programming languages including C, C++, C#, Vala, Java, Perl, Python,
and others.


%package libs
Summary:        Widget library for Glade UI designer
Obsoletes:      glade3-libgladeui < 1:3.11.0-3

%description    libs
The %{name}-libs package consists of the widgets that compose the Glade GUI as
a separate library to ease the integration of Glade into other applications.


%package devel
Summary:        Development files for %{name}
Requires:       %{name}-libs%{?_isa} = %{version}-%{release}
Obsoletes:      glade3-libgladeui-devel < 1:3.11.0-3

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use Glade widget library.


%prep
%setup -q


%build
%configure --disable-static

# Omit unused direct shared library dependencies.
sed -i -e 's/ -shared / -Wl,-O1,--as-needed\0/g' libtool

make %{?_smp_mflags}


%install
%make_install
find $RPM_BUILD_ROOT -type f -name "*.la" -delete

# Remove rpaths.
chrpath --delete $RPM_BUILD_ROOT%{_bindir}/glade*
chrpath --delete $RPM_BUILD_ROOT%{_libdir}/glade/modules/*.so

%find_lang glade --with-gnome


%check
appstream-util validate-relax --nonet $RPM_BUILD_ROOT%{_datadir}/metainfo/glade.appdata.xml
desktop-file-validate $RPM_BUILD_ROOT%{_datadir}/applications/glade.desktop


%post
update-desktop-database &> /dev/null || :
touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :

%postun
update-desktop-database &> /dev/null || :
if [ $1 -eq 0 ] ; then
    touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%posttrans
gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig


%files -f glade.lang
%license COPYING*
%doc AUTHORS NEWS README
%{_bindir}/glade
%{_bindir}/glade-previewer
%{_datadir}/applications/glade.desktop
%{_datadir}/icons/hicolor/*/apps/glade.png
%{_datadir}/icons/hicolor/scalable/apps/glade-brand-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/glade-symbolic.svg
%{_datadir}/metainfo/glade.appdata.xml
%{_mandir}/man1/glade.1*
%{_mandir}/man1/glade-previewer*

%files libs
%license COPYING*
%dir %{_libdir}/girepository-1.0
%{_libdir}/girepository-1.0/Gladeui-2.0.typelib
%dir %{_libdir}/glade/
%dir %{_libdir}/glade/modules/
%{_libdir}/glade/modules/libgladegtk.so
%{_libdir}/glade/modules/libgladepython.so
%{_libdir}/glade/modules/libgladewebkit2gtk.so
%{_libdir}/libgladeui-2.so.*
%{_datadir}/glade/

%files devel
%{_includedir}/libgladeui-2.0/
%{_libdir}/libgladeui-2.so
%{_libdir}/pkgconfig/gladeui-2.0.pc
%dir %{_datadir}/gir-1.0
%{_datadir}/gir-1.0/Gladeui-2.0.gir
%doc %{_datadir}/gtk-doc/


%changelog
* Tue Apr 03 2018 Kalev Lember <klember@redhat.com> - 3.22.1-1
- Update to 3.22.1
- Resolves: #1569723

* Tue Mar 22 2016 Kalev Lember <klember@redhat.com> - 3.20.0-1
- Update to 3.20.0
- Resolves: #1386873

* Fri Jan 24 2014 Daniel Mach <dmach@redhat.com> - 3.15.0-5
- Mass rebuild 2014-01-24

* Fri Dec 27 2013 Daniel Mach <dmach@redhat.com> - 3.15.0-4
- Mass rebuild 2013-12-27

* Tue Jul 16 2013 Matthias Clasen <mclasen@redhat.com> - 3.15.0-3
- Fix source url

* Wed Jun 26 2013 Matthias Clasen <mclasen@redhat.com> - 3.15.0-2
- Add man pages
- Trim %%changelog

* Mon Mar 18 2013 Richard Hughes <rhughes@redhat.com> - 3.15.0-1
- Update to 3.15.0

* Wed Feb 13 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.14.2-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_19_Mass_Rebuild

* Wed Dec 05 2012 Kalev Lember <kalevlember@gmail.com> - 3.14.2-2
- Revise the summary for consistency with the parallel installable
  glade2/glade3 packages (#882557)

* Mon Nov 26 2012 Kalev Lember <kalevlember@gmail.com> - 3.14.2-1
- Update to 3.14.2

* Tue Oct 16 2012 Kalev Lember <kalevlember@gmail.com> - 3.14.1-1
- Update to 3.14.1

* Wed Sep 19 2012 Kalev Lember <kalevlember@gmail.com> - 3.14.0-1
- Update to 3.14.0
- Remove the unrecognized --disable-scrollkeeper option

* Thu Jul 19 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.13.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Sun May 06 2012 Kalev Lember <kalevlember@gmail.com> - 3.13.0-1
- Update to 3.13.0

* Sun May 06 2012 Kalev Lember <kalevlember@gmail.com> - 3.12.1-1
- Update to 3.12.1

* Thu Apr 12 2012 Kalev Lember <kalevlember@gmail.com> - 3.12.0-3
- Update the spec file comments about licensing and simplify the License tag
- Install the typelib in -libs subpackage

* Fri Apr 06 2012 Kalev Lember <kalevlember@gmail.com> - 3.12.0-2
- Review fixes (#806093)
- Use find_lang --with-gnome for including help files
- Include license files also in the main package in addition to -libs

* Wed Apr 04 2012 Kalev Lember <kalevlember@gmail.com> - 3.12.0-1
- Update to 3.12.0

* Thu Mar 22 2012 Kalev Lember <kalevlember@gmail.com> - 3.11.0-1
- Initial packaging based on Fedora glade3
- Rename the package to glade; added obsoletes for upgrade path
- Spec clean up for review
