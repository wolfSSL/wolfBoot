#!/bin/bash

export RENODE_UART=/tmp/wolfboot.uart
export RENODE_PIDFILE=/tmp/renode.pid
export RENODE_CONFIG=tools/renode/nrf52840_wolfboot.resc
export POFF=262139
export RENODE_PORT=55155
export RENODE_OPTIONS="--pid-file=$RENODE_PIDFILE --disable-xwt -P $RENODE_PORT"
export EXPVER=tools/test-expect-version/test-expect-version
export TEST_OPTIONS=$@

quit_renode() {
    if (which nc); then
        (echo && echo quit && echo) | nc -q 1 localhost $RENODE_PORT >/dev/null
    fi
}




rm -f $RENODE_UART

if (echo $TEST_OPTIONS | grep "LMS" &>/dev/null); then
    # Need git.
    apt install -y git

    # wolfSSL needs to be on latest master for LMS support. Also, we need to
    # add the wolfssl module as a safe directory so docker can use it.
    git config --global --add safe.directory /workspace/lib/wolfssl || exit 2
    cd lib/wolfssl && git checkout master && git pull && cd ../.. || exit 2

    # Need to clone the hash-sigs repo, and patch it for wolfBoot build.
    mkdir -p lib/hash-sigs/lib || exit 2
    git clone https://github.com/cisco/hash-sigs.git lib/hash-sigs/src || exit 2
    cd lib/hash-sigs/src && git checkout b0631b8891295bf2929e68761205337b7c031726 && \
        git apply ../../../tools/lms/0001-Patch-to-support-wolfBoot-LMS-build.patch &&\
        cd ../../.. || exit 2
fi

if (echo $TEST_OPTIONS | grep "XMSS" &>/dev/null); then
    # Need git.
    apt install -y git

    # wolfSSL needs to be on latest master for XMSS support. Also, we need to
    # add the wolfssl module as a safe directory so docker can use it.
    git config --global --add safe.directory /workspace/lib/wolfssl || exit 2
    cd lib/wolfssl && git checkout master && git pull && cd ../.. || exit 2


    # Need to clone the hash-sigs repo, and patch it for wolfBoot build.
    cd lib || exit 2
    git clone https://github.com/XMSS/xmss-reference.git xmss || exit 2
    cd xmss && git checkout 171ccbd26f098542a67eb5d2b128281c80bd71a6 && \
        git apply ../../tools/xmss/0001-Patch-to-support-wolfSSL-xmss-reference-integration.patch &&\
        cd ../../ || exit 2
fi

make distclean
make -C tools/keytools
make -C tools/test-expect-version
make clean && make $TEST_OPTIONS || exit 2
make /tmp/renode-test-update.bin $TEST_OPTIONS || exit 2
cp /tmp/renode-test-update.bin test-app/ || exit 3

cp wolfboot.elf /tmp/renode-wolfboot.elf || exit 3
cp test-app/image_v1_signed.bin /tmp/renode-test-v1.bin || exit 3
cp test-app/renode-test-update.bin /tmp || exit 3
echo "Launching Renode"
renode $RENODE_OPTIONS $RENODE_CONFIG >/dev/null &
while ! (test -e $RENODE_UART); do sleep .1; done
echo "Renode up: uart port activated"
echo "Renode running: renode has been started."
RET=$($EXPVER $RENODE_UART)

if (test $RET -eq 1); then
    echo "Factory img: OK"
else
    echo "FAILURE"
    quit_renode
    exit 1
fi

RET=$($EXPVER $RENODE_UART)
if (test $RET -eq 2); then
    echo "Update: OK"
else
    echo "FAILURE"
    quit_renode
    exit 1
fi

quit_renode
exit 0
