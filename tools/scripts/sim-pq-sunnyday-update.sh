#!/bin/bash
#
# For PQ methods the keytools must be built to match the alg.
#

print_usage_and_die() {
  echo "usage:"
  echo "  ./tools/scripts/sim-pq-sunnyday-update.sh <path to sim-pq dot config>"
  echo ""
  echo "example:"
  echo "  ./tools/scripts/sim-pq-sunnyday-update.sh config/examples/sim-ml-dsa.config"
  exit 1
}

err_and_die() {
  echo "error: $1"
  exit 1
}

if [ $# -ne 1 ]; then
  print_usage_and_die
fi

sim_pq=$1

if [ ! -f $sim_pq ]; then
  err_and_die "file not found: $sim_pq"
fi

cp $sim_pq .config || err_and_die "cp $sim_pq"

make keysclean; make clean;

make keytools || err_and_die "keytools build failed"

make test-sim-internal-flash-with-update || err_and_die "make sim failed"

V=`./wolfboot.elf update_trigger get_version 2>/dev/null`
if [ "x$V" != "x1" ]; then
    echo "Failed first boot with update_trigger"
    exit 1
fi

V=`./wolfboot.elf success get_version 2>/dev/null`
if [ "x$V" != "x2" ]; then
    echo "Failed update (V: $V)"
    exit 1
fi

echo Test successful.
exit 0


