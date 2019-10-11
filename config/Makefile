-include ../.config
include ../tools/config.mk
config:
	rm -f ../config/target.ld
	rm -f ../.config
	rm -f ../include/target.h
	@$(foreach q, $(CONFIG_VARS), a=$($q); echo -n $q \? [$$a] :; a=$($q); read SH_$q; test -z $$SH_$q && SH_$q=$$a; echo "$q?=$$SH_$q" >> ../.config;)

