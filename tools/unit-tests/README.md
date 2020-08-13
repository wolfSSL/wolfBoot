# Unit Test Tools

This uses the "check" unit test framework for C.

You may need to run "apt install check", "yum install check" or "brew install check".

## Building

Use `make` to build.

## Expected output

```sh
$ ./unit-parser
Running suite(s): wolfBoot
Explicit end of options reached
This field is too large (bigger than the space available in the current header)
This field is too large and would overflow the image header
Illegal address (too high)
Illegal address (too high)
100%: Checks: 2, Failures: 0, Errors: 0
```
