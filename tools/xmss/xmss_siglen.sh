#!/bin/bash
#
# Prints the XMSS/XMSS^MT signature length given a parameter set string.
#
# References:
#   - https://www.rfc-editor.org/rfc/rfc8391.html
#   - https://eprint.iacr.org/2017/349.pdf

print_usage_and_exit() {
  echo "usage:"
  echo "  ./xmss_siglen.sh <parameter string>"
  echo ""
  echo "examples:"
  echo "  ./xmss_siglen.sh  XMSS-SHA2_10_256"
  echo "  ./xmss_siglen.sh  XMSS-SHA2_16_256"
  echo "  ./xmss_siglen.sh  XMSS-SHA2_20_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_20/2_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_20/4_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_40/2_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_40/4_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_40/8_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_60/3_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_60/6_256"
  echo "  ./xmss_siglen.sh  XMSSMT-SHA2_60/12_256"
  exit 1
}

if [ $# -ne 1 ]; then
  print_usage_and_exit
fi

param=$1
len=${#param}

n=32
p=67
h=0
d=1

if [[ "$len" != @("16"|"20"|"21") ]]; then
  echo "error: invalid len: $len"
  exit 1
fi

if [[ $len == 16 ]]; then
  # Must be one of these three:
  #   - "XMSS-SHA2_10_256"
  #   - "XMSS-SHA2_16_256"
  #   - "XMSS-SHA2_20_256"
  if [[ $param =~ "XMSS-SHA2_" && $param =~ "_256" ]]; then
    h=$(echo $param | cut -c 11-12)
  else
    echo "error: invalid param string: $param"
    exit 1
  fi

  # In XMSS the signature length is:
  #   siglen = 4 + n(p + h + 1)
  siglen=$((4 + n * (p + h + 1)))
else
  # Must be one of these 8:
  #   - "XMSSMT-SHA2_20/2_256"
  #   - "XMSSMT-SHA2_20/4_256"
  #   - "XMSSMT-SHA2_40/2_256"
  #   - "XMSSMT-SHA2_40/4_256"
  #   - "XMSSMT-SHA2_40/8_256"
  #   - "XMSSMT-SHA2_60/3_256"
  #   - "XMSSMT-SHA2_60/6_256"
  #   - "XMSSMT-SHA2_60/12_256"
  #
  if [[ $param =~ "XMSSMT-SHA2_" && $param =~ "_256" ]]; then
    h=$(echo $param  | cut -c 13-14)
  else
    echo "error: invalid param string: $param"
    exit 1
  fi

  if [[ $len == 20 ]]; then
    d=$(echo $param  | cut -c 16-16)
  else
    d=$(echo $param  | cut -c 16-17)
  fi

  # In XMSS^MT the signature length is:
  #   siglen = Sum[h]/8 + n (Sum[p] + Sum[h] + 1)
  #
  # Where Sum[h] means the total height of the hyper-tree.
  # Note: The Sum[h]/8 must be rounded up.
  siglen=$((((h + 7) / 8) + n * ((d * p) + h + 1)))
fi

echo "parameter set:    $param"
echo "signature length: $siglen"
