/* unit.h
 *
 * Copyright (C) 2006-2022 wolfSSL Inc.
 *
 * This file is part of wolfPKCS11.
 *
 * wolfPKCS11 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfPKCS11 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifdef DEBUG_WOLFPKCS11
#define CHECK_COND(cond, ret, msg)                                         \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s - ", __FILE__, __LINE__, msg);     \
            if (!(cond)) {                                                 \
                fprintf(stderr, "FAIL\n");                                 \
                ret = -1;                                                  \
            }                                                              \
            else                                                           \
                fprintf(stderr, "PASS\n");                                 \
        }                                                                  \
        else if (!(cond)) {                                                \
            fprintf(stderr, "\n%s:%d - %s - FAIL\n",                       \
                    __FILE__, __LINE__, msg);                              \
            ret = -1;                                                      \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR(rv, msg)                                                 \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s", __FILE__, __LINE__, msg);        \
            if (rv != CKR_OK)                                              \
                fprintf(stderr, ": %lx - FAIL\n", rv);                     \
            else                                                           \
                fprintf(stderr, " - PASS\n");                              \
        }                                                                  \
        else if (rv != CKR_OK) {                                           \
            fprintf(stderr, "\n%s:%d - %s: %lx - FAIL\n",                  \
                    __FILE__, __LINE__, msg, rv);                          \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR_FAIL(rv, exp, msg)                                       \
    do {                                                                   \
        if (verbose) {                                                     \
            fprintf(stderr, "%s:%d - %s", __FILE__, __LINE__, msg);        \
            if (rv != exp) {                                               \
                fprintf(stderr, " RETURNED %lx - FAIL\n", rv);             \
                if (rv == CKR_OK)                                          \
                    rv = -1;                                               \
            }                                                              \
            else {                                                         \
                fprintf(stderr, " - PASS\n");                              \
                rv = CKR_OK;                                               \
            }                                                              \
        }                                                                  \
        else if (rv != exp) {                                              \
            fprintf(stderr, "\n%s:%d - %s RETURNED %lx - FAIL\n",          \
                    __FILE__, __LINE__, msg, rv);                          \
            if (rv == CKR_OK)                                              \
                rv = -1;                                                   \
        }                                                                  \
        else                                                               \
            rv = CKR_OK;                                                   \
    }                                                                      \
    while (0)
#else
#define CHECK_COND(cond, ret, msg)                                         \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "\n%s:%d - %s - FAIL\n",                       \
                    __FILE__, __LINE__, msg);                              \
            ret = -1;                                                      \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR(rv, msg)                                                 \
    do {                                                                   \
        if (rv != CKR_OK) {                                                \
            fprintf(stderr, "\n%s:%d - %s: %lx - FAIL\n",                  \
                    __FILE__, __LINE__, msg, rv);                          \
        }                                                                  \
    }                                                                      \
    while (0)
#define CHECK_CKR_FAIL(rv, exp, msg)                                       \
    do {                                                                   \
        if (rv != exp) {                                                   \
            fprintf(stderr, "\n%s:%d - %s RETURNED %lx - FAIL\n",          \
                    __FILE__, __LINE__, msg, rv);                          \
            if (rv == CKR_OK)                                              \
                rv = -1;                                                   \
        }                                                                  \
        else                                                               \
            rv = CKR_OK;                                                   \
    }                                                                      \
    while (0)
#endif

#ifdef TEST_MULTITHREADED
#define TEST_CASE(func, flags, setup, teardown, argsSz) \
    { func, #func, CKR_OK, 0, 0, flags, setup, teardown, argsSz, 0, 0 }
#else
#define TEST_CASE(func, flags, setup, teardown, argsSz) \
    { func, #func, CKR_OK, 0, 0, flags, setup, teardown, argsSz }
#endif

typedef struct TEST_FUNC
{
    CK_RV (*func)(void* args);
    const char* name;
    CK_RV ret;
    byte run:1;
    byte attempted:1;
    int flags;
    CK_RV (*setup)(int flags, void* args);
    void (*teardown)(int flags, void* args);
    int argsSz;
#ifdef TEST_MULTITHREADED
    pthread_t thread;
    int cnt;
#endif
} TEST_FUNC;


static int verbose = 0;

#ifdef TEST_MULTITHREADED
static wolfSSL_Mutex readMutex;
static wolfSSL_Mutex writeMutex;
static int lockCnt;
static int stop = 0;
static int secs = 10;

static int LockInit(void)
{
    int ret;

    ret = wc_InitMutex(&readMutex);
    if (ret == 0) {
        ret = wc_InitMutex(&writeMutex);
        if (ret != 0)
            wc_FreeMutex(&readMutex);
    }
    if (ret == 0)
        lockCnt = 0;
    if (ret != 0)
        ret = -1;

    return ret;
}

static void LockFree(void)
{
    wc_FreeMutex(&writeMutex);
    wc_FreeMutex(&readMutex);
}

static int LockRW(void)
{
    return wc_LockMutex(&writeMutex);
}

static int UnlockRW(void)
{
    return wc_UnLockMutex(&writeMutex);
}

static int LockRO(void)
{
    int ret;

    ret = wc_LockMutex(&readMutex);
    if (ret == 0) {
        if (++lockCnt == 1)
            ret = wc_LockMutex(&writeMutex);
    }
    if (ret == 0)
        ret = wc_UnLockMutex(&readMutex);
    if (ret != 0)
        ret = -1;

    return ret;
}

static int UnlockRO(void)
{
    int ret;

    ret = wc_LockMutex(&readMutex);
    if (ret == 0) {
        if (--lockCnt == 0)
            ret = wc_UnLockMutex(&writeMutex);
    }
    if (ret == 0)
        ret = wc_UnLockMutex(&readMutex);
    if (ret != 0)
        ret = -1;

    return ret;
}

static void* run_test(void* args)
{
    int ret;
    CK_RV rv;
    TEST_FUNC* tf = (TEST_FUNC*)args;
    void* testArgs;

    testArgs = XMALLOC(tf->argsSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (testArgs != NULL) {
        ret = LockRO();
        if (ret != 0)
            fprintf(stderr, "Locking failed\n");
        else {
            tf->cnt = 0;
            rv = tf->setup(tf->flags, testArgs);
            if (rv != CKR_OK)
                fprintf(stderr, "Setup failed\n");
            if (rv == CKR_OK) {
                while (rv == CKR_OK && !stop) {
                    rv = tf->ret = tf->func(testArgs);
                    tf->cnt++;
                }
                tf->teardown(tf->flags, testArgs);
            }
            UnlockRO();
        }

        XFREE(testArgs, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }

    return NULL;
}

static CK_RV run_tests(TEST_FUNC* testFunc, int testFuncCnt, int onlySet,
                       int flags)
{
    CK_RV ret = CKR_OK;
    int i;

    ret = LockInit();
    if (ret != 0)
        fprintf(stderr, "Failed to initialize mutex!\n");
    else {
        ret = LockRW();
        if (ret != 0)
            fprintf(stderr, "Failed to lock mutex!\n");
        else {
            for (i = 0; i < testFuncCnt; i++) {
                testFunc[i].attempted = 0;

                if (testFunc[i].flags != flags)
                    continue;

                if (onlySet && !testFunc[i].run)
                    continue;

                if (ret == CKR_OK) {
                    testFunc[i].attempted = 1;

                    fprintf(stderr, "%d: %s ...\n", i + 1, testFunc[i].name);

                    ret = pthread_create(&testFunc[i].thread, NULL, run_test,
                                                                  &testFunc[i]);
                    if (ret != 0)
                        fprintf(stderr, "Failed to create thread for: %d\n", i);
                }
            }

            UnlockRW();
        }
    }

    for (i = 0; i < secs; i++) {
        sleep(1);
        fprintf(stderr, ".");
    }
    fprintf(stderr, "\n");
    stop = 1;
    for (i = 0; i < testFuncCnt; i++) {
        if (!testFunc[i].attempted)
            continue;

        pthread_join(testFunc[i].thread, 0);
        fprintf(stderr, "%d: %s ... %d ... ", i + 1, testFunc[i].name,
                                                               testFunc[i].cnt);
        if (testFunc[i].ret == CKR_OK)
            fprintf(stderr, "PASSED\n");
        else
            fprintf(stderr, "FAILED\n");
        }

    LockFree();
    stop = 0;

    return ret;
}
#else
static CK_RV run_tests(TEST_FUNC* testFunc, int testFuncCnt, int onlySet,
                       int flags)
{
    CK_RV ret = CKR_OK;
    int i;
    void* testArgs;

    for (i = 0; i < testFuncCnt; i++) {
        if (testFunc[i].flags != flags)
            continue;

        if (onlySet && !testFunc[i].run)
            continue;

        testArgs = XMALLOC(testFunc[i].argsSz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        if (testArgs == NULL) {
            fprintf(stderr, "Failed to allocate memory for test case args!\n");
            ret = -1;
            break;
        }

        ret = testFunc[i].setup(flags, testArgs);
        if (ret == CKR_OK) {
            testFunc[i].attempted = 1;

            fprintf(stderr, "%d: %s ... ", i + 1, testFunc[i].name);
            if (verbose)
                fprintf(stderr, " START\n");
            testFunc[i].ret = testFunc[i].func(testArgs);
            if (verbose)
                fprintf(stderr, "%d: %s ... ", i + 1, testFunc[i].name);
            if (testFunc[i].ret == CKR_OK)
                fprintf(stderr, "PASSED\n");
            else if (verbose)
                fprintf(stderr, "FAILED\n");

            testFunc[i].teardown(flags, testArgs);
        }

        XFREE(testArgs, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }

    return ret;
}
#endif

static void UnitUsage(void)
{
#ifdef TEST_MULTITHREADED
    printf("-secs <num>        Number of seconds to run tests for\n");
#endif
    printf("-v                 Verbose output\n");
}

/* Match the command line argument with the string.
 *
 * arg  Command line argument.
 * str  String to check for.
 * return 1 if the command line argument matches the string, 0 otherwise.
 */
static int string_matches(const char* arg, const char* str)
{
    int len = (int)XSTRLEN(str) + 1;
    return XSTRNCMP(arg, str, len) == 0;
}

#ifdef TEST_MULTITHREADED

#define UNIT_PARSE_ARGS(argc, argv)                                        \
    else if (string_matches(*argv, "-v"))                                  \
        verbose = 1;                                                       \
    else if (string_matches(*argv, "-secs")) {                             \
        argc--;                                                            \
        argv++;                                                            \
        if (argc == 0) {                                                   \
            fprintf(stderr, "Number of secs not supplied\n");              \
            return 1;                                                      \
        }                                                                  \
        secs = atoi(*argv);                                                \
    }

#else

#define UNIT_PARSE_ARGS(argc, argv)                                        \
    else if (string_matches(*argv, "-v"))                                  \
        verbose = 1;

#endif

