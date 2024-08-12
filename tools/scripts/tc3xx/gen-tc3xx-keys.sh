#!/bin/bash

set -euxo pipefail

# Note the --der option is required for wolfHSM compatibility
# If you want to use the built-in keystore for testing, remove the --nolocalkeys option
(cd ../../../ && tools/keytools/keygen --ecc256 -g priv.der --exportpubkey --nolocalkeys --der)
