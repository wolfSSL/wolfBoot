# wolbBoot CMake Downloads

Device-specific supplemental download specifications.

Include in `CmakePresets.json` via the `WOLFBOOT_DOWNLOADS_CMAKE`

```
"cacheVariables": {
  "WOLFBOOT_DOWNLOADS_CMAKE": "${sourceDir}/cmake/downloads/my_dependency.cmake"
}
```

Format for `my_dependency.cmake` entries:

```
add_download(
    NAME some_name
    URL  https://github.com/some/repo.git
    TAG  5.9.0
)
```
