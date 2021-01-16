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

#include "kprintf.h"
#include <sys/types.h>
/* #include <stdbool.h> */
/* #include <stdint.h> */
#include "hal.h"

#define true 1
#define false 0

static _Bool isdigit(unsigned char c) {
    return ((unsigned char)(c - '0') < 10);
}

void kprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vcprintf((vcprintf_callback_t)hal_putc, NULL, format, args);
    va_end(args);
}

int snprintf(char *buf, size_t size, const char *format, ...) {
    va_list args;
    int len;

    va_start(args, format);
    len = vsnprintf(buf, size, format, args);
    va_end(args);

    return len;
}

typedef struct Flags {
    _Bool plus:1;	// Always include a '+' or '-' sign
    _Bool left:1;	// left justified
    _Bool alternate:1;	// 0x prefix
    _Bool space:1;	// space if plus
    _Bool zeropad:1;	// pad with zero
    _Bool sign:1;	// unsigned/signed number
    _Bool upper:1;	// use UPPER case
} Flags;

/* atoi - convert string to int
 * @ptr: pointer to string
 *
 * Returns converted int and leaves ptr pointing to the first character after
 * the number.
 */
int atoi(const char** ptr) {
    const char* s = *ptr;
    int i = 0;
    while(isdigit(*s)) {
	i = i * 10 + (*s++ - '0');
    }
    *ptr = s;
    return i;
}

typedef struct {
    char *pos;
    size_t size;
} BufferState;

/* cprint_int - Convert integer to string
 * @callback:	callback function to add char
 * @num:	number to convert
 * @base:	must be 10 or 16
 * @size:	number of bytes to fill
 * @precision:	number of digits for floats
 * @flags:	output flags
 *
 * Returns nothing.
 */
void cprint_int(vcprintf_callback_t callback, void *state, uint64_t num,
		int base, int width, int precision, Flags flags) {
    const char LOWER[] = "0123456789abcdef";
    const char UPPER[] = "0123456789ABCDEF";
    const char *digits = (flags.upper) ? UPPER : LOWER;
    char tmp[20];

    // Sanity check base
    if (base != 8 && base != 10 && base != 16) return;

    // Check for sign
    _Bool negative = false;
    if (flags.sign) {
	int64_t t = num;
	if (t < 0) {
	    num = -t;
	    negative = true;
	}
    }

    // convert number in reverse order
    int len = 0;
    if (num == 0) { // special case
	tmp[len++] = '0';
    }
    while(num > 0) {
	tmp[len++] = digits[num % base];
	num /= base;
    }
    // Correct presision if number too large
    if (precision < len) precision = len;

    // Account for sign and alternate form
    if (negative || flags.plus) {
	--width;
    }
    if (flags.alternate) {
	width -= 2;
    }
    
    // Put sign if any
    if (negative) {
	callback('-', state);
    } else if (flags.plus) {
	callback(flags.space ? ' ' : '+', state);
    }

    // Put 0x prefix
    if (flags.alternate) {
	callback('0', state);
	callback('x', state);
    }

    // Pad with ' ' if not left aligned
    if (!flags.left) {
	while(precision < width--) callback(flags.zeropad ? '0' : ' ', state);
    }

    // Pad with ' ' or '0' to precision
    while(len < precision--) {
	callback(flags.zeropad ? '0' : ' ', state);
	--width;
    }

    // Put number
    while(len > 0) {
	callback(tmp[--len], state);
	--width;
    }

    // fill remaining space (flags.left was set)
    while(width-- > 0) callback(' ', state);
}

static void buffer_add(char c, BufferState *state) {
    if (state->size > 0) {
	*state->pos = c;
	--state->size;
    }
    ++state->pos;
}

/* vcprintf - Format a string and call callback for each char
 * @callback:	callback function to add char
 * @format:	Format string for output
 * @args:	Arguments for format string
 *
 * Returns nothing.
 */
void vcprintf(vcprintf_callback_t callback, void *state, const char* format,
	     va_list args) {
    while(*format != 0) {
	// Copy normal chars 1:1
	if (*format++ != '%') {
	    callback(format[-1], state); // format has already advanced
	    continue;
	}

	// Placeholder: %[flags][width][.precision][length]type
	/* Flags:
	 * '+': Always include a '+' or '-' sign for numeric types
	 * '-': Left align output
	 * '#': Alternate form, '0x' prefix for p and x
	 * ' ': Include ' ' for postive numbers
	 * '0': Pad with '0'
	 */
	Flags flags = {false, false, false, false, false, false, false};
    repeat:
	switch(*format++) {
	case '+': flags.plus = true; goto repeat;
	case '-': flags.left = true;  goto repeat;
	case '#': flags.alternate = true; goto repeat;
	case ' ': flags.space = true; goto repeat;
	case '0': flags.zeropad = true; goto repeat;
	default: --format; // undo ++
	}
	/* Width:
	 * '[0-9]'+: use at least this many characters
	 * '*'     : use int from 'args' as width
	 */
	int width = 0;
	if (*format == '*') {
	    ++format;
	    width = va_arg(args, int);
	    if (width < 0) width = 0;
	} else if (isdigit(*format)) {
	    width = atoi(&format);
	}
	/* Precision:
	 * '[0-9]'+: use max this many characters for a string
	 * '*'     : use int from 'args' as precision
	 */
	int precision = -1;
	if (*format == '.') {
	    ++format;
	    if (*format == '*') {
		++format;
		precision = va_arg(args, int);
		if (precision < 0) precision = 0;
	    } else {
		precision = atoi(&format);
	    }
	}
	/* Length:
	 * 'hh': [u]int8_t
	 * 'h' : [u]int16_t
	 * 'l' : [u]int32_t
	 * 'll': [u]int64_t
	 * 'z' : [s]size_t
	 * 't' : ptrdiff_t
	 */
	int length = 4;
	switch(*format++) {
	case 'h':
	    if (*format == 'h') {
		++format; length = 1;
	    } else {
		length = sizeof(short);
	    }
	    break;
	case 'l':
	    if (*format == 'l') {
		++format; length = sizeof(long long);
	    } else {
		length = sizeof(long);
	    }
	    break;
	case 'z':
	    length = sizeof(size_t);
	    break;
	case 't':
	    length = sizeof(intptr_t);
	    break;
	default: --format; // undo ++
	}
	/* Type:
	 * 'd', 'i': signed decimal
	 * 'u'     : unsigned decimal
	 * 'x', 'X': unsigned hexadecimal (UPPER case)
	 * 'p'     : signed hexadecimal of a pointer
	 * 'c'     : character
	 * 's'     : string
	 * '%'     : literal '%'
	 */
	int base = 10;
	uint64_t num = 0;
	switch(*format++) {
	case 'd':
	case 'i':
	    switch(length) {
	    case 1: num = (int8_t) va_arg(args, int); break;
	    case 2: num = (int16_t)va_arg(args, int); break;
	    case 4: num = (int32_t)va_arg(args, int); break;
	    case 8: num = (int64_t)va_arg(args, int64_t); break;
	    }
	    flags.sign = true;
	    if (precision == -1) precision = 0;
	    cprint_int(callback, state, num, base, width, precision, flags);
	    break;
	case 'p':
	    flags.alternate = true;
	    if (precision == -1) precision = 2 * sizeof(void*);
		__attribute__((fallthrough));
	case 'X': flags.upper = true;
		__attribute__((fallthrough));
	case 'x': base = 16; flags.space = false; flags.zeropad = true;
		__attribute__((fallthrough));
	case 'u':
	    switch(length) {
	    case 1: num = (uint8_t) va_arg(args, int); break;
	    case 2: num = (uint16_t)va_arg(args, int); break;
	    case 4: num = (uint32_t)va_arg(args, int); break;
	    case 8: num = (uint64_t)va_arg(args, uint64_t); break;
	    }
	    if (precision == -1) precision = 0;
	    cprint_int(callback, state, num, base, width, precision, flags);
	    break;
	case 'c':
	    callback((char)va_arg(args, int), state);
	    break;
	case 's': {
	    char* s = va_arg(args, char*);
	    if (precision == -1) {
		while(*s != 0) {
		    callback(*s++, state);
		}
	    } else {
		while(precision > 0 && *s != 0) {
		    --precision;
		    callback(*s++, state);
		}
	    }
	    break;
	}
	case '%':
	    callback('%', state);
	    break;
	default: // Unknown placeholder, rewind and copy '%' verbatim
	    while(*format != '%') --format;
	    callback(*format++, state);
	}
    }
}

/* vsnprintf - Format a string and place it in a buffer
 * @buf:    Buffer for result
 * @size:   Size of buffer including trailing '\0'
 * @format: Format string for output
 * @args:   Arguments for format string
 *
 * Returns the number of characters which would be generated for the given
 * input, excluding the trailing '\0', as per ISO C99. If the result is
 * greater than or equal to @size, the resulting string is truncated.
 */
int vsnprintf(char* buf, size_t size, const char* format, va_list args) {
    BufferState state = (BufferState){buf, size};
    vcprintf((vcprintf_callback_t)buffer_add, (void*)&state, format, args);
    // terminate string if there is space in the buffer
    buffer_add('\0', &state);
    // always terminate string even if there was no space
    buf[size - 1] = '\0';
    return state.pos - buf - 1;
}

void cprintf(vcprintf_callback_t callback, void *state, const char* format,
	     ...) {
    va_list args;
    va_start(args, format);
    vcprintf(callback, state, format, args);
    va_end(args);
}
