## Process this file with automake to produce Makefile.in

YEAPHONE_RINGTONES = default_p1k.bin default_p1kh.bin

ringdir = $(datadir)/yeaphone/ringtones
ring_DATA = $(YEAPHONE_RINGTONES)

EXTRA_DIST = gen_ringtones.pl

CLEANFILES = $(YEAPHONE_RINGTONES)

# specify how to generate the ringtones
$(YEAPHONE_RINGTONES): gen_ringtones.pl
	./gen_ringtones.pl

