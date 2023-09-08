#!/bin/bash
#
# Prints the LMS signature length given 3 LMS parameters.
# This assumes the LMS parameters are the same per each level tree.
#
# References:
#   - https://www.rfc-editor.org/info/rfc8554
#   - https://eprint.iacr.org/2017/349.pdf

print_usage_and_exit() {
  echo "usage:"
  echo "  ./lms_siglen.sh levels height winternitz"
  echo ""
  echo "options:"
  echo "  levels = {1..8}"
  echo "  height = {5, 10, 15, 20, 25}"
  echo "  winternitz = {1, 2, 4, 8}"
  exit 1
}

if [ $# -ne 3 ]; then
  print_usage_and_exit
fi

levels=$1
height=$2
winternitz=$3

if [[ $levels -lt 1 || $levels -gt 8 ]]; then
  echo "error: invalid levels: $levels"; exit 1
fi

if [[ "$height" != @("5"|"10"|"15"|"20"|"25") ]]; then
  echo "error: invalid height: $height"; exit 1
fi

if [[ "$winternitz" != @("1"|"2"|"4"|"8") ]]; then
  echo "error: invalid winternitz: $height"; exit 1
fi

# n == 32 by definition because this LMS implementation uses SHA256.
n=32

# p = f(w). It's a function of Winternitz value.
if [[ $winternitz == 1 ]]; then
  p=265
else
  p=$(((265 / winternitz) + 1))
fi

# Get the signature length given parameters.
# Reference: Table 2 of Kampanakis, Fluhrer, IACR, 2017.
siglen=$((((36 * levels) + (2 * n * levels) - n - 20) + \
            n * ((p * levels) + (height * levels))))

echo "levels:     $levels"
echo "height:     $height"
echo "winternitz: $winternitz"
echo "signature length: $siglen"
