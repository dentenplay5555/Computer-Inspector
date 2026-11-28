/* additions to mbedtls's default configuration (CMake passes this file as
 * MBEDTLS_USER_CONFIG_FILE to mbedtls and to everything that uses its headers).
 *
 * ixwebsocket handles every connection in its own thread, and tls 1.3 in mbedtls 3.6 uses
 * psa crypto, whose shared state (key slots, the random generator) is only safe across
 * threads with MBEDTLS_THREADING_C. without it, handshakes that overlap can fail with
 * "internal error" or "this is a bug in the library".
 */
#define MBEDTLS_THREADING_C
#if defined(_MSC_VER)
/* no pthreads with visual studio: windows critical sections, see threading_alt.h and
 * tls_global_init() in tlspin.cpp, which installs them */
#define MBEDTLS_THREADING_ALT
#else
/* linux, macos and mingw (winpthreads) */
#define MBEDTLS_THREADING_PTHREAD
#endif
