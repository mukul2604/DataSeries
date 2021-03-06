#!/bin/sh -x
#
# (c) Copyright 2008, Hewlett-Packard Development Company, LP
#
#  See the file named COPYING for license details
#
# test script

set -e

SRC=$1

# create our data file
./expr
../process/ds2txt --skip-index expr.ds |
  perl $SRC/check-data/clean-timing.pl >expr.txt

# run our tests
run() {
  msg=$2
  test "$msg" = "" && msg="$1"
  echo "expr: $msg start" >>expr.txt
  ../process/ds2txt --skip-all --where 'Test::Simple' "$1" expr.ds |
    perl $SRC/check-data/clean-timing.pl >>expr.txt
  echo "expr: $msg end" >>expr.txt
}

run "a != b"
run "a == b"
run "a > b"
run "a < b"
run "a >= b"
run "a <= b"
run "a * - b == - c"
run "(a == b) && (b == c)"
run "(a == b) || (a == c)"

run "d == e"
run "d != e"
run "d > e"
run "d >= e"
run "d < e"
run "d <= e"
run 'd == "0"'
run 'd != "0"'
run '"0" == d'
run '"0" != d'
run '"\\\"\f\r\n\b\t\v" == "\\\"\f\r\n\b\t\v"' "special-characters"

cmp $SRC/check-data/expr.txt expr.txt
rm -f expr.ds expr.txt

exit 0
