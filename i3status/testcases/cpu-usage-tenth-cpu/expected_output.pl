#!/usr/bin/env perl

use v5.10;
use strict;
use warnings;

chomp(my $cpu_count = `grep -c -P '^processor\\s+:' /proc/cpuinfo`);
if ($cpu_count < 10) {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"full_text\":\"all: 00% CPU_0: 00% CPU_10: \"}]\n";
} else {
    print "{\"version\":1}\n[\n[{\"name\":\"cpu_usage\",\"full_text\":\"all: 00% CPU_0: 00% CPU_10: 00%\"}]\n";
}
