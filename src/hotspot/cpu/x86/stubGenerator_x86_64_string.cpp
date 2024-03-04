/*
 * Copyright (c) 2023, Intel Corporation. All rights reserved.
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "macroAssembler_x86.hpp"
#include "precompiled.hpp"
#include "stubGenerator_x86_64.hpp"
#include "runtime/stubRoutines.hpp"

/******************************************************************************/
//                     String handling intrinsics
//                     --------------------------
//
// Currently implements scheme described in http://0x80.pl/articles/simd-strfind.html
// Implementation can be found at https://github.com/WojciechMula/sse4-strstr
//
// The general idea is as follows:
// 1. Broadcast the first byte of the needle to a ymm register (32 bytes)
// 2. Broadcast the last byte of the needle to a different ymm register
// 3. Compare the first-byte ymm register to the first 32 bytes of the haystack
// 4. Compare the last-byte register to the 32 bytes of the haystack at the (k-1)st position
//    where k is the length of the needle
// 5. Logically AND the results of the comparison
//
// The result of the AND yields the position within the haystack where both the first
// and last bytes of the needle exist in their correct relative positions.  Check the full
// needle value against the haystack to confirm a match.
//
// This implementation uses memcmp to compare when the size of the needle is >= 32 bytes.
// For other needle sizes, the comparison is done with register compares to eliminate the
// overhead of the call (including range checks, etc.).  The size of the comparison is
// known, and it is also known to be safe reading the haystack for the full width of the needle.
//
// The original algorithm as implemented will potentially read past the end of the haystack.
// This implementation protects against that.  Instead of reading as many 32-byte chunks as
// possible and then handling the tail, we calculate the last position of a vaild 32-byte
// read and adjust the starting position of the second read such that the last read will not
// go beyond the end of the haystack.  So the first comparison is to the first 32 bytes of the
// haystack, and the second is offset by an amount to make the last read legal.  The remainder of
// the comparisons are done incrementing by 32 bytes.
//
// This will cause 16 bytes on average to be examined twice, but that is cheaper than the
// logic required for tail processing.
//
/******************************************************************************/

#define __ _masm->

// Register definitions for consistency
#define XMM_BYTE_0 xmm0
#define XMM_BYTE_K xmm1
#define XMM_BYTE_1 xmm12
#define XMM_BYTE_2 xmm13
#define XMM_TMP1 xmm15
#define XMM_TMP2 xmm14
#define XMM_TMP3 xmm2
#define XMM_TMP4 xmm3

// Number of bytes of haystack to compare for equality
// A value of 2 checks the first and last bytes of the needle.
// A value of 3 checks the first two bytes and the last byte of the needle.
// A value of 4 checks the first three bytes and last byte of the needle.
// Added for performance tuning.
#define NUMBER_OF_NEEDLE_BYTES_TO_COMPARE 4

// If EARLY_BAILOUT is non-zero, each element of comparison will be checked.
// If non-zero, all compares will be done with a final check.
#define EARLY_BAILOUT 0

/******************************************************************************/
//                     Helper for big haystack loop construct
//                     --------------------------------------
//
// Code:
//
// template <size_t k, typename MEMCMP>
// size_t FORCE_INLINE avx2_strstr_memcmp_ptr(const char *s, size_t n, const char *needle, MEMCMP memcmp_fun)
// {
//   char *start = (char *)s;
//   char *end = (char *)&s[(n)]; // & ~0x1f];
//   long long incr = (n <= 32) ? 32 : (n - k - 31) % 32;
//
//   const __m256i first = _mm256_set1_epi8(needle[0]);
//   const __m256i last = _mm256_set1_epi8(needle[k - 1]);
//
//   while (s < end)
//   {
//
//     const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(s));
//     CHECK_BOUNDS(s, 32, start);
//     const __m256i block_last = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(s + k - 1));
//     CHECK_BOUNDS(s + k - 1, 32, start);
//
//     const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
//     const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);
//
//     uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));
//
//     while (mask != 0)
//     {
////////////////  Helper code ends here, before comparing full needle
//       const auto bitpos = bits::get_first_bit_set(mask);
//       if (memcmp_fun(s + bitpos + 1, needle + 1, k - 2) == 0)
//       {
//         return s - start + bitpos;
//       }
//
//       mask = bits::clear_leftmost_set(mask);
//     }
//     s += incr;
//     incr = 32;
//   }
//
//   return std::string::npos;
// }
/******************************************************************************/

void StubGenerator::string_indexof_big_loop_helper(
    int size, Label& bailout, Label& loop_top, Register haystack,
    Register hsLength, Register eq_mask, Register incr, Register needle,
    Register hsPtrRet, Register temp1, StrIntrinsicNode::ArgEncoding ae) {
  Label temp;

  // const Register haystack = rbx;
  // const Register hs_temp = rcx;
  // const Register termAddr = rax;
  // const Register hs_length = rsi;
  // const Register eq_mask = rsi;
  // const Register incr = rdx;
  const Register r_temp = hsLength;   // This parameter gets overwritten!!!!
  // const Register needle = r14;

  const XMMRegister byte_0 = XMM_BYTE_0;
  const XMMRegister byte_k = XMM_BYTE_K;
  const XMMRegister byte_1 = XMM_BYTE_1;
  const XMMRegister byte_2 = XMM_BYTE_2;
  const XMMRegister cmp_0 = XMM_TMP3;
  const XMMRegister cmp_k = XMM_TMP4;
  const XMMRegister result = XMM_TMP3;

  bool isLL = (ae == StrIntrinsicNode::LL);
  bool isUL = (ae == StrIntrinsicNode::UL);
  bool isUU = (ae == StrIntrinsicNode::UU);
  bool isU = isUL || isUU;  // At least one is UTF-16

  int sizeIncr = isU ? 2 : 1;

  if (isU && (size & 1)) {
    __ emit_int8(0xcc);
    return;
  }

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
  if (size > (isU ? 4 : 2)) {
    // Add compare for second byte
    if (isU) {
      __ vpbroadcastw(byte_1, Address(needle, 2), Assembler::AVX_256bit);
    } else {
      __ vpbroadcastb(byte_1, Address(needle, 1), Assembler::AVX_256bit);
    }
  }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
  if (size > (isU ? 6 : 3)) {
    // Add compare for third byte
    if (isU) {
      __ vpbroadcastw(byte_2, Address(needle, 4), Assembler::AVX_256bit);
    } else {
      __ vpbroadcastb(byte_2, Address(needle, 2), Assembler::AVX_256bit);
    }
  }
#endif

  __ movq(r11, -1);

  __ leaq(temp1, Address(haystack, hsLength, Address::times_1));
  // Calculate first increment to ensure last read is exactly 32 bytes
  __ leal(r_temp, Address(hsLength, 32 + sizeIncr - size));
  __ andl(r_temp, 0x1f);
  __ cmpl(hsLength, 0x21);
  __ movl(incr, 0x20);
  __ cmovl(Assembler::aboveEqual, incr, r_temp);
  __ movq(hsPtrRet, haystack);
  __ jmpb(temp);

  __ align(16);
  __ bind(loop_top);
  __ addq(hsPtrRet, incr);
  __ movl(incr, 0x20);
  __ cmpq(hsPtrRet, temp1);
  __ jae(bailout);

  __ bind(temp);
  // Compare first byte of needle to haystack
  if (isU) {
    __ vpcmpeqw(cmp_0, byte_0, Address(hsPtrRet, 0), Assembler::AVX_256bit);
  } else {
    __ vpcmpeqb(cmp_0, byte_0, Address(hsPtrRet, 0), Assembler::AVX_256bit);
  }
  if (size != (isU ? 2 : 1)) {
    // Compare last byte of needle to haystack at proper position
    if (isU) {
      __ vpcmpeqw(cmp_k, byte_k, Address(hsPtrRet, size - sizeIncr),
                  Assembler::AVX_256bit);
    } else {
      __ vpcmpeqb(cmp_k, byte_k, Address(hsPtrRet, size - sizeIncr),
                  Assembler::AVX_256bit);
    }

    __ vpand(result, cmp_k, cmp_0, Assembler::AVX_256bit);

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
    if (size > (isU ? 4 : 2)) {
#if EARLY_BAILOUT > 0
      __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
      __ testl(eq_mask, eq_mask);
      __ je_b(loop_top);
#endif
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_1, Address(hsPtrRet, 2), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_1, Address(hsPtrRet, 1), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
    }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
    if (size > (isU ? 6 : 3)) {
#if EARLY_BAILOUT > 0
      __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
      __ testl(eq_mask, eq_mask);
      __ je_b(loop_top);
#endif
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_2, Address(hsPtrRet, 4), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_2, Address(hsPtrRet, 2), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
    }
#endif

    __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
  } else {
    __ vpmovmskb(eq_mask, cmp_0, Assembler::AVX_256bit);
  }
  __ testl(eq_mask, eq_mask);
  __ je_b(loop_top);
  // At this point, we have at least one "match" where first and last bytes
  // of the needle are found the correct distance apart.
  // NOTE: haystack (rbx) should be preserved; hsPtrRet(rcx) is expected to
  //    point to the haystack such that hsPtrRet[tzcntl(eq_mask)] points to
  //    the matched string.
}

/******************************************************************************/
//                     Helper for small haystack loop construct
//                     ----------------------------------------
//
// Code:
//
// template <size_t k, typename MEMCMP>
// size_t FORCE_INLINE avx2_strstr_memcmp_small(const char *s, size_t n, const char *needle, MEMCMP memcmp_fun)
// {
// #pragma nounroll
//   for (size_t i = 0; i < n - k + 1; i++) {
//     if (s[i] == needle[0] && s[i + k - 1] == needle[k - 1]) {
// ////////////////  Helper code ends here, before comparing full needle
//       if (!memcmp_fun(s + i + 1, needle + 1, k - 2)) {
//         return i;
//       }
//     }
//   }
//
//   return std::string::npos;
// }
/******************************************************************************/

void StubGenerator::string_indexof_small_loop_helper(
    int size, Label& bailout, StrIntrinsicNode::ArgEncoding ae) {
  bool isLL = (ae == StrIntrinsicNode::LL);
  bool isUL = (ae == StrIntrinsicNode::UL);
  bool isUU = (ae == StrIntrinsicNode::UU);
  bool isU = isUL || isUU;  // At least one is UTF-16

  const Register haystack = rbx;
  const Register eq_mask = rsi;
  const Register needle = r14;

  const XMMRegister byte_0 = XMM_BYTE_0;
  const XMMRegister byte_k = XMM_BYTE_K;
  const XMMRegister byte_1 = XMM_BYTE_1;
  const XMMRegister byte_2 = XMM_BYTE_2;
  const XMMRegister cmp_0 = XMM_TMP3;
  const XMMRegister cmp_k = XMM_TMP4;
  const XMMRegister result = XMM_TMP3;

  int sizeIncr = isU ? 2 : 1;

  if (isU && (size & 1)) {
    __ emit_int8(0xcc);
    return;
  }

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
  if (size > (isU ? 4 : 2)) {
    // Add compare for second byte
    if (isU) {
      __ vpbroadcastw(byte_1, Address(needle, 2), Assembler::AVX_256bit);
    } else {
      __ vpbroadcastb(byte_1, Address(needle, 1), Assembler::AVX_256bit);
    }
  }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
  if (size > (isU ? 6 : 3)) {
    // Add compare for third byte
    if (isU) {
      __ vpbroadcastw(byte_2, Address(needle, 4), Assembler::AVX_256bit);
    } else {
      __ vpbroadcastb(byte_2, Address(needle, 2), Assembler::AVX_256bit);
    }
  }
#endif

  // Compare first byte of needle to haystack
  if (isU) {
    __ vpcmpeqw(cmp_0, byte_0, Address(haystack, 0), Assembler::AVX_256bit);
  } else {
    __ vpcmpeqb(cmp_0, byte_0, Address(haystack, 0), Assembler::AVX_256bit);
  }
  if (size != (isU ? 2 : 1)) {
    // Compare last byte of needle to haystack at proper position
    if (isU) {
      __ vpcmpeqw(cmp_k, byte_k, Address(haystack, size - sizeIncr),
                  Assembler::AVX_256bit);
    } else {
      __ vpcmpeqb(cmp_k, byte_k, Address(haystack, size - sizeIncr),
                  Assembler::AVX_256bit);
    }
    __ vpand(result, cmp_k, cmp_0, Assembler::AVX_256bit);

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
    if (size > (isU ? 4 : 2)) {
#if EARLY_BAILOUT > 0
      __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
      __ testl(eq_mask, eq_mask);
      __ je(bailout);
#endif
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_1, Address(haystack, 2), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_1, Address(haystack, 1), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
    }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
    if (size > (isU ? 6 : 3)) {
#if EARLY_BAILOUT > 0
      __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
      __ testl(eq_mask, eq_mask);
      __ je(bailout);
#endif
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_2, Address(haystack, 4), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_2, Address(haystack, 2), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
    }
#endif

    __ vpmovmskb(eq_mask, result, Assembler::AVX_256bit);
  } else {
    __ vpmovmskb(eq_mask, cmp_0, Assembler::AVX_256bit);
  }
  __ testl(eq_mask, eq_mask);
  __ je(bailout);
  // At this point, we have at least one "match" where first and last bytes
  // of the needle are found the correct distance apart.
}

void StubGenerator::string_indexof_preload_needle_helper(
    int size, Register needle, Register needleVal,
    StrIntrinsicNode::ArgEncoding ae) {
  // Pre-load the value (correctly sized) of the needle for comparison purposes.

  bool isLL = (ae == StrIntrinsicNode::LL);
  bool isUL = (ae == StrIntrinsicNode::UL);
  bool isUU = (ae == StrIntrinsicNode::UU);
  bool isU = isUL || isUU;  // At least one is UTF-16

  int bytesAlreadyCompared = 0;
  int bytesLeftToCompare = 0;
  int offsetOfFirstByteToCompare = 0;

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
  bytesAlreadyCompared = isU ? 4 : 2;
  offsetOfFirstByteToCompare = isU ? 2 : 1;
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
  bytesAlreadyCompared = isU ? 6 : 3;
  offsetOfFirstByteToCompare = isU ? 4 : 2;
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
  bytesAlreadyCompared = isU ? 8 : 4;
  offsetOfFirstByteToCompare = isU ? 6 : 3;
#endif
  bytesLeftToCompare = size - bytesAlreadyCompared;
  assert((bytesLeftToCompare <= 8), "Too many bytes left to compare");

  if (bytesLeftToCompare <= 0) {
    return;
  }

  // At this point, there is at least one byte of the needle that needs to be
  // compared to the haystack.

  switch (bytesLeftToCompare) {
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
    case 1:
    case 2:
      __ movzwl(needleVal,
                Address(needle, offsetOfFirstByteToCompare));  // needle value
      break;

    case 3:
    case 4:
    case 5:
      __ movl(needleVal,
              Address(needle, offsetOfFirstByteToCompare));  // needle value
      break;

    case 6:
      __ movq(needleVal,
              Address(needle, offsetOfFirstByteToCompare - 1));  // needle value
      break;

    case 7:
    case 8:
      __ movq(needleVal,
              Address(needle, offsetOfFirstByteToCompare));  // needle value
      break;
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE >= 3
    case 1:
    case 2:
      __ movl(needleVal,
              Address(needle, offsetOfFirstByteToCompare - 2));  // needle value
      break;

    case 3:
    case 4:
      __ movl(needleVal,
              Address(needle, offsetOfFirstByteToCompare));  // needle value
      break;

    case 5:
    case 6:
      __ movq(needleVal,
              Address(needle, offsetOfFirstByteToCompare - 2));  // needle value
      break;

    case 7:
    case 8:
      __ movq(needleVal,
              Address(needle, offsetOfFirstByteToCompare));  // needle value
      break;
#endif

    default:
      break;
  }
}

void StubGenerator::string_indexof_byte_compare_helper(
    int size, Label& L_noMatch, Label& L_matchFound, Register needle,
    Register needleVal, Register haystack, Register mask, Register foundIndex,
    Register tmp, StrIntrinsicNode::ArgEncoding ae) {
  // Compare size bytes of needle to haystack
  //
  // At a minimum, the first and last bytes of needle already compare equal
  // to the haystack, so there is no need to compare them again.

  bool isLL = (ae == StrIntrinsicNode::LL);
  bool isUL = (ae == StrIntrinsicNode::UL);
  bool isUU = (ae == StrIntrinsicNode::UU);
  bool isU = isUL || isUU;  // At least one is UTF-16

  int bytesAlreadyCompared = 0;
  int bytesLeftToCompare = 0;
  int offsetOfFirstByteToCompare = 0;

  Label temp;

  if (isU) {
    if ((size & 1) != 0) {
      __ emit_int8(0xcc);
      return;
    }
  }

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
  bytesAlreadyCompared = isU ? 4 : 2;
  offsetOfFirstByteToCompare = isU ? 2 : 1;
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
  bytesAlreadyCompared = isU ? 6 : 3;
  offsetOfFirstByteToCompare = isU ? 4 : 2;
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
  bytesAlreadyCompared = isU ? 8 : 4;
  offsetOfFirstByteToCompare = isU ? 6 : 3;
#endif
  bytesLeftToCompare = size - bytesAlreadyCompared;
  assert((bytesLeftToCompare <= 8), "Too many bytes left to compare");

  if (bytesLeftToCompare <= 0) {
    __ tzcntl(foundIndex, mask);
    __ jmp(L_matchFound);
    return;
  }

  // At this point, there is at least one byte of the needle that needs to be
  // compared to the haystack.

  __ tzcntl(foundIndex, mask);  // Index of match within haystack

  switch (bytesLeftToCompare) {
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
    case 1:
    case 2:
      __ cmpw(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ je(L_matchFound);
      break;

    case 3:
    case 4:
      __ cmpl(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ je(L_matchFound);
      break;

    case 5:
      __ cmpl(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ jne(L_noMatch);
      __ movl(tmp,
              Address(needle, offsetOfFirstByteToCompare + 1));  // needle value
      __ cmpl(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare + 1),
              tmp);
      __ je(L_matchFound);
      break;

    case 6:
      __ cmpq(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare - 1),
              needleVal);
      __ je(L_matchFound);
      break;

    case 7:
    case 8:
      __ cmpq(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ je(L_matchFound);
      break;
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE >= 3
    case 1:
    case 2:
      __ cmpl(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare - 2),
              needleVal);
      __ je(L_matchFound);
      break;

    case 3:
    case 4:
      __ cmpl(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ je(L_matchFound);
      break;

    case 5:
    case 6:
      __ cmpq(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare - 2),
              needleVal);
      __ je(L_matchFound);
      break;

    case 7:
    case 8:
      __ cmpq(Address(haystack, foundIndex, Address::times_1,
                      offsetOfFirstByteToCompare),
              needleVal);
      __ je(L_matchFound);
      break;
#endif

    default:
      break;
  }
}

void StubGenerator::generate_string_indexof() {
  assert((int) StrIntrinsicNode::LL < 4, "Enum out of range");
  assert((int) StrIntrinsicNode::UL < 4, "Enum out of range");
  assert((int) StrIntrinsicNode::UU < 4, "Enum out of range");
  generate_string_indexof_stubs(StrIntrinsicNode::LL);
  generate_string_indexof_stubs(StrIntrinsicNode::UL);
  generate_string_indexof_stubs(StrIntrinsicNode::UU);
  assert(StubRoutines::_string_indexof_array[StrIntrinsicNode::LL] != nullptr, "LL not generated.");
  assert(StubRoutines::_string_indexof_array[StrIntrinsicNode::UL] != nullptr, "UL not generated.");
  assert(StubRoutines::_string_indexof_array[StrIntrinsicNode::UU] != nullptr, "UU not generated.");
}

void StubGenerator::generate_string_indexof_stubs(StrIntrinsicNode::ArgEncoding ae) {
  StubCodeMark mark(this, "StubRoutines", "stringIndexOf");
  address large_hs_jmp_table[10];  // Jump table for large haystacks
  address small_hs_jmp_table[10];  // Jump table for small haystacks
  int jmp_ndx = 0;
  bool isLL = (ae == StrIntrinsicNode::LL);
  bool isUL = (ae == StrIntrinsicNode::UL);
  bool isUU = (ae == StrIntrinsicNode::UU);
  bool isU = isUL || isUU; // At least one is UTF-16
  assert(isLL || isUL || isUU, "Encoding not recognized");
  // __ align(CodeEntryAlignment);
  // __ enter();  // required for proper stackwalking of RuntimeStub frame

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  //                         AVX2 code
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  if (VM_Version::supports_avx2()) {  // AVX2 version

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  //                         Code generation explanation:
  //
  //  The generator will generate code for three cases:
  //  1. Both needle and haystack are Latin-1 (single byte) encoded (LL)
  //  2. Both the needle and haystack are UTF-16 encoded (two bytes per character) (UU)
  //  3. The haystack is UTF-16 encoded and the needle is Latin-1 encoded (UL)
  //
  //  The case of the haystack being Latin-1 and the needle being UTF-16 is short-circuited
  //  so that we never get called in this case.
  //
  //  For the UL case (haystack UTF-16 and needle Latin-1), the needle will be expanded
  //  onto the stack (for size <= MAX_NEEDLE_LEN_TO_EXPAND) and the UU code will do the work.
  //  For UL where the needle size is > MAX_NEEDLE_LEN_TO_EXPAND, we default to a
  //  byte-by-byte comparison (this will be rare).
  //
  //  Note that the code assumes MAX_NEEDLE_LEN_TO_EXPAND is >= 32.
  //
  //  The UU and LL cases are identical except for the loop increments and loading
  //  of the characters into registers.  UU loads and compares words, LL - bytes.
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

#undef STACK_SPACE
#undef MAX_NEEDLE_LEN_TO_EXPAND
#define MAX_NEEDLE_LEN_TO_EXPAND 0x40
// Stack layout:
#define COPIED_HAYSTACK_STACK_OFFSET (0x0)
#define COPIED_HAYSTACK_STACK_SIZE (32)
#define EXPANDED_NEEDLE_STACK_OFFSET \
  (COPIED_HAYSTACK_STACK_OFFSET + COPIED_HAYSTACK_STACK_SIZE)
#define EXPANDED_NEEDLE_STACK_SIZE (MAX_NEEDLE_LEN_TO_EXPAND * 2)
#define SAVED_HAYSTACK_STACK_OFFSET \
  (EXPANDED_NEEDLE_STACK_OFFSET + EXPANDED_NEEDLE_STACK_SIZE)
#define SAVED_HAYSTACK_STACK_SIZE (8)
#define SAVED_INCREMENT_STACK_OFFSET \
  (SAVED_HAYSTACK_STACK_OFFSET + SAVED_HAYSTACK_STACK_SIZE)
#define SAVED_INCREMENT_STACK_SIZE (8)
#define SAVED_TERM_ADDR_STACK_OFFSET \
  (SAVED_INCREMENT_STACK_OFFSET + SAVED_INCREMENT_STACK_SIZE)
#define SAVED_TERM_ADDR_STACK_SIZE (8)
#define STACK_SPACE                                          \
  (COPIED_HAYSTACK_STACK_SIZE + EXPANDED_NEEDLE_STACK_SIZE + \
   SAVED_HAYSTACK_STACK_SIZE + SAVED_INCREMENT_STACK_SIZE +  \
   SAVED_TERM_ADDR_STACK_SIZE)

#define CLEAR_BIT(mask, tmp) \
  if (isU) {                 \
    __ blsrl(tmp, mask);     \
    __ blsrl(mask, tmp);     \
  } else {                   \
    __ blsrl(mask, mask);    \
  }

    const Register haystack     = rdi;
    const Register haystack_len = rsi;
    const Register needle       = rdx;
    const Register needle_len   = rcx;

    const Register save_ndl_len = r12;

    const XMMRegister byte_0    = XMM_BYTE_0;
    const XMMRegister byte_k    = XMM_BYTE_K;
    const XMMRegister byte_1    = XMM_BYTE_1;
    const XMMRegister byte_2    = XMM_BYTE_2;
    const XMMRegister xmm_tmp1  = XMM_TMP1;
    const XMMRegister xmm_tmp2  = XMM_TMP2;
    const XMMRegister xmm_tmp3  = XMM_TMP3;
    const XMMRegister xmm_tmp4  = XMM_TMP4;

    const XMMRegister save_r12  = xmm4;
    const XMMRegister save_r13  = xmm5;
    const XMMRegister save_r14  = xmm6;
    const XMMRegister save_r15  = xmm7;
    const XMMRegister save_rbx  = xmm8;
    // xmm registers more valuable in inner loops...
    // const XMMRegister save_rsi  = xmm9;
    // const XMMRegister save_rdi  = xmm10;
    // const XMMRegister save_rcx  = xmm11;
    // const XMMRegister save_r8   = xmm12;
    // const XMMRegister save_r9   = xmm13;
    Label L_begin;

    Label L_returnRBP, L_checkRangeAndReturn, L_returnError;
    Label L_bigCaseFixupAndReturn, L_checkRangeAndReturnRCX;
    Label L_returnZero, L_copyHaystackToStackDone, L_bigSwitchTop;
    Label L_bigCaseDefault, L_smallCaseDefault, L_copyHaystackToStack, L_smallSwitchTop;

    Label L_wcharBegin, L_continue, L_wideNoExpand, L_copyDone, L_copyHigh;
    Label L_wideMidLoop, L_wideTopLoop, L_wideInnerLoop, L_wideFound;

    address jump_table;
    address jump_table_1;

    // Jump past jump table setups to get addresses of cases.

    __ align(CodeEntryAlignment);
    StubRoutines::_string_indexof_array[isLL   ? StrIntrinsicNode::LL
                                        : isUU ? StrIntrinsicNode::UU
                                               : StrIntrinsicNode::UL] =
        __ pc();
    __ enter();  // required for proper stackwalking of RuntimeStub frame
    __ jmp(L_begin);

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    //
    // Set up jump table entries for both small and large haystack switches.

    {
      Label L_returnRAX;

      const Register haystack = rbx;
      const Register needle = r14;
      const Register needle_val = rdi;
      const Register needle_val2 = rax;
      const Register set_bit = r11;
      const Register eq_mask = rsi;

      __ bind(L_returnRAX);
      __ movq(rbp, rax);
      __ jmp(L_returnRBP);

      ////////////////////////////////////////////////
      //  On entry to each case of small_hs, the register state is:
      //
      //  rbp = -1
      //  rbx = &haystack
      //  rcx = junk
      //  rdx = &needle
      //  rsi = haystack length
      //  rdi = &haystack
      //  r10 = hs_len - needle len
      //  r12 = needle length
      //  r14 = &needle
      //  XMM_BYTE_0 - first element of needle broadcast
      //  XMM_BYTE_K - last element of needle broadcast
      //
      //  The haystack is <= 32 bytes

      // Small case 1:
      small_hs_jmp_table[0] = __ pc();
      {
        string_indexof_small_loop_helper(1, L_returnRBP, ae);
        string_indexof_byte_compare_helper(
            1, L_returnRBP, L_checkRangeAndReturn, needle, needle_val, haystack,
            eq_mask, set_bit, needle_val, ae);
      }  // Small case 2:
      small_hs_jmp_table[1] = __ pc();
      {
        string_indexof_small_loop_helper(2, L_returnRBP, ae);
        string_indexof_byte_compare_helper(
            2, L_returnRBP, L_checkRangeAndReturn, needle, needle_val, haystack,
            eq_mask, set_bit, needle_val, ae);
      }
      // Small case 3:
      small_hs_jmp_table[2] = __ pc();

      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(3, L_returnRBP, ae);
        string_indexof_preload_needle_helper(3, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(3, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 4:
      small_hs_jmp_table[3] = __ pc();
      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(4, L_returnRBP, ae);
        string_indexof_preload_needle_helper(4, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(4, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 5:
      small_hs_jmp_table[4] = __ pc();

      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(5, L_returnRBP, ae);
        string_indexof_preload_needle_helper(5, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(5, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 6:
      small_hs_jmp_table[5] = __ pc();
      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(6, L_returnRBP, ae);
        string_indexof_preload_needle_helper(6, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(6, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 7:
      small_hs_jmp_table[6] = __ pc();
      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(7, L_returnRBP, ae);
        string_indexof_preload_needle_helper(7, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(7, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 8:
      small_hs_jmp_table[7] = __ pc();
      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(8, L_returnRBP, ae);
        string_indexof_preload_needle_helper(8, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(8, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 9:
      small_hs_jmp_table[8] = __ pc();

      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(9, L_returnRBP, ae);
        string_indexof_preload_needle_helper(9, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(9, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
      // Small case 10:
      small_hs_jmp_table[9] = __ pc();
      {
        Label L_loopTop, L_tmp;
        string_indexof_small_loop_helper(10, L_returnRBP, ae);
        string_indexof_preload_needle_helper(10, needle, needle_val, ae);

        __ align(8);
        __ bind(L_loopTop);
        string_indexof_byte_compare_helper(10, L_tmp, L_checkRangeAndReturn,
                                           needle, needle_val, haystack,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_loopTop);
        __ jmp(L_returnRBP);
      }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    //
    // Large haystack (>32 bytes) switch

    {
      const Register haystack = rbx;
      const Register hs_length = rsi;
      const Register incr = rdx;
      const Register r_Tmp = rax;
      const Register hs_ptr = rcx;
      const Register needle = r14;
      const Register needle_val = rdi;
      const Register eq_mask = rsi;
      const Register set_bit = r8;
      const Register hs_val = r9;

      // Big case 1:
      large_hs_jmp_table[0] = __ pc();
      if (isU) {
        __ emit_int8(0xcc);
      } else {
        Label L_notInFirst32, L_found, L_loopTop;

        string_indexof_big_loop_helper(1, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_byte_compare_helper(
            1, L_returnError, L_bigCaseFixupAndReturn, needle, needle_val,
            hs_ptr, eq_mask, set_bit, rax, ae);
      }

      // Big case 2:
      large_hs_jmp_table[1] = __ pc();
      {
        Label L_loopTop;

        string_indexof_big_loop_helper(2, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_byte_compare_helper(2, L_returnError, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr,
                                           eq_mask, set_bit, rax, ae);
      }

      // Big case 3:
      large_hs_jmp_table[2] = __ pc();

      if (isU) {
        __ emit_int8(0xcc);
      } else {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(3, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(3, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(3, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr,
                                           eq_mask, set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 4:
      large_hs_jmp_table[3] = __ pc();
      {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(4, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(4, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(4, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 5:
      large_hs_jmp_table[4] = __ pc();

      if (isU) {
        __ emit_int8(0xcc);
      } else {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(5, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(5, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(5, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 6:
      large_hs_jmp_table[5] = __ pc();
      {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(6, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(6, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(6, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 7:
      large_hs_jmp_table[6] = __ pc();

      if (isU) {
        __ emit_int8(0xCC);  // No odd needle sizes for UTF-16
      } else {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(7, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        // __ movl(needle_val, Address(needle, 1));
        // __ jmpb(L_tmp);

        // __ align(8);
        // __ bind(L_innerLoop);
        // __ blsrl(eq_mask, eq_mask);
        // __ je(L_loopTop);

        // __ bind(L_tmp);
        // __ tzcntl(set_bit, eq_mask);
        // __ cmpl(Address(hs_ptr, set_bit, Address::times_1, 1), needle_val);
        // __ jne(L_innerLoop);
        // __ movzbl(hs_val, Address(hs_ptr, set_bit, Address::times_1, 5));
        // __ cmpb(hs_val, Address(needle, 5));
        // __ jne(L_innerLoop);
        // __ jmp(L_small7_8_fixup);
        string_indexof_preload_needle_helper(7, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(7, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 8:
      large_hs_jmp_table[7] = __ pc();
      {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(8, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        // #if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
        //         if (isU) {    // 4 element needle - all compared already
        //           __ tzcntl(set_bit, eq_mask);
        //           __ jmp(L_bigCaseFixupAndReturn);
        //         } else {
        // #endif
        //         __ movl(needle_val, Address(needle, 1));
        //         __ jmpb(L_tmp);

        //         __ align(8);
        //         __ bind(L_innerLoop);
        //         CLEAR_BIT(eq_mask, set_bit);
        //         __ je(L_loopTop);

        //         __ bind(L_tmp);
        //         __ tzcntl(set_bit, eq_mask);
        //         __ cmpl(Address(hs_ptr, set_bit, Address::times_1, 1),
        //         needle_val);
        //         __ jne(L_innerLoop);
        //         __ movzwl(hs_val, Address(hs_ptr, set_bit, Address::times_1,
        //         5));
        //         __ cmpw(Address(needle, 5), hs_val);
        //         __ jne(L_innerLoop);

        //         __ bind(L_small7_8_fixup);
        //         __ subq(hs_ptr, haystack);
        //         __ addq(hs_ptr, set_bit);
        //         __ jmp(L_checkRangeAndReturnRCX);
        // #if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
        //         }
        // #endif
        string_indexof_preload_needle_helper(8, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(8, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 9:
      large_hs_jmp_table[8] = __ pc();

      if (isU) {
        __ emit_int8(0xcc);
      } else {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(9, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(9, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(9, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }

      // Big case 10:
      large_hs_jmp_table[9] = __ pc();
      {
        Label L_loopTop, L_innerLoop, L_tmp;

        string_indexof_big_loop_helper(10, L_checkRangeAndReturn, L_loopTop,
                                       haystack, hs_length, eq_mask, incr,
                                       needle, hs_ptr, r_Tmp, ae);
        string_indexof_preload_needle_helper(10, needle, needle_val, ae);

        __ align(8);
        __ bind(L_innerLoop);
        string_indexof_byte_compare_helper(10, L_tmp, L_bigCaseFixupAndReturn,
                                           needle, needle_val, hs_ptr, eq_mask,
                                           set_bit, rax, ae);
        __ bind(L_tmp);
        CLEAR_BIT(eq_mask, set_bit);
        __ jne(L_innerLoop);
        __ jmp(L_loopTop);
      }
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    __ align(8);

    jump_table = __ pc();

    for (jmp_ndx = 0; jmp_ndx < 10; jmp_ndx++) {
      __ emit_address(large_hs_jmp_table[jmp_ndx]);
    }

    jump_table_1 = __ pc();

    for (jmp_ndx = 0; jmp_ndx < 10; jmp_ndx++) {
      __ emit_address(small_hs_jmp_table[jmp_ndx]);
    }

    __ align(8);

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    //
    // Big case default:

    {
      Label L_loopTop, L_temp, L_innerLoop, L_found;

      ////////////////////////////////////////////////////////////////////////////////////////
      ////////////////////////////////////////////////////////////////////////////////////////
      ////////////////////////////////////////////////////////////////////////////////////////
      //
      // Big case default:
      //
      //  rbx: haystack
      //  rcx: k
      //  rdx: junk
      //  rsi: n
      //  rdi: haystack
      //  r10: n - k
      //  r12: k
      //  r13: junk
      //  r14: needle
      //  rbp: -1
      //  XMM_BYTE_0 - first element of needle broadcast
      //  XMM_BYTE_K - last element of needle broadcast

      const Register rTmp = rax;
      const Register haystack = rbx;
      const Register saveNeedleAddress = rbx; // NOTE re-use
      const Register origNeedleLen = rcx;
      const Register firstNeedleCompare = rdx;
      const Register hsLen = rsi;
      const Register origHsLen = rsi; // NOTE re-use
      const Register rTmp2 = rdi;
      const Register mask = rbp;
      const Register rScratch = r8;
      const Register compLen = r9;
      const Register needleLen = r12;
      const Register hsIndex = r12; // NOTE re-use
      const Register constOffset = r13;
      const Register needle = r14;
      const Register index = r14; // NOTE re-use
      const Register haystackEnd = r15;

      const XMMRegister cmp_0 = xmm_tmp3;
      const XMMRegister cmp_k = xmm_tmp4;
      const XMMRegister result = xmm_tmp3;

      const XMMRegister saveCompLen = xmm_tmp2;
      const XMMRegister saveIndex = xmm_tmp1;

      __ bind(L_bigCaseDefault);
      __ movq(r11, -1);

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
      // Add compare for second element
      if (isU) {
        __ vpbroadcastw(byte_1, Address(needle, 2), Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_1, Address(needle, 1), Assembler::AVX_256bit);
      }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
      // Add compare for third element
      if (isU) {
        __ vpbroadcastw(byte_2, Address(needle, 4), Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_2, Address(needle, 2), Assembler::AVX_256bit);
      }
#endif

      __ leaq(haystackEnd, Address(haystack, hsLen, Address::times_1));

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
      __ leaq(firstNeedleCompare, Address(needle, isU ? 2 : 1));
      __ leaq(compLen, Address(needleLen, isU ? -0x4 : -0x2)); // nlen - 2 elements
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
      __ leaq(firstNeedleCompare, Address(needle, isU ? 4 : 2));
      __ leaq(compLen, Address(needleLen, isU ? -0x6 : -0x3)); // nlen - 3 elements
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
      __ leaq(firstNeedleCompare, Address(needle, isU ? 6 : 3));
      __ leaq(compLen, Address(needleLen, isU ? -0x8 : -0x4)); // nlen - 4 elements
#endif
      //  firstNeedleCompare has address of second element of needle
      //  compLen has length of comparison to do

      __ movq(Address(rsp, SAVED_HAYSTACK_STACK_OFFSET), haystack);   // Save haystack

      __ movq(index, origHsLen);
      __ negptr(index); // incr
      __ movq(constOffset, isU ? -30 : -31);
      __ subq(constOffset, origNeedleLen); // constant offset from end for full 32-byte read
      __ jmpb(L_temp);

      __ bind(L_loopTop);
      __ addq(index, 32);
      __ subq(origHsLen, 32);
      __ jle(L_returnError);
      __ cmpq(index, constOffset);
      __ cmovq(Assembler::greater, index, constOffset);

      __ bind(L_temp);
      __ movq(hsIndex, origNeedleLen);
      __ addq(hsIndex, index);

      // Compare first byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_0, byte_0, Address(haystackEnd, index), Assembler::AVX_256bit);
        // Compare last byte of needle to haystack at proper position
        __ vpcmpeqw(cmp_k, byte_k, Address(haystackEnd, hsIndex, Address::times_1, -2),
                    Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_0, byte_0, Address(haystackEnd, index), Assembler::AVX_256bit);
        // Compare last byte of needle to haystack at proper position
        __ vpcmpeqb(cmp_k, byte_k, Address(haystackEnd, hsIndex, Address::times_1, -1),
                    Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, cmp_0, Assembler::AVX_256bit);
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
#if EARLY_BAILOUT > 0
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je_b(L_loopTop);
#endif
      // Compare second byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_1, Address(haystackEnd, index, Address::times_1, 2), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_1, Address(haystackEnd, index, Address::times_1, 1), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
#if EARLY_BAILOUT > 0
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je_b(L_loopTop);
#endif
      // Compare third byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_2, Address(haystackEnd, index, Address::times_1, 4), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_2, Address(haystackEnd, index, Address::times_1, 2), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
#endif
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je_b(L_loopTop);

      __ align(8);
      __ bind(L_innerLoop);
      __ tzcntl(rTmp, mask);

      __ movdq(saveIndex, rTmp);
      __ movdq(saveCompLen, compLen);
      __ movq(saveNeedleAddress, firstNeedleCompare);  // Save address of nth element of needle

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
      __ leaq(rTmp2, Address(haystackEnd, index, Address::times_1, isU ? 2 : 1));
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
      __ leaq(rTmp2, Address(haystackEnd, index, Address::times_1, isU ? 4 : 2));
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
      __ leaq(rTmp2, Address(haystackEnd, index, Address::times_1, isU ? 6 : 3));
#endif
      __ addq(rTmp2, rTmp);
      __ arrays_equals(false, rTmp2, firstNeedleCompare, compLen, rTmp, rScratch, xmm_tmp3, xmm_tmp4,
                       false /* char */, knoreg);
      __ testl(rTmp, rTmp);
      __ jne_b(L_found);

      __ movdq(compLen, saveCompLen);
      __ movq(firstNeedleCompare, saveNeedleAddress);
      CLEAR_BIT(mask, rTmp);
      __ jne(L_innerLoop);
      __ jmp(L_loopTop);

      __ bind(L_found);
      __ movdq(rTmp, saveIndex);
      __ leaq(rScratch, Address(haystackEnd, index, Address::times_1));
      __ subq(rScratch, Address(rsp, SAVED_HAYSTACK_STACK_OFFSET));
      __ addq(rScratch, rTmp);
      __ movq(r11, rScratch);
      __ jmp(L_checkRangeAndReturn);
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    //
    // Small case default:
    //
    //  rbx: haystack
    //  r14: needle
    //  r13: k - 1
    //  r12: k
    //  r10: n - k
    //  rbp: -1
    //  rdi: junk
    //  rsi: n
    //  rdx: junk
    //  rcx: junk
    //  XMM_BYTE_0 - first element of needle broadcast
    //  XMM_BYTE_K - last element of needle broadcast
    //
    //  Haystack always copied to stack, so 32-byte reads OK
    //  Haystack length < 32
    //  10 < needle length < 32

    {
      __ bind(L_smallCaseDefault);

      Label L_innerLoop;

      const Register needle = r14;
      const Register saveCompLen = r14; // NOTE re-use
      const Register needleLen = r12;
      const Register saveNeedleAddress = r12; // NOTE re-use
      const Register firstNeedleCompare = rdx;
      const Register compLen = r9;
      const Register haystack = rbx;
      const Register mask = r8;
      const Register rTmp = rdi;
      const Register rTmp2 = r13;
      const Register rTmp3 = rax;

      const XMMRegister cmp_0 = xmm_tmp3;
      const XMMRegister cmp_k = xmm_tmp4;
      const XMMRegister result = xmm_tmp3;

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
      // Add compare for second byte
      if (isU) {
        __ vpbroadcastw(byte_1, Address(needle, 2), Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_1, Address(needle, 1), Assembler::AVX_256bit);
      }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
      // Add compare for third byte
      if (isU) {
        __ vpbroadcastw(byte_2, Address(needle, 4), Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_2, Address(needle, 2), Assembler::AVX_256bit);
      }
#endif

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
      __ leaq(firstNeedleCompare, Address(needle, isU ? 2 : 1));      //////////////////////////////////////////////////
      __ leaq(compLen, Address(needleLen, isU ? -0x4 : -0x2)); // nlen - 2 elements
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
      __ leaq(firstNeedleCompare, Address(needle, isU ? 4 : 2));      //////////////////////////////////////////////////
      __ leaq(compLen, Address(needleLen, isU ? -0x6 : -0x3)); // nlen - 3 elements
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
      __ leaq(firstNeedleCompare, Address(needle, isU ? 6 : 3));      //////////////////////////////////////////////////
      __ leaq(compLen, Address(needleLen, isU ? -0x8 : -0x4)); // nlen - 4 elements
#endif
      //  firstNeedleCompare has address of second element of needle
      //  compLen has length of comparison to do


      // Compare first byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_0, byte_0, Address(haystack, 0), Assembler::AVX_256bit);
        // Compare last byte of needle to haystack at proper position
        __ vpcmpeqw(cmp_k, byte_k, Address(haystack, needleLen, Address::times_1, -2),
                    Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_0, byte_0, Address(haystack, 0), Assembler::AVX_256bit);
        // Compare last byte of needle to haystack at proper position
        __ vpcmpeqb(cmp_k, byte_k, Address(haystack, needleLen, Address::times_1, -1),
                    Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, cmp_0, Assembler::AVX_256bit);
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 2
#if EARLY_BAILOUT > 0
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je(L_returnRBP);
#endif
      // Compare second byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_1, Address(haystack, 2), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_1, Address(haystack, 1), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE > 3
#if EARLY_BAILOUT > 0
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je(L_returnRBP);
#endif
      // Compare third byte of needle to haystack
      if (isU) {
        __ vpcmpeqw(cmp_k, byte_2, Address(haystack, 4), Assembler::AVX_256bit);
      } else {
        __ vpcmpeqb(cmp_k, byte_2, Address(haystack, 2), Assembler::AVX_256bit);
      }
      __ vpand(result, cmp_k, result, Assembler::AVX_256bit);
#endif
      __ vpmovmskb(mask, result, Assembler::AVX_256bit);
      __ testl(mask, mask);
      __ je(L_returnRBP);   // Not found

      __ movq(saveCompLen, compLen);
      __ movq(saveNeedleAddress, firstNeedleCompare);  // Save address of 2nd element of needle

      __ align(8);
      __ bind(L_innerLoop);
      __ tzcntl(r11, mask);

#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE <= 2
      __ leaq(rTmp, Address(haystack, r11, Address::times_1, isU ? 2 : 1));
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 3
      __ leaq(rTmp, Address(haystack, r11, Address::times_1, isU ? 4 : 2));
#endif
#if NUMBER_OF_NEEDLE_BYTES_TO_COMPARE == 4
      __ leaq(rTmp, Address(haystack, r11, Address::times_1, isU ? 6 : 3));
#endif
      __ arrays_equals(false, rTmp, firstNeedleCompare, compLen, rTmp3, rTmp2, xmm_tmp3, xmm_tmp4,
                       false /* char */, knoreg);
      __ testl(rTmp3, rTmp3);
      __ jne_b(L_checkRangeAndReturn);

      __ movq(compLen, saveCompLen);
      __ movq(firstNeedleCompare, saveNeedleAddress);
      CLEAR_BIT(mask, rTmp3);
      __ jne(L_innerLoop);
      __ jmp(L_returnRBP);

    }

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////

    __ bind(L_returnError);
    __ movq(rbp, -1);
    __ jmpb(L_returnRBP);

    __ bind(L_returnZero);
    __ xorl(rbp, rbp);
    __ jmpb(L_returnRBP);

    __ bind(L_bigCaseFixupAndReturn);
    __ movl(rax, r8);
    __ subq(rcx, rbx);
    __ addq(rcx, rax);

    __ bind(L_checkRangeAndReturnRCX);
    __ movq(r11, rcx);

    __ bind(L_checkRangeAndReturn);
    __ cmpq(r11, r10);
    __ movq(rbp, -1);
    __ cmovq(Assembler::belowEqual, rbp, r11);

    __ bind(L_returnRBP);
    __ movq(rax, rbp);
    __ addptr(rsp, STACK_SPACE);
    __ pop(rbp);
#ifdef _WIN64
    __ pop(r9);
    __ pop(r8);
    __ pop(rcx);
    __ pop(rdi);
    __ pop(rsi);
#endif
#ifdef PUSH_REGS
    __ pop(rbx);
    __ pop(r12);
    __ pop(r13);
    __ pop(r14);
    __ pop(r15);
#else
    __ movdq(r12, save_r12);
    __ movdq(r13, save_r13);
    __ movdq(r14, save_r14);
    __ movdq(r15, save_r15);
    __ movdq(rbx, save_rbx);
#endif
    if (isU) {
      __ sarq(rax, 1);
    }
    __ vzeroupper();

    __ leave();  // required for proper stackwalking of RuntimeStub frame
    __ ret(0);

    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////

    __ bind(L_begin);
#ifdef PUSH_REGS
    __ push(r15);
    __ push(r14);
    __ push(r13);
    __ push(r12);
    __ push(rbx);
#else
    __ movdq(save_r12, r12);
    __ movdq(save_r13, r13);
    __ movdq(save_r14, r14);
    __ movdq(save_r15, r15);
    __ movdq(save_rbx, rbx);
#endif
#ifdef _WIN64
    __ push(rsi);
    __ push(rdi);
    __ push(rcx);
    __ push(r8);
    __ push(r9);

    __ movq(rdi, rcx);
    __ movq(rsi, rdx);
    __ movq(rdx, r8);
    __ movq(rcx, r9);
#endif

    __ push(rbp);
    __ subptr(rsp, STACK_SPACE);
    // if (k == 0) {
    //   return 0;
    // }
    __ testq(needle_len, needle_len);
    __ je(L_returnZero);

    __ testq(haystack_len, haystack_len);
    __ je(L_returnError);

    __ movq(rbp, -1);

    if (isUL) {
      // Branch out if doing wide chars
      __ jmp(L_wcharBegin);
    }

    if (isUU) {  // Adjust sizes of hs and needle
      __ shlq(needle_len, 1);
      __ shlq(haystack_len, 1);
    }

    // wide char processing comes here after expanding needle
    __ bind(L_continue);
    __ movq(r10, haystack_len);
    __ subq(r10, needle_len);
    __ jl(L_returnRBP);

    __ movq(save_ndl_len, needle_len);
    __ movq(r14, needle);
    __ movq(rbx, haystack);

    {
      Label L_short;

      // Always need needle broadcast to ymm registers
      if (isU) {
        __ vpbroadcastw(byte_0, Address(needle, 0), Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_0, Address(needle, 0), Assembler::AVX_256bit);
      }

      // __ cmpq(needle_len, isU ? 2 : 1);
      // __ je_b(L_short);

      if (isU) {
        __ vpbroadcastw(byte_k, Address(needle, needle_len, Address::times_1, -2),
                        Assembler::AVX_256bit);
      } else {
        __ vpbroadcastb(byte_k, Address(needle, needle_len, Address::times_1, -1),
                        Assembler::AVX_256bit);
      }

      // __ bind(L_short);
    }

    __ cmpq(haystack_len, 0x20);
    __ jae_b(L_bigSwitchTop);

    {
      Label L_moreThan16, L_adjustHaystack;

      const Register index = rax;
      const Register haystack = rbx;

      __ bind(L_copyHaystackToStack);
      __ cmpq(haystack_len, 0x10);
      __ ja_b(L_moreThan16);

      __ movq(index, COPIED_HAYSTACK_STACK_OFFSET + 0x10);
      __ movdqu(xmm_tmp1, Address(haystack, haystack_len, Address::times_1, -0x10));
      __ movdqu(Address(rsp, COPIED_HAYSTACK_STACK_OFFSET), xmm_tmp1);
      __ jmpb(L_adjustHaystack);

      __ bind(L_moreThan16);
      __ movq(index, COPIED_HAYSTACK_STACK_OFFSET + 0x20);
      __ vmovdqu(xmm_tmp1, Address(haystack, haystack_len, Address::times_1, -0x20));
      __ vmovdqu(Address(rsp, COPIED_HAYSTACK_STACK_OFFSET), xmm_tmp1);

      __ bind(L_adjustHaystack);
      __ subq(index, haystack_len);
      __ leaq(haystack, Address(rsp, index, Address::times_1));
    }

    __ bind(L_smallSwitchTop);
    __ leaq(r13, Address(save_ndl_len, -1));
    __ cmpq(r13, 0x9);
    __ ja(L_smallCaseDefault);
    __ mov64(r15, (int64_t)jump_table_1);
    __ jmp(Address(r15, r13, Address::times_8));

    __ bind(L_bigSwitchTop);
    __ leaq(rax, Address(save_ndl_len, -1));
    __ cmpq(rax, 0x9);
    __ ja(L_bigCaseDefault);
    __ mov64(r15, (int64_t)jump_table);
    __ jmp(Address(r15, rax, Address::times_8));

    if (isUL) {
      ////////////////////////////////////////////////////////////////////////////////////////
      ////////////////////////////////////////////////////////////////////////////////////////
      //                         Wide char code
      ////////////////////////////////////////////////////////////////////////////////////////
      ////////////////////////////////////////////////////////////////////////////////////////
      //
      // Pseudo-code:
      //
      // If needle length less than MAX_NEEDLE_LEN_TO_EXPAND, read the needle bytes from r14
      // and write them as words onto the stack.  Then go to the "regular" code.  This is
      // equavilent to doing a UU comparison, since the haystack will be in UTF-16.
      //
      // If the needle can't be expanded, process the same way as the default cases above.
      // That is, for each haystack chunk, compare the needle.
      __ bind(L_wcharBegin);

      assert(MAX_NEEDLE_LEN_TO_EXPAND >= 32, "Small UL needles not supported");

      __ shlq(haystack_len, 1);

      __ movq(r14, haystack_len);
      __ leaq(r10, Address(needle_len, needle_len, Address::times_1));
      __ cmpq(r10, haystack_len);
      __ jg(L_returnRBP);

      __ movq(r12, needle);
      __ movq(r13, haystack);
      __ cmpq(needle_len, MAX_NEEDLE_LEN_TO_EXPAND);
      __ ja(L_wideNoExpand);

      __ movq(rdx, -1);
      __ addq(rdx, needle_len);  // k - 1

      // __ shrq(rdx, 1);

      __ movq(rax, rdx);
      __ shrq(rax, 4);  // high
      __ andl(rdx, -16);
      __ subl(rcx, rdx);                  // low
      __ leal(rdx, Address(rcx, -0x10));  // low - 16
      __ movslq(r14, rdx);
      __ addl(rcx, rcx);
      __ movl(rdx, 0x20);
      __ vpmovzxbw(xmm0, Address(r12, r14, Address::times_1),
                   Assembler::AVX_256bit);  // load needle[low-16]
      __ subl(rdx, rcx);
      __ vmovdqu(Address(rsp, EXPANDED_NEEDLE_STACK_OFFSET), xmm0);  // store to stack
      __ testl(rax, rax);
      __ je_b(L_copyDone);

      __ addq(r12, r14);
      __ movl(rcx, 0x10);

      __ align(8);
      __ bind(L_copyHigh);
      __ vpmovzxbw(xmm0, Address(r12, rcx, Address::times_1),
                   Assembler::AVX_256bit);  // load needle[low-16]
      __ vmovdqu(Address(rsp, rcx, Address::times_2, EXPANDED_NEEDLE_STACK_OFFSET),
                 xmm0);  // store to stack
      __ addq(rcx, 0x10);
      __ decl(rax);
      __ jne(L_copyHigh);

      // Needle copied
      __ bind(L_copyDone);

      // Jump to L_continue here after restoring state
      __ movslq(rax, rdx);
      __ leaq(needle, Address(rsp, rax, Address::times_1));
      __ addq(needle, EXPANDED_NEEDLE_STACK_OFFSET);
      __ movq(needle_len, r10);
      __ jmp(L_continue);

      // Prepare for wchar anysize
      __ bind(L_wideNoExpand);

      __ movq(r15, r12);
      __ leaq(rax, Address(r14, r13, Address::times_1));  // &hs[hslen*2]
      __ movq(Address(rsp, SAVED_TERM_ADDR_STACK_OFFSET), rax);  // Save terminating address
      __ movzbl(rax, Address(r15));   // First byte of needle
      __ movdl(xmm0, rax);
      __ vpbroadcastw(xmm0, xmm0, Assembler::AVX_256bit); // 1st byte of needle in words
      __ shrq(r10, 1);
      __ movzbl(rax, Address(r10, r15, Address::times_1, -0x1));  // Last byte of needle
      __ shlq(r10, 1);
      __ movdl(xmm1, rax);
      __ vpbroadcastw(xmm1, xmm1, Assembler::AVX_256bit); // Last byte of needle in words
      __ movl(rax, r14);  // Calculate incr
      __ subl(rax, r10);
      // __ incl(rax);   // hslen*2 - needlelen*2 + 1 --- for 1st increment    // ASGASG
      __ andl(rax, 0x1f);
      __ cmpq(r14, 0x21);
      __ movl(rcx, 0x20);
      __ cmovl(Assembler::aboveEqual, rcx, rax);  // Increment for 1st haystack read
      __ addq(r15, 1);  // Increment needle
      __ leaq(r14, Address(r10, -0x4)); // Length of comparison
      __ movq(Address(rsp, SAVED_HAYSTACK_STACK_OFFSET), r13);  // Save haystack
      __ movq(rbx, r13);
      __ jmpb(L_wideMidLoop);

      __ bind(L_wideTopLoop);

      __ addq(rbx, Address(rsp, SAVED_INCREMENT_STACK_OFFSET));  // Add increment
      __ movl(rcx, 0x20);
      __ cmpq(rbx, Address(rsp, SAVED_TERM_ADDR_STACK_OFFSET));  // Loop termination
      __ jae(L_returnRBP);

      __ bind(L_wideMidLoop);

      __ movq(Address(rsp, SAVED_INCREMENT_STACK_OFFSET), rcx);  // update increment
      __ vpcmpeqw(xmm_tmp3, xmm0, Address(rbx), Assembler::AVX_256bit);
      __ vpcmpeqw(xmm_tmp4, xmm1, Address(rbx, r10, Address::times_1, -0x2),
                  Assembler::AVX_256bit);
      __ vpand(xmm_tmp3, xmm_tmp3, xmm_tmp4, Assembler::AVX_256bit);
      __ vpmovmskb(r12, xmm_tmp3, Assembler::AVX_256bit); // compare first & last words of needle
      __ testl(r12, r12);
      __ je(L_wideTopLoop);

      __ leaq(r13, Address(rbx, 0x2));  // First byte of hs to compare

      __ bind(L_wideInnerLoop);
      __ tzcntl(r8, r12);
      __ movq(rdi, r13);
      __ addq(rdi, r8);   // First word to compare in hs
      __ movdq(xmm_tmp2, r14); // Save stomped registers
      __ movdq(xmm_tmp1, r15);
      __ shrq(r14, 1);
      __ arrays_equals(false, rdi, r15, r14, rax, r9, xmm_tmp3, xmm_tmp4,
                       false /* char */, knoreg, true /* expand_ary2 */);
      __ movdq(r14, xmm_tmp2);
      __ movdq(r15, xmm_tmp1);
      __ testl(rax, rax);
      __ jne_b(L_wideFound);

      __ blsrl(rax, r12); // Clear 2 bits - word compares
      __ blsrl(r12, rax);
      __ jne(L_wideInnerLoop);
      __ jmp(L_wideTopLoop);

      __ bind(L_wideFound);
      __ subq(rbx, Address(rsp, SAVED_HAYSTACK_STACK_OFFSET)); // curr_hs - hs
      __ addq(rbx, r8); // add offset of match
      __ movq(rbp, rbx);
      __ jmp(L_returnRBP);
    }

#undef STACK_SPACE
#undef MAX_NEEDLE_LEN_TO_EXPAND
#undef CLEAR_BIT

  } else {  // SSE version
    assert(false, "Only supports AVX2");
  }

  return;
}

#undef __
#undef XMM_BYTE_0
#undef XMM_BYTE_K
#undef XMM_BYTE_1
#undef XMM_BYTE_2
#undef XMM_TMP1
#undef XMM_TMP2
#undef XMM_TMP3
#undef XMM_TMP4
