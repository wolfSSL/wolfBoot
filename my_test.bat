rmdir /s /q build-windows-stm32l4

cmake --preset windows-stm32l4


:: cmake --build --preset windows-stm32l4 --parallel 4 -v

cmake --build --preset windows-stm32l4
