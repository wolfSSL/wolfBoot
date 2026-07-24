# wolfBoot Renesas RX CI image

Builds `ghcr.io/wolfssl/wolfboot-ci-rx`, the container used by
`.github/workflows/test-build-renesas-rx.yml` to build the Renesas RX example
configs (`config/examples/renesas-rx65n.config`, `renesas-rx72n.config`) with a
real GNU RX (GNURX) toolchain.

The GNURX ELF toolchain is **login-gated** at the vendor (llvm-gcc-renesas.com)
and has no public download URL, so unlike the ARM CI it cannot be fetched inline
in the workflow. It is baked into this image instead. Because the toolchain
cannot be redistributed here, this image is built and pushed by hand whenever the
pinned GNURX version changes.

## Build and push (or refresh)

1. Download the GNURX **Linux** package (free account required) from
   https://llvm-gcc-renesas.com/downloads/ -- the RX ELF release (`v1.0` ships
   `14.2.0.202511`; `8.3.0.202311` also works).

2. Produce `gnurx-linux.tar.gz` (a gzipped tar of the toolchain tree containing
   `bin/rx-elf-gcc`) in this directory. The Linux download is a **native x86_64
   ELF installer** (`-p <path>` install path, `-y` silent), so it must run on a
   **real x86_64 Linux host** -- extracting under qemu/Rosetta emulation (e.g. an
   Apple-Silicon Mac) silently fails ("Cleaning up leftover files..." then an
   empty prefix). On x86_64 Linux:
   ```sh
   chmod +x gcc-*-GNURX-ELF.run
   ./gcc-*-GNURX-ELF.run -p "$PWD/gnurx" -y
   tar czf gnurx-linux.tar.gz -C "$PWD/gnurx" .
   ```

3. Log in to GHCR with a token that has `write:packages` on the wolfSSL org:
   ```sh
   gh auth refresh -h github.com -s write:packages,read:packages
   gh auth token | docker login ghcr.io -u <your-user> --password-stdin
   ```
   In the browser flow, also approve the GitHub CLI app for the **wolfSSL org**
   (SSO/authorize) -- the org restricts third-party OAuth apps. If that route is
   blocked, use a classic PAT with `write:packages` instead.

4. Build and push (tag must match the `image:` tag in
   `../../workflows/test-build-renesas-rx.yml`; run on x86_64 Linux):
   ```sh
   ./docker-build-push.sh v1.0
   ```
   The Dockerfile locates `rx-elf-gcc` inside the tarball automatically, so the
   internal nesting of the release does not matter.

5. Make the package **public** (one-time), matching `wolfboot-ci-arm` /
   `wolfboot-ci-powerpc`. There is no REST API for this -- use the UI at
   https://github.com/orgs/wolfssl/packages/container/wolfboot-ci-rx/settings
   (Danger Zone -> Change visibility -> Public). A fork pull request cannot pull
   an `internal` image, so public is required for PR CI.

## Notes

- `gnurx-linux.tar.gz` is intentionally not committed (see `.gitignore` here).
- Bumping the toolchain: rebuild with the new download, push a new tag (e.g.
  `v1.1`), update the `image:` tag in `test-build-renesas-rx.yml`, and make the
  new version public.
- The image only needs `rx-elf-*` on `PATH`; wolfBoot builds RX with
  `make USE_GCC=0 CROSS_COMPILE=rx-elf-`.
