#!/usr/bin/make -f
# Separate tarball/patch build system by Adam Heath <doogie@debian.org>

# The magic targets that you need to concern yourself with are:
#
# source.build:		Unpacks upstream tarballs, optionally applies patches
#			to fix the upstream patches, then applies upstream
#			patches.
# source.make:		Applies debian patches.
# source.clean:		Cleans the build directory, then unfixes the upstream
#			patches.
# source.compile:	Will compile the source for you.  Please check
#			debian/scripts/vars.
# source.cmd:		When calling this target, if you define a variable
#			SOURCE_CMD, it will run that command in the build
#			tree.
# make-diff:		Generates debian.diff in the current directory which
#			contains all edits that are currently in the build
#			tree.
#
# Nothing in this file should require any editting.  Please look at
# debian/scripts/vars for things to change for the local environment.
#
# debian/rules target		command
# ----------------------------------------------------------------
# clean:			$(MAKE) -f debian/sys-build.mk source.clean
# build:			$(MAKE) -f debian/sys-build.mk source.compile
#					for simple systems.
# build:			$(MAKE) -f debian/sys-build.mk source.make
#					and, in the rules file, you can
#					build the targets you want.
SHELL=/bin/bash
ifndef NOISY
.SILENT:
endif

include debian/scripts/vars
# remove quotes
DIFF_EXCLUDE:=$(patsubst %,-x %,$(shell echo $(DIFF_EXCLUDE)))

ifdef TAR_DIR
BUILD_TREE=$(SOURCE_DIR)/$(TAR_DIR)
else
BUILD_TREE=$(SOURCE_DIR)
endif

SOURCE_CMD=:

ifdef CLEAN_IGNORE
	CLEAN_CMD=-
	CLEAN_SH=
else
	CLEAN_CMD=
	CLEAN_SH=
endif
ifndef CLEAN_TARGET
	CLEAN_TARGET=clean
endif

foo:
	echo $(DIFF_EXCLUDE)

make-diff:
	mv $(BUILD_TREE) bak
	$(MAKE) -f debian/sys-build.mk source.clean
	$(MAKE) -f debian/sys-build.mk source.make
	mv $(BUILD_TREE) $(BUILD_TREE).orig
	mv bak $(BUILD_TREE)

ifdef TAR_DIR
ifdef CLEAN_TARGET_EXTERNAL
	$(CLEAN_CMD)$(MAKE) -f debian/rules $(CLEAN_TARGET_EXTERNAL)
else
	$(CLEAN_CMD)$(MAKE) -C $(BUILD_TREE) $(CLEAN_TARGET)
endif
	-(cd $(SOURCE_DIR);diff -ruN $(TAR_DIR).orig $(TAR_DIR) $(DIFF_EXCLUDE)) > debian.diff
else
ifdef CLEAN_TARGET_EXTERNAL
	$(CLEAN_CMD)$(MAKE) -f debian/rules $(CLEAN_TARGET_EXTERNAL)
else
	$(CLEAN_CMD)for a in $(BUILD_TREE)/*;do $(MAKE) -C $$a $(CLEAN_TARGET);done
endif
	-(diff -ruN $(BUILD_TREE).orig $(BUILD_TREE) $(DIFF_EXCLUDE)) > debian.diff
	if [ ! -s debian.diff ];then\
		rm debian.diff;\
	fi
endif
	rm -rf $(BUILD_TREE).orig

patchapply: $(STAMP_DIR)/patchapply
$(STAMP_DIR)/patchapply: $(STAMP_DIR)/source.build $(STAMP_DIR)
	$(SHELL) debian/scripts/lib patch.apply
	touch $@
	rm -f $(STAMP_DIR)/patchunapply

patchunapply: $(STAMP_DIR)/patchunapply
$(STAMP_DIR)/patchunapply: $(STAMP_DIR)/source.build $(STAMP_DIR)
	$(SHELL) debian/scripts/lib patch.unapply
	touch $@
	rm -f $(STAMP_DIR)/patchapply

.export: SOURCE_TREE

#
# The rules that really do the work all start with $(STAMPDIR)
# This little trick allows us to use stamp files to keep us from
# having to rerun long targets over and over.  It also puts
# all stamp files in one place, for easy cleaning.
#
# If a stampdir rule depends on something else, be sure it is
# another stampdir rule.  Depending on base rule won't work.
#

source.build: $(STAMP_DIR)/source.build
STAMP_DIR_TARGETS+= $(STAMP_DIR)/source.build
$(STAMP_DIR)/source.build: $(STAMP_DIR)/source.unpack $(STAMP_DIR)/source.patch $(STAMP_DIR)
	touch $@

source.make: $(STAMP_DIR)/source.make
STAMP_DIR_TARGETS+= $(STAMP_DIR)/source.make
$(STAMP_DIR)/source.make: $(STAMP_DIR)/source.build $(STAMP_DIR)/patchapply $(STAMP_DIR)
	touch $@

source.unpack: $(STAMP_DIR)/source.unpack
STAMP_DIR_TARGETS+= $(STAMP_DIR)/source.unpack
$(STAMP_DIR)/source.unpack: $(STAMP_DIR)
	$(SHELL) debian/scripts/source.unpack
	touch $@

source.patch: $(STAMP_DIR)/source.patch
STAMP_DIR_TARGETS+= $(STAMP_DIR)/source.patch
$(STAMP_DIR)/source.patch: $(STAMP_DIR)/source.unpack $(STAMP_DIR)/fix.source.patch $(STAMP_DIR)
	$(SHELL) debian/scripts/lib source.patch
	touch $@

fix.source.patch: $(STAMP_DIR)/fix.source.patch
STAMP_DIR_TARGETS+= $(STAMP_DIR)/fix.source.patch
$(STAMP_DIR)/fix.source.patch: $(STAMP_DIR)
	$(SHELL) debian/scripts/lib fix.source.patch
	touch $@

unfix.source.patch: $(STAMP_DIR)/unfix.source.patch
STAMP_DIR_TARGETS+= $(STAMP_DIR)/unfix.source.patch
$(STAMP_DIR)/unfix.source.patch: $(STAMP_DIR)
	$(SHELL) debian/scripts/lib unfix.source.patch
	touch $@

source.compile: $(STAMP_DIR)/source.compile
STAMP_DIR_TARGETS+= $(STAMP_DIR)/source.compile
$(STAMP_DIR)/source.compile: $(STAMP_DIR)/source.make $(STAMP_DIR)
	$(MAKE) -C $(BUILD_TREE) $(BUILD_TARGET)
	touch $@

source.command:
	(cd $(BUILD_TREE); $(SOURCE_CMD))

DIR_TARGETS+=$(STAMP_DIR)
$(STAMP_DIR_TARGETS): $(STAMP_DIR)

$(DIR_TARGETS)/:
	mkdir -p $@

source.clean: unfix.source.patch
	$(SHELL) debian/scripts/lib source.clean
	rm -f $(STAMP_DIR_TARGETS)
	rm -rf $(STAMP_DIR)
	$(MAKE) -C debian/scripts clean
