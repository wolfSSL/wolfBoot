# wolfBoot Renesas RX CI image: Ubuntu + GNU RX (GNURX) ELF toolchain.
#
# Used by .github/workflows/test-build-renesas-rx.yml to build the RX example
# configs. The GNURX toolchain is login-gated at the vendor
# (llvm-gcc-renesas.com) with no public URL, so it is supplied at build time as
# gnurx-linux.tar.gz rather than fetched. See README.md to build/refresh/push.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential git make curl ca-certificates xz-utils \
    && rm -rf /var/lib/apt/lists/*

# A gzipped tar of the GNURX ELF toolchain tree (contains bin/rx-elf-gcc etc.),
# produced from the login-gated Linux download. See README.md.
ARG GNURX_TARBALL=gnurx-linux.tar.gz
COPY ${GNURX_TARBALL} /tmp/gnurx.tar.gz
RUN mkdir -p /opt/gnurx \
    && tar xf /tmp/gnurx.tar.gz -C /opt/gnurx \
    && rm /tmp/gnurx.tar.gz \
    # Locate the bin/ that holds rx-elf-gcc (tarball nesting varies by release)
    # and expose it at a fixed path so PATH is stable across toolchain versions.
    && RXBIN="$(dirname "$(find /opt/gnurx -type f -name 'rx-elf-gcc' -print -quit)")" \
    && test -n "$RXBIN" \
    && ln -s "$RXBIN" /opt/rx-elf-bin

ENV PATH="/opt/rx-elf-bin:${PATH}"

# Fail the build early if the toolchain is not usable.
RUN rx-elf-gcc --version | head -1

# wolfBoot builds RX via the LD-direct path:
#   make USE_GCC=0 CROSS_COMPILE=rx-elf- wolfboot.srec
