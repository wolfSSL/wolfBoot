#!/bin/sh
# Creates the coverage output for the library.
# Runs the tests for the current configuration.
# Must be run on Linux and have lcov installed.
# The configuration must have used --enable-coverage.
#

WP11_PATH=`realpath $0`
WP11_PATH=`dirname $WP11_PATH`
COVERAGE_HTML=coverage
COVERAGE_INFO=coverage.info

which lcov >/dev/null 2>&1
LCOV=$?
which genhtml >/dev/null 2>&1
GENHTML=$?
if [ $LCOV -ne 0 -o $GENHTML -ne 0 ]; then
  echo 'Please install lcov on this system.'
  echo
  return 1
fi

grep -- '--enable-coverage' config.status >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo 'Not configured for coverage!'
  echo
  echo 'Configure with option:\n  --enable-coverage'
  echo
  return 1
fi

if [ ! -d "$COVERAGE_HTML" ]; then
  mkdir $COVERAGE_HTML
fi

make clean
find . -name '*.gcda' | xargs rm -f
find . -name '*.gcno' | xargs rm -f
make test

lcov --rc lcov_branch_coverage=1 --capture --list-full-path --directory src --directory src --output-file $COVERAGE_INFO

genhtml --rc lcov_branch_coverage=1 --prefix $WP11_PATH --branch-coverage $COVERAGE_INFO --output-directory $COVERAGE_HTML

OUTDIR=`readlink -m $PWD/$COVERAGE_HTML`
echo
echo 'Coverage results:'
echo "  $OUTDIR/index.html"
echo

