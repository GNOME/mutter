#!/usr/bin/make -f
all: sh.vars mk.vars

clean:
	rm -f sh.vars mk.vars

mk.vars: vars.build vars
	$(SHELL) vars.build vars make > $@
sh.vars: vars.build vars
	$(SHELL) vars.build vars shell > $@

