/* SPDX-License-Identifier: MIT */

#ifndef TPW_TEST_H
#define TPW_TEST_H

#include <stdio.h>
#include <stdlib.h>

/* Minimal in-repo test harness — no external test framework dependency. */
#define TPW_ASSERT(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define TPW_ASSERT_EQ(a, b) TPW_ASSERT((a) == (b))

#endif /* TPW_TEST_H */
