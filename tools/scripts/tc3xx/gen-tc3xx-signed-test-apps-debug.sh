#!/bin/bash

set -euxo pipefail

../../keytools/sign --rsa4096 --sha256 "../../../IDE/AURIX/test-app/TriCore Debug (GCC)/test-app.bin" ../../../priv.der 1
../../keytools/sign --rsa4096 --sha256 "../../../IDE/AURIX/test-app/TriCore Debug (GCC)/test-app.bin" ../../../priv.der 2
