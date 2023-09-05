#!/bin/bash
#
# Prints the LMS signature length given 3 LMS parameters.
#
# The total signature length is built up from several sub
# signature lengths that need to be calculated.
#
# This assumes the LMS parameters are the same per each level
# tree.
#
# References:
#   - https://datatracker.ietf.org/doc/html/rfc8554
#   - https://github.com/cisco/hash-sigs
#   - https://eprint.iacr.org/2017/349.pdf

# User parameters:
#   levels = {1..8}
#   height = {5, 10, 15, 20, 25}
#   winternitz = {1, 2, 4, 8}
levels=3
height=5
winternitz=8

# Globals

# The lm_pub_len is 4 less than the HSS pub len of 60.
ots_sig_len=0
sig_len=0
total_len=0
lm_pub_len=56

# n and p.
#  n == 32 by definition because this LMS implementation uses SHA256.
#  p = f(w), it's a function of Winternitz value.
n=32
p=0

# Functions

# Get p given Winternitz value.
ots_get_p() {
  if [[ $winternitz == 1 ]]; then
    p=265
  else
    p=$(((265 / winternitz) + 1))
  fi
}

# Get the OTS sig len for given n and p.
ots_get_sig_len() {
  ots_sig_len=$((4 + n * (p + 1)))
}

# Get the merkle tree sig len for given height and ots_sig_len.
get_sig_len() {
  sig_len=$((8 + $ots_sig_len + (n * $height)))
}

# Finally, get the total signature length given the levels and sig_len.
get_total_len() {
  total_len=$((4 + (levels * sig_len) + ((levels - 1) * (lm_pub_len))))
}

ots_get_p
ots_get_sig_len
get_sig_len
get_total_len

echo "levels:      $levels"
echo "height:      $height"
echo "winternitz:  $winternitz"
echo "#"
# Extra debugging info
# echo "n:           $n"
# echo "p:           $p"
# echo "#"
# echo "ots_sig_len: $ots_sig_len"
# echo "sig_len:     $sig_len"
echo "total_len:   $total_len"
