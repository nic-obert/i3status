#!/usr/bin/env perl

use v5.10;
use strict;
use warnings;

chomp(my $cpu_count = `grep -c -P '^processor\\s+:' /proc/cpuinfo`);
if ($cpu_count == 1) {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"full_text\":\"all: 00% CPU_0: 00% CPU_1: \"}]\n";
} else {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"full_text\":\"all: 50% CPU_0: 00% CPU_1: 100%\"}]\n";
}
