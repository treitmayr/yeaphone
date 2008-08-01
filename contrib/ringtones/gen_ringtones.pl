#!/usr/bin/perl

use strict;

# frequency parameter is calculated as: (0x10000 - f)

my @default_ringtone_g1 = (
        0xEF,                   # volume [0-255]
        0xFB, 0x1E, 0x00, 0x0C, # 1250 [hz], 12/100 [s]
        0xFC, 0x18, 0x00, 0x0C, # 1000 [hz], 12/100 [s]
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFF, 0xFF, 0x01, 0x90, # silent, 400/100 [s]
        0x00, 0x00              # end of sequence
);

my @special_g1 = (
        0xEF,                   # volume [0-255]
        0xFB, 0x1E, 0x00, 0x0C, # 1250 [hz], 12/100 [s]
        0xFC, 0x18, 0x00, 0x0C, # 1000 [hz], 12/100 [s]
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFF, 0xFF, 0x00, 0x50, # silent, 80/100 [s]
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFB, 0x1E, 0x00, 0x0C,
        0xFC, 0x18, 0x00, 0x0C,
        0xFF, 0xFF, 0x01, 0x40, # silent, 320/100 [s]
        0x00, 0x00              # end of sequence
);

#parameters calculated with:
#perl -e '$q=sqrt(2);$f=3000;for(1..8){printf "%04x\n",0x10000-$f;$f/=$q;}'
my @falling_g1 = (
        0xEF,                   # volume [0-255]
        0xF4, 0x48, 0x00, 0x0C, # 3000 [hz], 12/100 [s]
        0xF7, 0xB6, 0x00, 0x0C,
        0xFA, 0x24, 0x00, 0x0C,
        0xFB, 0xDB, 0x00, 0x0C,
        0xFD, 0x12, 0x00, 0x0C,
        0xFD, 0xED, 0x00, 0x0C,
        0xFE, 0x89, 0x00, 0x0C,
        0xFE, 0xF6, 0x00, 0x0C,
        0xFF, 0xFF, 0x01, 0x00, # silent, 256/100 [s]
        0x00, 0x00              # end of sequence
);
my @rising_g1 = (
        0xEF,                   # volume [0-255]
        0xFE, 0xF6, 0x00, 0x0C,
        0xFE, 0x89, 0x00, 0x0C,
        0xFD, 0xED, 0x00, 0x0C,
        0xFD, 0x12, 0x00, 0x0C,
        0xFB, 0xDB, 0x00, 0x0C,
        0xFA, 0x24, 0x00, 0x0C,
        0xF7, 0xB6, 0x00, 0x0C,
        0xF4, 0x48, 0x00, 0x0C, # 3000 [hz], 12/100 [s]
        0xFF, 0xFF, 0x01, 0x00, # silent, 256/100 [s]
        0x00, 0x00              # end of sequence
);

#perl -e '$q=1.059463;$f=3000;for(1..8){printf "%04x\n",0x10000-$f;$f/=$q;}'
#still sounds kind of strange (eg. rising tone instead of fallgin...)
my @falling2_g1 = (
        0xEF,                   # volume [0-255]
        0xF4, 0x48, 0x00, 0x0C, # 3000 [hz], 12/100 [s]
        0xF4, 0xF0, 0x00, 0x0C,
        0xF5, 0x8F, 0x00, 0x0C,
        0xF6, 0x25, 0x00, 0x0C,
        0xF6, 0xB2, 0x00, 0x0C,
        0xF7, 0x38, 0x00, 0x0C,
        0xF7, 0xB6, 0x00, 0x0C,
        0xF8, 0x2D, 0x00, 0x0C,
        0xFF, 0xFF, 0x01, 0x00, # silent, 256/100 [s]
        0x00, 0x00              # end of sequence
);

my @default_ringtone_g2 = (
        0xFF,           # volume [0-255]
        0x1E, 0x0C,     # 1250 [hz], 12/100 [s]
        0x18, 0x0C,     # 1000 [hz], 12/100 [s]
        0x00, 0x00      # end of sequence
);

my %mapping = (
  "default_p1k.bin"  => \@default_ringtone_g1,
  "special_p1k.bin"  => \@special_g1,
  "rising_p1k.bin"   => \@rising_g1,
  "falling_p1k.bin"  => \@falling_g1,
  "falling2_p1k.bin"  => \@falling2_g1,
  "default_p1kh.bin" => \@default_ringtone_g2
);

foreach my $fname (keys(%mapping)) {
  open FILE, ">$fname" or die "$fname: $!";
  print "creating $fname\n";
  binmode FILE;
  foreach my $byte (@{$mapping{$fname}}) {
    print FILE chr($byte);
  }
  close FILE;
}