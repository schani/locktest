#!/bin/bash

if make all ; then
    for bench in single nested ; do
	echo "# $bench"
	for impl in thin thin-cas thin-halfword fat ; do
	    echo "## $impl"
	    for i in `seq 5` ; do
		`which time` ./locktest-$impl $bench
	    done
	done
    done
fi
