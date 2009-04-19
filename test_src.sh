#!/bin/sh

set -e # -x

for method in str buf fp fdr fd
do
    result=`echo "abc" | valgrind -q test_src $method`
    test "$result" = "abc"
done

set +e
result=`echo "abc" | valgrind -q test_src fdm 2>&1`
set -e
test "$result" = "yip_fd_map_source: Illegal seek"

yes "The quick brown fox jumps over the lazy dog" | dd of=test_src.input bs=1024 count=1024 2> /dev/null

for method in str buf fp fdr fdm fd path
do
    valgrind -q test_src $method test_src.input > test_src.output
    cmp -s test_src.input test_src.output
done

for method in str buf fp fdr fd
do
    cat test_src.input | valgrind -q test_src $method > test_src.output
    cmp -s test_src.input test_src.output
done

rm -rf test_src.input test_src.output

true
