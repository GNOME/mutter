pkgdatadir = $(datadir)/mutter-$(LIBMUTTER_API_VERSION)
pkglibdir = $(libdir)/mutter-$(LIBMUTTER_API_VERSION)

SUBDIRS = cogl clutter data src po doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/stamp-it \
	po/.intltool-merge-cache
