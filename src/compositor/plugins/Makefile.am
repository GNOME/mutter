pkgdatadir = $(datadir)/mutter-$(LIBMUTTER_API_VERSION)
pkglibdir = $(libdir)/mutter-$(LIBMUTTER_API_VERSION)

AM_CPPFLAGS = \
	$(MUTTER_CFLAGS)						\
	-I$(top_builddir)/src						\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/cogl						\
	-I$(top_builddir)/cogl						\
	-I$(top_builddir)/cogl/cogl					\
	-I$(top_srcdir)/clutter						\
	-I$(top_builddir)/clutter					\
	-I$(top_builddir)/clutter/clutter				\
	-DMUTTER_LIBEXECDIR=\"$(libexecdir)\"				\
	-DMUTTER_LOCALEDIR=\"$(localedir)\"				\
	-DMUTTER_PKGDATADIR=\"$(pkgdatadir)\"				\
	-DMUTTER_DATADIR=\"$(datadir)\"					\
	-DG_LOG_DOMAIN=\"mutter\"					\
	-DSN_API_NOT_YET_FROZEN=1					\
	-DMUTTER_PLUGIN_DIR=\"$(MUTTER_PLUGIN_DIR)\"

libdefault_la_CFLAGS   = -fPIC
libdefault_la_SOURCES  = default.c
libdefault_la_LDFLAGS  = -module -avoid-version -no-undefined
libdefault_la_LIBADD   = $(CLUTTER_LIBS)

pkglib_LTLIBRARIES = libdefault.la

# post-install hook to remove the .la and .a files we are not interested in
# (There is no way to stop libtool generating static libs locally, and we
# cannot do this globally because of libmutter-private.so).
install-exec-hook:
	-rm -f $(DESTDIR)$(pkglibdir)/*.a
	-rm -f $(DESTDIR)$(pkglibdir)/*.la

# Since we removed the .la file, 'make uninstall' doesn't work properly,
# since it counts on libtool to remove the .la files, so just kill the
# .so file manually.
uninstall-local:
	-rm -f $(DESTDIR)$(pkglibdir)/libdefault.so
