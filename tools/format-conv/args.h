/* args.h
 *
 * Copyright (C) 2006-2022 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef ARGS_H
#define ARGS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FALSE  0
#define TRUE   1

#define ARGS_STDIN  1
#define ARGS_STDOUT 1

#ifndef NO_ERROR_MESSAGE
#define FPRINTF(...) fprintf(__VA_ARGS__)
#else
#define FPRINTF(...)
#endif


typedef struct {
    int error;
    long long used;
    int argc;
    char **argv;
} Args_ctx;

static Args_ctx args_ctx;

#define ARGS_INIT(c, v) \
    int (c) = args_ctx.argc;    \
    char **(v) = args_ctx.argv;

#define SET_USED(c) args_ctx.used |= (0x1 << (c))
#define GET_USED(c) (0x1 << (c)) & args_ctx.used

#define SET_ERROR args_ctx.error = TRUE

static int Args_error()
{
    return args_ctx.error;
}

static void Args_open(int ac, char **av)
{
    args_ctx.argc  = ac;
    args_ctx.argv  = av;
    args_ctx.used  = 0;
    args_ctx.error = FALSE;
}
 
static FILE *Args_infile(const char *mode, int defaultIn)
{
    FILE *in;

    ARGS_INIT(argc, argv);

    for(argv++; argc > 1; argv++, argc--) {
        if(GET_USED(argc))
            continue;
        if((*argv)[0] != '-') {
            SET_USED(argc);
            if((in = fopen(*argv, mode)) != NULL) 
                return in;
            else if(defaultIn == ARGS_STDIN) {
                return stdin;
            } else {
                FPRINTF(stderr, "Input file open error (%s)\n", *argv);
                SET_ERROR;
                return NULL;
            }
        }
    }
    FPRINTF(stderr, "No input file\n");
    return NULL;
}

static FILE *Args_outfile(const char *mode, int defaultOut)
{
    FILE *out;
    int infile = 0;
    ARGS_INIT(argc, argv);

    for (argv++; argc > 1; argv++, argc--) {
        if (GET_USED(argc))
            continue;
        if ((*argv)[0] != '-') {
            SET_USED(argc);
            if ((out = fopen(*(argv), mode)) != NULL)
                return out;
            else if (defaultOut == ARGS_STDOUT) {
                return stdout;
            } else {
                FPRINTF(stderr, "Input file open error (%s)\n", *argv);
                SET_ERROR;
                return NULL;
            }
        }
    }
    return stdout;
}

static int Args_option(char *opt)
{
    ARGS_INIT(argc, argv);

    for (argv++; argc > 1; argv++, argc--) {
        if ((*argv)[0] == '-' && strcmp(&(*argv)[1], opt) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static int args_hexDigit2int(char h)
{
    int v;

    if(h >= '0' && h <= '9')
        return h - '0';
    if (h >= 'a' && h <= 'f')
        return h - 'a' + 10;
    if(h >= 'A' && h <= 'F')
        return h - 'A' + 10;
    return -1;
}


static unsigned char args_twoHexDigits2bin(char *hex) {
    int v1, v2;
    v1 = args_hexDigit2int(hex[0]);
    v2 = args_hexDigit2int(hex[1]);
    if(v1 < 0 || v2 < 0)
        return -1;

    return v1 << 4 | v2;
}

static void args_hex2bin(unsigned char *v, char *hex, int sz)
{
    int i;
    char twoHex[3];
    twoHex[2] = '\0';

    if (v == NULL || hex == NULL){
        SET_ERROR;
        FPRINTF(stderr, "Invalid argument\n");
        return;
    }
    if(strlen(hex) == 0) {
        *v = 0;
        return;
    }

    twoHex[0] = strlen(hex) % 2 ? '0' : *hex++;
    twoHex[1] = *hex++;

    for(i = 0; i < sz; i++) {
        if((v[sz] = args_twoHexDigits2bin(twoHex)) < 0) {
            SET_ERROR;
            FPRINTF(stderr, "Invalid hex value\n");
            return;
        }
        if(*hex == '\0')
            return;
        twoHex[0] = *hex++;
        twoHex[1] = *hex++;
    }
    SET_ERROR;
    FPRINTF(stderr, "Too many digits\n");
}

static int Args_optHex(char *opt, unsigned char *v, int sz)
{
    unsigned int *hex;
    ARGS_INIT(argc, argv);

    for (argv++; argc > 1; argv++, argc--) {
        if (GET_USED(argc))
            continue;
        if ((*argv)[0] == '-' && strcmp(&(*argv)[1], opt) == 0) {
            SET_USED(argc);
            SET_USED(argc-1);
            args_hex2bin(v, *++argv, sz);
            return TRUE;
        }
    }
    return FALSE;
}

static int args_dec2bin(char *dec)
{
    int v = 0;

    for (; *dec; dec++) {
        if (*dec < '0' || *dec > '9'){
            SET_ERROR;
            FPRINTF(stderr, "Invalid decimal value\n");
            return 0;
        }
        v = v * 10 + (*dec - '0');
        if((unsigned int)v != (int)v) {
            SET_ERROR;
            FPRINTF(stderr, "Decimal value overflow\n");
            return 0;
        }
    }
    return v;
}

static int Args_optDec(char *opt, int *v)
{
    ARGS_INIT(argc, argv);
    for (argv++; argc > 1; argv++, argc--) {
        if (GET_USED(argc))
            continue;
        if ((*argv)[0] == '-' && strcmp(&(*argv)[1], opt) == 0) {
            SET_USED(argc);
            SET_USED(argc-1);
            *v = args_dec2bin(*++argv);    
            return TRUE;        
        }
    }
    return FALSE;
}

static const char *Args_optStr(char *opt)
{
    ARGS_INIT(argc, argv);
    for (argv++; argc > 1; argv++, argc--) {
        if (GET_USED(argc))
            continue;
        if ((*argv)[0] == '-' && strcmp(&(*argv)[1], opt) == 0) {
            SET_USED(argc);
            SET_USED(argc - 1);
            return *++argv;
        }
    }
    return NULL;
}

static const char *Args_nthArg(int n)
{
    ARGS_INIT(argc, argv);
    for (argv++; argc > 1; argv++, argc--) {
        if (GET_USED(argc))
            continue;
        if ((*argv)[0] != '-') {
            SET_USED(argc);
            SET_USED(argc - 1);
            return *argv;
        }
    }
    return NULL;
}

static void Args_close(FILE * in, FILE * out)
{
    if (in != NULL && in != stdin)
        fclose(in);
    if (out != NULL && out != stdout)
        fclose(out);
}

#endif

/*

This command argment process assume following argment format.

$ commend [-option,...] [infile [outfile]]

Option Type:
Simple option: -? or -*. '-' followed by a single or multiple charactors.
String option: A simple option followed by a charactor string.
Hex option:    A simple option followed by a hexa dicimal value
Decimal option:A simple option followed by a decimal value


--- Macro Option ---
NO_ERROR_MESSAGE: Eliminate error message


--- API Reference ---
static void Args_open(int ac, char **av)
Description:
    Prepare for command argment process
Argument:
    int ac: Argument count passed in main(ac, av)
    int av: Argument value passed in main(ac, av)

static int Args_error()
Description:
    Check if there has been errors in the previous APIs
    The error flag is kept until Args_close.
    Note that API return value if for indicate either the option is found or not.
    You can get errors by this API. Get option APIs output error message as it is deteced
    unless NO_ERROR_MESSAGE is enabled.
Return:
    TRUE: There has been errors
    FALSE: No error


static void Args_close(FILE *in, FILE *out)
Description:
    Wrap up and close the process.


static int Args_option(char *opt)
Description:
    Check if there is a specified Simple option
Argument:
    char *opt: Option string to check.
Return:
    TRUE: The option found
    FALES: The option not found


static const char *Args_optStr(char *opt)
Description:
    Check if there is a specified String option
Argument:
    char *opt: Option string to check.
Return:
    Non NULL: Pointer to the string
    NULL: The option not found

static int Args_optDec(char *opt, int *v)
Description:
    Check if there is a specified String option
Argument:
    char *opt: Option string to check.
    int  *v:   Pointer to binary converted value
Return:
    TRUE:  Option found
    FALES: Not found


static uint8_t *Args_optHex(char *opt, uint8_t *bin, int sz)
Description:
    Check if there is a specified String option
Argument:
    char *opt: Option string to check.
    uint8_t bin: Binary converted hex value
Return:
    Non NULL: Pointer to the hex value string
    NULL: No specified option


static FILE *Args_infile(const char *mode, int defaultIn)
Description:
    Open the file if there is a input file argument.
Argument:
    char *mode: open mode for fopen
    int defaultIn:
        ARGS_STDIN: Assing stdin if it has no input file argment.
Return:
    non-NULL: File descriptor
    NULL:     No infine specified


static FILE *Args_outfile(const char *mode, int defaultOut)
Description:
    Open the file if there is a output file argument.
Argument:
    char *mode: open mode for fopen
    int defaultOut:
        ARGS_STDOUT: Assing stdin if it has no output file argment.
Return:
    non-NULL: File descriptor
    NULL:     No out file specified

static const char *Args_nthArg(int n)
Description:
    Get Nth non-optional argument

Argument:
    int n: Argument position to get
Return:
    Non NULL: Pointer to the argument string
    NULL:     No argument



--- Example Code ---

command [-e][-pub][-s 999] in_file [out_file]

in_file is mandate. If no out_file is specified, output to stdout



int main(int ac, char** av)
{
    FILE *in, *out;
    int ret;
    int keySz = 0;
    int ecc;
    int pub;

    Args_open(ac, av);
    ret   = Args_optDec("s", &keySz);
    ecc   = Args_option("e");
    pub   = Args_option("pub");
    in    = Args_infile("rb", 0);
    out   = Args_outfile("w+", ARGS_STDOUT);

    if (Args_error()) {
        Args_close(in, out);
        return -1;
    }

    if (ecc)
        ret = func1(in, out, keySz, pub);
    else
        ret = func2(in, out, keySz, pub);

    Args_close(in, out);

    return ret;
}

*/