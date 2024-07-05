#!/bin/bash

set -euxo pipefail

(cd ../../../ && tools/keytools/keygen --rsa4096 -g priv.der)
