#!/usr/bin/env perl

use v5.10;
use strict;
use warnings;

chomp(my $cpu_count = `grep -c -P '^processor\\s+:' /proc/cpuinfo`);
if ($cpu_count == 1) {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"color\":\"#FF0000\",\"full_text\":\"all: 100% CPU_0: 100% CPU_1: \"}]\n";
} else {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"color\":\"#FFFF00\",\"full_text\":\"all: 75% CPU_0: 100% CPU_1: 50%\"}]\n";
}
