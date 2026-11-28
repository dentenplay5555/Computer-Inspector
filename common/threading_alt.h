/* mbedtls mutex type for visual studio builds (MBEDTLS_THREADING_ALT, see
 * mbedtls_user_config.h): a windows critical section. included by mbedtls/threading.h. */
#ifndef CI_THREADING_ALT_H
#define CI_THREADING_ALT_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct mbedtls_threading_mutex_t {
    CRITICAL_SECTION cs;
    char is_valid;   /* 0 after a failed init or a free: lock and unlock then refuse */
} mbedtls_threading_mutex_t;

#endif
