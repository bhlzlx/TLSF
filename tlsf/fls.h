#pragma once

/*
** Two Level Segregated Fit memory allocator, version 3.1.
** Written by Matthew Conte
**	http://tlsf.baisoku.org
**
** Based on the original documentation by Miguel Masmano:
**	http://www.gii.upv.es/tlsf/main/docs
**
** This implementation was written to the specification
** of the document, therefore no GPL restrictions apply.
** 
** Copyright (c) 2006-2016, Matthew Conte
** All rights reserved.
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the copyright holder nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
** 
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL MATTHEW CONTE BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* ====================================================================
 * 
 *     这部分代码是从网上抄的，主要就是利用不同CPU特有的加速指令去求
 * first level 的值，差不多相当于 (uint)floor(log2(size)), 但是它太慢
 * 所以才有下面这大坨代码
 *     简单说，就是每个平台实现这个工作量也不小，我们直接拿来抄就行了
 * 												bhlzlx@gmail.com
 * ====================================================================*/

#if defined(__cplusplus)
#define tlsf_decl inline
#else
#define tlsf_decl static
#endif
/*
** Architecture-specific bit manipulation routines.
**
** TLSF achieves O(1) cost for malloc and free operations by limiting
** the search for a free block to a free list of guaranteed size
** adequate to fulfill the request, combined with efficient free list
** queries using bitmasks and architecture-specific bit-manipulation
** routines.
**
** Most modern processors provide instructions to count leading zeroes
** in a word, find the lowest and highest set bit, etc. These
** specific implementations will be used when available, falling back
** to a reasonably efficient generic implementation.
**
** NOTE: TLSF spec relies on ffs/fls returning value 0..31.
** ffs/fls return 1-32 by default, returning 0 for error.
*/

/*
** Detect whether or not we are building for a 32- or 64-bit (LP/LLP)
** architecture. There is no reliable portable method at compile-time.
*/
#if defined (__alpha__) || defined (__ia64__) || defined (__x86_64__) \
	|| defined (_WIN64) || defined (__LP64__) || defined (__LLP64__)
#define TLSF_64BIT
#endif

/*
** gcc 3.4 and above have builtin support, specialized for architecture.
** Some compilers masquerade as gcc; patchlevel test filters them out.
*/
#if defined (__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) \
	&& defined (__GNUC_PATCHLEVEL__)

#if defined (__SNC__)
/* SNC for Playstation 3. */

tlsf_decl int tlsf_ffs(unsigned int word)
{
	const unsigned int reverse = word & (~word + 1);
	const int bit = 32 - __builtin_clz(reverse);
	return bit - 1;
}

#else

tlsf_decl int tlsf_ffs(unsigned int word)
{
	return __builtin_ffs(word) - 1;
}

#endif

tlsf_decl int tlsf_fls(unsigned int word)
{
	const int bit = word ? 32 - __builtin_clz(word) : 0;
	return bit - 1;
}

#elif defined (_MSC_VER) && (_MSC_VER >= 1400) && (defined (_M_IX86) || defined (_M_X64))
/* Microsoft Visual C++ support on x86/X64 architectures. */

#include <intrin.h>

#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)

tlsf_decl int tlsf_fls(unsigned int word)
{
	unsigned long index;
	return _BitScanReverse(&index, word) ? index : -1;
}

tlsf_decl int tlsf_ffs(unsigned int word)
{
	unsigned long index;
	return _BitScanForward(&index, word) ? index : -1;
}

#elif defined (_MSC_VER) && defined (_M_PPC)
/* Microsoft Visual C++ support on PowerPC architectures. */

#include <ppcintrinsics.h>

tlsf_decl int tlsf_fls(unsigned int word)
{
	const int bit = 32 - _CountLeadingZeros(word);
	return bit - 1;
}

tlsf_decl int tlsf_ffs(unsigned int word)
{
	const unsigned int reverse = word & (~word + 1);
	const int bit = 32 - _CountLeadingZeros(reverse);
	return bit - 1;
}

#elif defined (__ARMCC_VERSION)
/* RealView Compilation Tools for ARM */

tlsf_decl int tlsf_ffs(unsigned int word)
{
	const unsigned int reverse = word & (~word + 1);
	const int bit = 32 - __clz(reverse);
	return bit - 1;
}

tlsf_decl int tlsf_fls(unsigned int word)
{
	const int bit = word ? 32 - __clz(word) : 0;
	return bit - 1;
}

#elif defined (__ghs__)
/* Green Hills support for PowerPC */

#include <ppc_ghs.h>

tlsf_decl int tlsf_ffs(unsigned int word)
{
	const unsigned int reverse = word & (~word + 1);
	const int bit = 32 - __CLZ32(reverse);
	return bit - 1;
}

tlsf_decl int tlsf_fls(unsigned int word)
{
	const int bit = word ? 32 - __CLZ32(word) : 0;
	return bit - 1;
}

#else
/* Fall back to generic implementation. */

tlsf_decl int tlsf_fls_generic(unsigned int word)
{
	int bit = 32;

	if (!word) bit -= 1;
	if (!(word & 0xffff0000)) { word <<= 16; bit -= 16; }
	if (!(word & 0xff000000)) { word <<= 8; bit -= 8; }
	if (!(word & 0xf0000000)) { word <<= 4; bit -= 4; }
	if (!(word & 0xc0000000)) { word <<= 2; bit -= 2; }
	if (!(word & 0x80000000)) { word <<= 1; bit -= 1; }

	return bit;
}

/* Implement ffs in terms of fls. */
tlsf_decl int tlsf_ffs(unsigned int word)
{
	return tlsf_fls_generic(word & (~word + 1)) - 1;
}

tlsf_decl int tlsf_fls(unsigned int word)
{
	return tlsf_fls_generic(word) - 1;
}

#endif

/* Possibly 64-bit version of tlsf_fls. */
#if defined (TLSF_64BIT)
tlsf_decl int tlsf_fls_sizet(size_t size)
{
	int high = (int)(size >> 32);
	int bits = 0;
	if (high)
	{
		bits = 32 + tlsf_fls(high);
	}
	else
	{
		bits = tlsf_fls((int)size & 0xffffffff);

	}
	return bits;
}
#else
#define tlsf_fls_sizet tlsf_fls
#endif