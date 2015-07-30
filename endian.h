#pragma once

#if defined (__GLIBC__)
#include <endian.h>
#if (__BYTE_ORDER == __LITTLE_ENDIAN)
	#define AHP_LITTLE_ENDIAN
#elif (__BYTE_ORDER == __BIG_ENDIAN)
	#define AHP_BIG_ENDIAN
#elif (__BYTE_ORDER == __PDP_ENDIAN)
	#define AHP_BIG_ENDIAN
#else
	#error Unknown machine endianness detected.
#endif
	#define AHP_BYTE_ORDER __BYTE_ORDER
#elif defined(_BIG_ENDIAN)
	#define AHP_BIG_ENDIAN
	#define AHP_BYTE_ORDER 4321
#elif defined(_LITTLE_ENDIAN)
	#define AHP_LITTLE_ENDIAN
	#define AHP_BYTE_ORDER 1234
#elif defined(__sparc) || defined(__sparc__) \
   || defined(_POWER) || defined(__powerpc__) \
   || defined(__ppc__) || defined(__hpux) \
   || defined(_MIPSEB) || defined(_POWER) \
   || defined(__s390__)
	#define AHP_BIG_ENDIAN
	#define AHP_BYTE_ORDER 4321
#elif defined(__i386__) || defined(__alpha__) \
   || defined(__ia64) || defined(__ia64__) \
   || defined(_M_IX86) || defined(_M_IA64) \
   || defined(_M_ALPHA) || defined(__amd64) \
   || defined(__amd64__) || defined(_M_AMD64) \
   || defined(__x86_64) || defined(__x86_64__) \
   || defined(_M_X64)
	#define AHP_LITTLE_ENDIAN
	#define AHP_BYTE_ORDER 1234
#else
	#error "Unable to detect endian for your target."
#endif
