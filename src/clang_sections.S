/* Workaround for GNU ld mishandling AT() on output sections when no input
 * object contributes a matching section.  Clang does not emit empty .data/.bss
 * sections in object files (GCC does), so GNU ld fails to assign the correct
 * LMA for the .data output section, producing a huge sparse binary.
 * Including this object ensures .data and .bss exist as inputs.
 */
.section .data
.section .bss
