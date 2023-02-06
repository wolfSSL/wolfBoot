#!/bin/bash
rm out.key
rm out.key_pub.der
rm keystore.bak
set -e
# test the c version
./tools/keytools/keygen $1 -g out.key
cp src/keystore.c keystore.bak
./tools/keytools/keygen $1 -i out.key_pub.der
#the only different line should be the /* Key associated to file 'pub_out.key' */
#if more than 1 lin is different the import is wrong
diffRes=$(diff -y --suppress-common-lines src/keystore.c keystore.bak | wc -l)
if [ "$diffRes" != "1" ]
then
    echo "Error keystore mismatch"
    exit 1
fi
rm out.key
rm out.key_pub.der
rm keystore.bak
exit 0
