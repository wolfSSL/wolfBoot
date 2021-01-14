/* https://github.com/mrvn/RaspberryPi-baremetal/tree/master/005-the-fine-printf */

/* Copyright (C) 2007-2015 Goswin von Brederlow <goswin-v-b@web.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Definition of the family of printf functions.
 */

#ifndef KERNEL_KPRINTF_H
#define KERNEL_KPRINTF_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/cdefs.h>

/* https://stackoverflow.com/questions/5825270/printflike-modifier */

#ifdef __GNUC__
#define __PRINTFLIKE(n,m) __attribute__((format(printf,n,m)))
#define NORETURN()      __attribute__((noreturn))
#else
#define __PRINTFLIKE(n,m) /* If only */
#define NORETURN()      /* If only */
#endif /* __GNUC__ */

__BEGIN_DECLS
void kprintf(const char *format, ...) __PRINTFLIKE(1, 2);

int snprintf(char *buf, size_t size, const char *format, ...) __PRINTFLIKE(3, 4);

int vsnprintf(char *buf, size_t size, const char *format, va_list args);

typedef void (*vcprintf_callback_t)(char c, void *state);

void cprintf(vcprintf_callback_t callback, void *state, const char* format,
	     ...) __PRINTFLIKE(3, 4);

void vcprintf(vcprintf_callback_t callback, void *state, const char* format,
	      va_list args);
__END_DECLS

#endif // #ifndef PRINTF_H
