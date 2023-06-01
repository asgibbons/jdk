/*
 * Copyright (c) 2023, Intel Corporation. All rights reserved.
 * Intel Math Library (LIBM) Source Code
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

#include "precompiled.hpp"
#include "macroAssembler_x86.hpp"
#include "stubGenerator_x86_64.hpp"

/******************************************************************************/
//                     ALGORITHM DESCRIPTION - FMOD()
//                     ---------------------
//
// If either value1 or value2 is NaN, the result is NaN.
//
// If neither value1 nor value2 is NaN, the sign of the result equals the sign of the dividend.
//
// If the dividend is an infinity or the divisor is a zero or both, the result is NaN.
//
// If the dividend is finite and the divisor is an infinity, the result equals the dividend.
//
// If the dividend is a zero and the divisor is finite, the result equals the dividend.
//
// In the remaining cases, where neither operand is an infinity, a zero, or NaN, the floating-point
// remainder result from a dividend value1 and a divisor value2 is defined by the mathematical
// relation result = value1 - (value2 * q), where q is an integer that is negative only if
// value1 / value2 is negative, and positive only if value1 / value2 is positive, and whose magnitude
// is as large as possible without exceeding the magnitude of the true mathematical quotient of value1 and value2.
//
/******************************************************************************/

#define __ _masm->

ATTRIBUTE_ALIGNED(32) static const uint64_t CONST_NaN[] = {
    0x7FFFFFFFFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL   // NaN vector
};
ATTRIBUTE_ALIGNED(32) static const uint64_t CONST_1p260[] = {
    0x5030000000000000ULL,    // 0x1p+260
};

ATTRIBUTE_ALIGNED(32) static const uint64_t CONST_MAX[] = {
    0x7FEFFFFFFFFFFFFFULL,    // Max
};

ATTRIBUTE_ALIGNED(32) static const uint64_t CONST_INF[] = {
    0x7FF0000000000000ULL,    // Inf
};

ATTRIBUTE_ALIGNED(32) static const uint64_t CONST_e307[] = {
    0x7FE0000000000000ULL
};

address StubGenerator::generate_libmFmod() {
  StubCodeMark mark(this, "StubRoutines", "libmFmod");
  address start = __ pc();
  __ enter(); // required for proper stackwalking of RuntimeStub frame

  if (VM_Version::supports_avx512vlbwdq()) {     // AVX512 version

#if 1

  Label L_5280, L_52a0, L_5256, L_5300, L_5320, L_52c0, L_52d0, L_5360, L_5380, L_53b0, L_5390;
  Label L_53c0, L_52a6, L_53d0, L_exit;

//    0x0000555555555200 <+0>:     c5 f9 6f d0                     vmovdqa xmm2,xmm0
//    0x0000555555555204 <+4>:     c5 fa 7e c0                     vmovq  xmm0,xmm0
//    0x0000555555555208 <+8>:     c4 e2 79 59 1d 0f 0e 00 00      vpbroadcastq xmm3,QWORD PTR [rip+0xe0f]        # CONST_NaN
  __ movdqa(xmm2, xmm0);
  __ movq(xmm0, xmm0);
  __ mov64(rax, 0x7FFFFFFFFFFFFFFF);
  __ evpbroadcastq(xmm3, rax, Assembler::AVX_128bit);
//    0x0000555555555211 <+17>:    c5 f9 db f3                     vpand  xmm6,xmm0,xmm3
//    0x0000555555555215 <+21>:    c5 f1 db e3                     vpand  xmm4,xmm1,xmm3
//    0x0000555555555219 <+25>:    c5 c9 ef d8                     vpxor  xmm3,xmm6,xmm0
//    0x000055555555521d <+29>:    c5 fa 7e ec                     vmovq  xmm5,xmm4
//    0x0000555555555221 <+33>:    62 f1 cf 78 5e c5               vdivsd xmm0,xmm6,xmm5,{rz-sae}
//    0x0000555555555227 <+39>:    c5 fa 7e c0                     vmovq  xmm0,xmm0
//    0x000055555555522b <+43>:    c5 c1 57 ff                     vxorpd xmm7,xmm7,xmm7
//    0x000055555555522f <+47>:    c4 e3 41 0b c0 0b               vroundsd xmm0,xmm7,xmm0,0xb
//    0x0000555555555235 <+53>:    c4 e3 79 17 c0 01               vextractps eax,xmm0,0x1
//    0x000055555555523b <+59>:    85 c0                           test   eax,eax
//    0x000055555555523d <+61>:    74 41                           je     0x555555555280 <fmod+128>
  __ vpand(xmm6, xmm0, xmm3, Assembler::AVX_128bit);
  __ vpand(xmm4, xmm1, xmm3, Assembler::AVX_128bit);
  __ vpxor(xmm3, xmm6, xmm0, Assembler::AVX_128bit);
  __ movq(xmm5, xmm4);
  __ evdivsd(xmm0, xmm6, xmm5, Assembler::EVEX_RZ);
  __ movq(xmm0, xmm0);
  __ vxorpd(xmm7, xmm7, xmm7, Assembler::AVX_128bit);
  __ vroundsd(xmm0, xmm7, xmm0, 0xb);
  __ extractps(rax, xmm0, 1);
  __ testl(rax, rax);
  __ jcc(Assembler::equal, L_5280);

//    0x000055555555523f <+63>:    3d fe ff ef 7f                  cmp    eax,0x7feffffe
//    0x0000555555555244 <+68>:    76 5a                           jbe    0x5555555552a0 <fmod+160>
//    0x0000555555555246 <+70>:    c5 e9 ef d2                     vpxor  xmm2,xmm2,xmm2
//    0x000055555555524a <+74>:    c5 f9 2e e2                     vucomisd xmm4,xmm2
//    0x000055555555524e <+78>:    75 06                           jne    0x555555555256 <fmod+86>
//    0x0000555555555250 <+80>:    0f 8b aa 00 00 00               jnp    0x555555555300 <fmod+256>
//    0x0000555555555256 <+86>:    c5 fb 10 15 ca 0d 00 00         vmovsd xmm2,QWORD PTR [rip+0xdca]        # CONST_MAX
//    0x000055555555525e <+94>:    c5 f9 2e d6                     vucomisd xmm2,xmm6
//    0x0000555555555262 <+98>:    0f 82 98 00 00 00               jb     0x555555555300 <fmod+256>
//    0x0000555555555268 <+104>:   c5 fb 10 05 c0 0d 00 00         vmovsd xmm0,QWORD PTR [rip+0xdc0]        # CONST_INF
//    0x0000555555555270 <+112>:   c5 f9 2e c4                     vucomisd xmm0,xmm4
//    0x0000555555555274 <+116>:   0f 83 a6 00 00 00               jae    0x555555555320 <fmod+288>
//    0x000055555555527a <+122>:   c5 f3 58 c1                     vaddsd xmm0,xmm1,xmm1
//    0x000055555555527e <+126>:   c3                              ret
//    0x000055555555527f <+127>:   90                              nop
  __ cmpl(rax, 0x7feffffe);
  __ jcc(Assembler::belowEqual, L_52a0);
  __ vpxor(xmm2, xmm2, xmm2, Assembler::AVX_128bit);
  __ ucomisd(xmm4, xmm2);
  __ jcc(Assembler::notEqual, L_5256);
  __ jcc(Assembler::noParity, L_5300);

  __ bind(L_5256);
  __ movsd(xmm2, ExternalAddress((address)CONST_MAX), rax);
  __ ucomisd(xmm2, xmm6);
  __ jcc(Assembler::below, L_5300);
  __ movsd(xmm0, ExternalAddress((address)CONST_INF), rax);
  __ ucomisd(xmm0, xmm4);
  __ jcc(Assembler::aboveEqual, L_5320);

  __ vaddsd(xmm0, xmm1, xmm1);
  __ jmp(L_exit);
//    0x0000555555555280 <+128>:   c5 e3 58 c2                     vaddsd xmm0,xmm3,xmm2
//    0x0000555555555284 <+132>:   c3                              ret
//    0x0000555555555285 <+133>:   66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x0000555555555294 <+148>:   66 66 66 2e 0f 1f 84 00 00 00 00 00     data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x00005555555552a0 <+160>:   62 f2 dd 78 ad c6               vfnmadd213sd xmm0,xmm4,xmm6,{rz-sae}
//    0x00005555555552a6 <+166>:   c5 f9 2e c4                     vucomisd xmm0,xmm4
//    0x00005555555552aa <+170>:   73 14                           jae    0x5555555552c0 <fmod+192>
//    0x00005555555552ac <+172>:   c5 e1 ef c0                     vpxor  xmm0,xmm3,xmm0
//    0x00005555555552b0 <+176>:   c3                              ret
  __ align32();
  __ bind(L_5280);
  __ vaddsd(xmm0, xmm3, xmm2);
  __ jmp(L_exit);

  __ align(8);
  __ bind(L_52a0);
  __ evfnmadd213sd(xmm0, xmm4, xmm6, Assembler::EVEX_RZ);
  __ bind(L_52a6);
  __ ucomisd(xmm0, xmm4);
  __ jcc(Assembler::aboveEqual, L_52c0);
  __ vpxor(xmm0, xmm3, xmm0, Assembler::AVX_128bit);
  __ jmp(L_exit);

  __ bind(L_52c0);
//    0x00005555555552b1 <+177>:   66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x00005555555552c0 <+192>:   c5 fa 7e f0                     vmovq  xmm6,xmm0
//    0x00005555555552c4 <+196>:   c5 f1 ef c9                     vpxor  xmm1,xmm1,xmm1
//    0x00005555555552c8 <+200>:   0f 1f 84 00 00 00 00 00         nop    DWORD PTR [rax+rax*1+0x0]
//    0x00005555555552d0 <+208>:   62 f1 cf 78 5e d5               vdivsd xmm2,xmm6,xmm5,{rz-sae}
//    0x00005555555552d6 <+214>:   c5 fa 7e d2                     vmovq  xmm2,xmm2
//    0x00005555555552da <+218>:   c4 e3 71 0b d2 0b               vroundsd xmm2,xmm1,xmm2,0xb
//    0x00005555555552e0 <+224>:   62 f2 dd 78 ad d0               vfnmadd213sd xmm2,xmm4,xmm0,{rz-sae}
//    0x00005555555552e6 <+230>:   c5 f9 2e d4                     vucomisd xmm2,xmm4
//    0x00005555555552ea <+234>:   c5 fa 7e f2                     vmovq  xmm6,xmm2
//    0x00005555555552ee <+238>:   c5 f9 28 c2                     vmovapd xmm0,xmm2
//    0x00005555555552f2 <+242>:   73 dc                           jae    0x5555555552d0 <fmod+208>
//    0x00005555555552f4 <+244>:   c5 e1 ef c2                     vpxor  xmm0,xmm3,xmm2
//    0x00005555555552f8 <+248>:   c3                              ret
  __ movq(xmm6, xmm0);
  __ vpxor(xmm1, xmm1, xmm1, Assembler::AVX_128bit);
  __ align32();
  __ bind(L_52d0);

  __ evdivsd(xmm2, xmm6, xmm5, Assembler::EVEX_RZ);
  __ movq(xmm2, xmm2);
  __ vroundsd(xmm2, xmm1, xmm2, 0xb);
  __ evfnmadd213sd(xmm2, xmm4, xmm0, Assembler::EVEX_RZ);
  __ ucomisd(xmm2, xmm4);
  __ movq(xmm6, xmm2);
  __ movapd(xmm0, xmm2);
  __ jcc(Assembler::aboveEqual, L_52d0);

  __ vpxor(xmm0, xmm3, xmm2, Assembler::AVX_128bit);
  __ jmp(L_exit);
//    0x00005555555552f9 <+249>:   0f 1f 80 00 00 00 00            nop    DWORD PTR [rax+0x0]
//    0x0000555555555300 <+256>:   c4 e2 d9 ad c6                  vfnmadd213sd xmm0,xmm4,xmm6
//    0x0000555555555305 <+261>:   c3                              ret
  __ bind(L_5300);
  __ vfnmadd213sd(xmm0, xmm4, xmm6);
  __ jmp(L_exit);
//    0x0000555555555306 <+262>:   66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x0000555555555315 <+277>:   66 66 2e 0f 1f 84 00 00 00 00 00        data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x0000555555555320 <+288>:   c5 db 59 0d 10 0d 00 00         vmulsd xmm1,xmm4,QWORD PTR [rip+0xd10]        # CONST_e307
//    0x0000555555555328 <+296>:   c5 fa 7e d1                     vmovq  xmm2,xmm1
//    0x000055555555532c <+300>:   62 f1 cf 78 5e c2               vdivsd xmm0,xmm6,xmm2,{rz-sae}
//    0x0000555555555332 <+306>:   c5 fa 7e c0                     vmovq  xmm0,xmm0
//    0x0000555555555336 <+310>:   c4 e3 41 0b f8 0b               vroundsd xmm7,xmm7,xmm0,0xb
//    0x000055555555533c <+316>:   c4 e3 79 17 f8 01               vextractps eax,xmm7,0x1
//    0x0000555555555342 <+322>:   3d ff ff ef 7f                  cmp    eax,0x7fefffff
//    0x0000555555555347 <+327>:   72 17                           jb     0x555555555360 <fmod+352>
//    0x0000555555555349 <+329>:   c5 f3 59 05 e7 0c 00 00         vmulsd xmm0,xmm1,QWORD PTR [rip+0xce7]        # CONST_e307
//    0x0000555555555351 <+337>:   c5 f9 2e f0                     vucomisd xmm6,xmm0
//    0x0000555555555355 <+341>:   73 29                           jae    0x555555555380 <fmod+384>
//    0x0000555555555357 <+343>:   c5 f9 28 fe                     vmovapd xmm7,xmm6
//    0x000055555555535b <+347>:   eb 53                           jmp    0x5555555553b0 <fmod+432>
  __ bind(L_5320);
  __ vmulsd(xmm1, xmm4, ExternalAddress((address)CONST_e307), rax);
  __ movq(xmm2, xmm1);
  __ evdivsd(xmm0, xmm6, xmm2, Assembler::EVEX_RZ);
  __ movq(xmm0, xmm0);
  __ vroundsd(xmm7, xmm7, xmm0, 0xb);
  __ extractps(rax, xmm7, 1);
  __ cmpl(rax, 0x7fefffff);
  __ jcc(Assembler::below, L_5360);
  __ vmulsd(xmm0, xmm1, ExternalAddress((address)CONST_e307), rax);
  __ ucomisd(xmm6, xmm0);
  __ jcc(Assembler::aboveEqual, L_5380);
  __ movapd(xmm7, xmm6);
  __ jmp(L_53b0);
//    0x000055555555535d <+349>:   0f 1f 00                        nop    DWORD PTR [rax]
//    0x0000555555555360 <+352>:   62 f2 f5 78 ad fe               vfnmadd213sd xmm7,xmm1,xmm6,{rz-sae}
//    0x0000555555555366 <+358>:   eb 48                           jmp    0x5555555553b0 <fmod+432>
  __ bind(L_5360);
  __ evfnmadd213sd(xmm7, xmm1, xmm6, Assembler::EVEX_RZ);
  __ jmp(L_53b0);
//    0x0000555555555368 <+360>:   66 66 66 66 66 66 2e 0f 1f 84 00 00 00 00 00    data16 data16 data16 data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x0000555555555377 <+375>:   66 0f 1f 84 00 00 00 00 00      nop    WORD PTR [rax+rax*1+0x0]
//    0x0000555555555380 <+384>:   c4 41 39 57 c0                  vxorpd xmm8,xmm8,xmm8
//    0x0000555555555385 <+389>:   66 66 2e 0f 1f 84 00 00 00 00 00        data16 cs nop WORD PTR [rax+rax*1+0x0]
  __ bind(L_5380);
  __ vxorpd(xmm8, xmm8, xmm8, Assembler::AVX_128bit);

  __ align32();
  __ bind(L_5390);
//    0x0000555555555390 <+400>:   62 f1 cf 78 5e f8               vdivsd xmm7,xmm6,xmm0,{rz-sae}
//    0x0000555555555396 <+406>:   c5 fa 7e ff                     vmovq  xmm7,xmm7
//    0x000055555555539a <+410>:   c4 e3 39 0b ff 0b               vroundsd xmm7,xmm8,xmm7,0xb
//    0x00005555555553a0 <+416>:   62 f2 fd 78 ad fe               vfnmadd213sd xmm7,xmm0,xmm6,{rz-sae}
//    0x00005555555553a6 <+422>:   c5 f9 2e f8                     vucomisd xmm7,xmm0
//    0x00005555555553aa <+426>:   c5 f9 28 f7                     vmovapd xmm6,xmm7
//    0x00005555555553ae <+430>:   73 e0                           jae    0x555555555390 <fmod+400>
//    0x00005555555553b0 <+432>:   c5 f9 2e f9                     vucomisd xmm7,xmm1
//    0x00005555555553b4 <+436>:   73 0a                           jae    0x5555555553c0 <fmod+448>
//    0x00005555555553b6 <+438>:   c5 f9 28 c7                     vmovapd xmm0,xmm7
//    0x00005555555553ba <+442>:   e9 e7 fe ff ff                  jmp    0x5555555552a6 <fmod+166>
  __ evdivsd(xmm7, xmm6, xmm0, Assembler::EVEX_RZ);
  __ movq(xmm7, xmm7);
  __ vroundsd(xmm7, xmm8, xmm7, 0xb);
  __ evfnmadd213sd(xmm7, xmm0, xmm6, Assembler::EVEX_RZ);
  __ ucomisd(xmm7, xmm0);
  __ movapd(xmm6, xmm7);
  __ jcc(Assembler::aboveEqual, L_5390);
  __ bind(L_53b0);
  __ ucomisd(xmm7, xmm1);
  __ jcc(Assembler::aboveEqual, L_53c0);
  __ movapd(xmm0, xmm7);
  __ jmp(L_52a6);
//    0x00005555555553bf <+447>:   90                              nop
//    0x00005555555553c0 <+448>:   c5 c9 57 f6                     vxorpd xmm6,xmm6,xmm6
//    0x00005555555553c4 <+452>:   66 66 66 2e 0f 1f 84 00 00 00 00 00     data16 data16 cs nop WORD PTR [rax+rax*1+0x0]
//    0x00005555555553d0 <+464>:   62 f1 c7 78 5e c2               vdivsd xmm0,xmm7,xmm2,{rz-sae}
//    0x00005555555553d6 <+470>:   c5 fa 7e c0                     vmovq  xmm0,xmm0
//    0x00005555555553da <+474>:   c4 e3 49 0b c0 0b               vroundsd xmm0,xmm6,xmm0,0xb
//    0x00005555555553e0 <+480>:   62 f2 f5 78 ad c7               vfnmadd213sd xmm0,xmm1,xmm7,{rz-sae}
//    0x00005555555553e6 <+486>:   c5 f9 2e c1                     vucomisd xmm0,xmm1
//    0x00005555555553ea <+490>:   c5 f9 28 f8                     vmovapd xmm7,xmm0
//    0x00005555555553ee <+494>:   73 e0                           jae    0x5555555553d0 <fmod+464>
//    0x00005555555553f0 <+496>:   e9 b1 fe ff ff                  jmp    0x5555555552a6 <fmod+166>
  __ bind(L_53c0);
  __ vxorpd(xmm6, xmm6, xmm6, Assembler::AVX_128bit);
  __ align32();
  __ bind(L_53d0);
  __ evdivsd(xmm0, xmm7, xmm2, Assembler::EVEX_RZ);
  __ movq(xmm0, xmm0);
  __ vroundsd(xmm0, xmm6, xmm0, 0xb);
  __ evfnmadd213sd(xmm0, xmm1, xmm7, Assembler::EVEX_RZ);
  __ ucomisd(xmm0, xmm1);
  __ movapd(xmm7, xmm0);
  __ jcc(Assembler::aboveEqual, L_53d0);
  __ jmp(L_52a6);





// (gdb) x/30gx 0x555555556020
// 0x555555556020: 0x7fffffffffffffff      0x7fefffffffffffff
// 0x555555556030: 0x7ff0000000000000      0x7fe0000000000000
// 0x555555556040: 0x000000343b031b01      0xffffefe000000005
// 0x555555556050: 0xffffeff000000068      0xfffff00000000090
// 0x555555556060: 0xfffff0e900000050      0xfffff1c0000000a8
// 0x555555556070: 0x00000000000000c8      0x0000000000000014

  __ bind(L_exit);

#else

  Label L_12ca, L_1280, L_115a, L_1271, L_1268, L_1227, L_1231, L_11f4, L_128a, L_1237, L_12bf, L_1290, L_exit;

// #include <ia32intrin.h>
// #include <emmintrin.h>
// #pragma float_control(precise, on)

// #define UINT32 unsigned int
// #define SINT32 int
// #define UINT64 unsigned __int64
// #define SINT64 __int64

// #define DP_FMA(a, b, c)    __fence(_mm_cvtsd_f64(_mm_fmadd_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c))))
// #define DP_FMA_RN(a, b, c)    _mm_cvtsd_f64(_mm_fmadd_round_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c), (_MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)))
// #define DP_FMA_RZ(a, b, c) __fence(_mm_cvtsd_f64(_mm_fmadd_round_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c), (_MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC))))

// #define DP_ROUND_RZ(a)   _mm_cvtsd_f64(_mm_roundscale_sd(_mm_setzero_pd(), _mm_set_sd(a), (_MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC)))

// #define DP_CONST(C)    _castu64_f64(0x##C##ull)
// #define DP_AND(X, Y)   _mm_cvtsd_f64(_mm_and_pd(_mm_set_sd(X), _mm_set_sd(Y)))
// #define DP_XOR(X, Y)   _mm_cvtsd_f64(_mm_xor_pd(_mm_set_sd(X), _mm_set_sd(Y)))
// #define DP_OR(X, Y)    _mm_cvtsd_f64(_mm_or_pd(_mm_set_sd(X), _mm_set_sd(Y)))
// #define DP_DIV_RZ(a, b) __fence(_mm_cvtsd_f64(_mm_div_round_sd(_mm_set_sd(a), _mm_set_sd(b), (_MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC))))
// #define DP_FNMA(a, b, c)    __fence(_mm_cvtsd_f64(_mm_fnmadd_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c))))
// #define DP_FNMA_RZ(a, b, c) __fence(_mm_cvtsd_f64(_mm_fnmadd_round_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c), (_MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC))))

// #define D2L(x)  _mm_castpd_si128(x)
// // transfer highest 32 bits (of low 64b) to GPR
// #define TRANSFER_HIGH_INT32(X)   _mm_extract_epi32(D2L(_mm_set_sd(X)), 1)


// double fmod(double x, double y)
// {
  __ subptr(rsp, 0x28);
  __ mov64(rax, 0x7fffffffffffffff);
  __ movapd(xmm20, xmm0);
  __ movapd(xmm3, xmm1);
// double a, b, sgn_a, q, bs, bs2;
// unsigned eq;

//     // |x|, |y|
//     a = DP_AND(x, DP_CONST(7fffffffffffffff));
  __ vxorpd(xmm18, xmm18, xmm18, Assembler::AVX_128bit);
  __ vmovsd(xmm5, xmm18, xmm20);
  __ movq(xmm17, rax);
  __ vandpd(xmm0, xmm5, xmm17, Assembler::AVX_128bit);
//     b = DP_AND(y, DP_CONST(7fffffffffffffff));
  __ vandpd(xmm1, xmm3, xmm17, Assembler::AVX_128bit);
//   // sign(x)
//   sgn_a = DP_XOR(x, a);
  __ vmovsd(xmm17, xmm18, xmm0);
  __ vxorpd(xmm16, xmm5, xmm17, Assembler::AVX_128bit);

//   q = DP_DIV_RZ(a, b);
  __ vmovsd(xmm5, xmm18, xmm1);
  __ evdivsd(xmm2, xmm17, xmm5, Assembler::EVEX_RZ);
//   q = DP_ROUND_RZ(q);
  __ vmovsd(xmm4, xmm18, xmm2);
  __ vrndscalesd(xmm19, xmm18, xmm4, 0x0B);

//   eq = TRANSFER_HIGH_INT32(q);
  __ vmovsd(xmm4, xmm18, xmm19);
  __ pextrd(rax, xmm4, 1);

//   if (!eq)  return x + sgn_a;
__ testl(rax, rax);
__ jcc(Assembler::equal, L_12ca);

//   if (eq >= 0x7fefffffu) goto SPECIAL_FMOD;
__ cmpl(rax, 0x7FEFFFFF);
__ jcc(Assembler::below, L_1280);

// SPECIAL_FMOD:

//   // y==0 or x==Inf?
//   if ((b == 0.0) || (!(a <= DP_CONST(7fefffffffffffff))))
  __ vxorpd(xmm2, xmm2, xmm2, Assembler::AVX_128bit);
  __ ucomisd(xmm1, xmm2);
  __ jcc(Assembler::parity, L_115a);
  __ jcc(Assembler::equal, L_1271);

  __ bind(L_115a);
  __ movsd(xmm2, ExternalAddress((address)CONST_MAX), rax);
  __ comisd(xmm2, xmm0);
  __ jcc(Assembler::below, L_1271);
//   // y is NaN?
//   if (!(b <= DP_CONST(7ff0000000000000))) return y + y;
  __ movsd(xmm2, ExternalAddress((address)CONST_INF), rax);
  __ comisd(xmm2, xmm1);
  __ jcc(Assembler::below, L_1268);

//   // b* 2*1023
//   bs = b * DP_CONST(7fe0000000000000);
  __ movsd(xmm21, ExternalAddress((address)CONST_e307), rax);
  __ vmulsd(xmm2, xmm1, xmm21);

//   q = DP_DIV_RZ(a, bs);
  __ vmovsd(xmm4, xmm18, xmm2);
  __ evdivsd(xmm3, xmm17, xmm4, Assembler::EVEX_RZ);
//   q = DP_ROUND_RZ(q);
  __ vmovsd(xmm19, xmm18, xmm3);
  __ vrndscalesd(xmm20, xmm18, xmm19, 0x0B);

//   eq = TRANSFER_HIGH_INT32(q);
  __ vmovsd(xmm3, xmm18, xmm20);
  __ pextrd(rdx, xmm3, 1);

//   if (eq >= 0x7fefffffu)
  __ cmpl(rdx, 0x7FEFFFFF);
  __ jcc(Assembler::below, L_1227);
//   {
//     // b* 2*1023 * 2^1023
//     bs2 = bs * DP_CONST(7fe0000000000000);
  __ vmulsd(xmm3, xmm2, xmm21);
//     while (bs2 <= a)
  __ comisd(xmm0, xmm3);
  __ jcc(Assembler::below, L_1231);
  __ vmovsd(xmm17, xmm18, xmm3);
//     {
//       q = DP_DIV_RZ(a, bs2);
  __ align32();
  __ bind(L_11f4);
  __ vmovsd(xmm22, xmm18, xmm0);
  __ evdivsd(xmm0, xmm22, xmm17, Assembler::EVEX_RZ);
//       q = DP_ROUND_RZ(q);
  __ vmovsd(xmm19, xmm18, xmm0);
  __ vrndscalesd(xmm20, xmm18, xmm19, 0x0B);
//       a = DP_FNMA_RZ(bs2, q, a);
  __ movapd(xmm0, xmm17);
  __ vmovsd(xmm21, xmm18, xmm20);
  __ evfnmadd213sd(xmm0, xmm21, xmm22, Assembler::EVEX_RZ);
//     while (bs2 <= a)
  __ comisd(xmm0, xmm3);
  __ jcc(Assembler::aboveEqual, L_11f4);
  __ jmp(L_1231);
//     }
//   }
//   else
//   a = DP_FNMA_RZ(bs, q, a);
  __ bind(L_1227);
  __ movapd(xmm0, xmm4);
  __ evfnmadd213sd(xmm0, xmm3, xmm17, Assembler::EVEX_RZ);

//   while (bs <= a)
  __ bind(L_1231);
  __ comisd(xmm0, xmm2);
  __ jcc(Assembler::below, L_128a);
//   {
//     q = DP_DIV_RZ(a, bs);
  __ align32();
  __ bind(L_1237);
  __ vmovsd(xmm20, xmm18, xmm0);
  __ evdivsd(xmm0, xmm20, xmm4, Assembler::EVEX_RZ);
//     q = DP_ROUND_RZ(q);
  __ vmovsd(xmm3, xmm18, xmm0);
  __ vrndscalesd(xmm17, xmm18, xmm3, 0x0B);
//     a = DP_FNMA_RZ(bs, q, a);
  __ movapd(xmm0, xmm4);
  __ vmovsd(xmm19, xmm18, xmm17);
  __ evfnmadd213sd(xmm0, xmm19, xmm20, Assembler::EVEX_RZ);

//   while (bs <= a)
  __ comisd(xmm0, xmm2);
  __ jcc(Assembler::aboveEqual, L_1237);
  __ jmp(L_128a);
//   // y is NaN?
//   if (!(b <= DP_CONST(7ff0000000000000))) return y + y;
__ bind(L_1268);
__ vaddsd(xmm0, xmm3, xmm3);
__ jmp(L_exit);
//     return DP_FNMA(b, q, a);    // NaN

  __ bind(L_1271);
  __ vfnmadd213sd(xmm5, xmm4, xmm17);
  __ movapd(xmm0, xmm5);
  __ jmp(L_exit);

//   a = DP_FNMA_RZ(b, q, a);
  __ bind(L_1280);
  __ movapd(xmm0, xmm5);
  __ evfnmadd213sd(xmm0, xmm4, xmm17, Assembler::EVEX_RZ);

// FMOD_CONT:
//   while (b <= a)
  __ bind(L_128a);
  __ comisd(xmm0, xmm1);
  __ jcc(Assembler::below, L_12bf);
//   {
//     q = DP_DIV_RZ(a, b);
  __ align32();
  __ bind(L_1290);
  __ vmovsd(xmm17, xmm18, xmm0);
  __ evdivsd(xmm0, xmm17, xmm5, Assembler::EVEX_RZ);
//     q = DP_ROUND_RZ(q);
  __ vmovsd(xmm2, xmm18, xmm0);
  __ vrndscalesd(xmm3, xmm18, xmm2, 0x0B);
//     a = DP_FNMA_RZ(b, q, a);
  __ movapd(xmm0, xmm5);
  __ vmovsd(xmm4, xmm18, xmm3);
  __ evfnmadd213sd(xmm0, xmm4, xmm17, Assembler::EVEX_RZ);

// FMOD_CONT:
//   while (b <= a)
//   }
  __ comisd(xmm0, xmm1);
  __ jcc(Assembler::aboveEqual, L_1290);

//   a = DP_XOR(a, sgn_a);
  __ bind(L_12bf);
  __ vxorpd(xmm0, xmm0, xmm16, Assembler::AVX_128bit);

//   return a;
__ jmp(L_exit);

//   if (!eq)  return x + sgn_a;
  __ bind(L_12ca);
  __ vaddsd(xmm0, xmm20, xmm16);
  __ bind(L_exit);
  __ addptr(rsp, 0x28);

#endif

  } else if (VM_Version::supports_fma()) {       // AVX2 version

    Label L_104a, L_11bd, L_10c1, L_1090, L_11b9, L_10e7, L_11af, L_111c, L_10f3, L_116e, L_112a;
    Label L_1173, L_1157, L_117f, L_11a0;

//   double fmod(double x, double y)
// {
// double a, b, sgn_a, q, bs, bs2, corr, res;
// unsigned eq;
// unsigned mxcsr, mxcsr_rz;

//   __asm { stmxcsr DWORD PTR[mxcsr] }
//   mxcsr_rz = 0x7f80 | mxcsr;
  __ push(rax);
  __ stmxcsr(Address(rsp, 0));
  __ movl(rax, Address(rsp, 0));
  __ movl(rcx, rax);
  __ orl(rcx, 0x7f80);
  __ movl(Address(rsp, 0x04), rcx);

//     // |x|, |y|
//     a = DP_AND(x, DP_CONST(7fffffffffffffff));
  __ movq(xmm2, xmm0);
  __ vmovdqu(xmm3, ExternalAddress((address)CONST_NaN), rcx);
  __ vpand(xmm4, xmm2, xmm3, Assembler::AVX_128bit);
//     b = DP_AND(y, DP_CONST(7fffffffffffffff));
  __ vpand(xmm3, xmm1, xmm3, Assembler::AVX_128bit);
//   // sign(x)
//   sgn_a = DP_XOR(x, a);
  __ mov64(rcx, 0x8000000000000000);
  __ movq(xmm5, rcx);
  __ vpand(xmm2, xmm2, xmm5, Assembler::AVX_128bit);

//   if (a < b)  return x + sgn_a;
  __ ucomisd(xmm3, xmm4);
  __ jcc(Assembler::belowEqual, L_104a);
  __ vaddsd(xmm0, xmm2, xmm0);
  __ jmp(L_11bd);

//   if (((mxcsr & 0x6000)!=0x2000) && (a < b * 0x1p+260))
  __ bind(L_104a);
  __ andl(rax, 0x6000);
  __ cmpl(rax, 0x2000);
  __ jcc(Assembler::equal, L_10c1);
  __ vmulsd(xmm0, xmm3, ExternalAddress((address)CONST_1p260), rax);
  __ ucomisd(xmm0, xmm4);
  __ jcc(Assembler::belowEqual, L_10c1);
//   {
//     q = DP_DIV(a, b);
  __ vdivpd(xmm0, xmm4, xmm3, Assembler::AVX_128bit);
//     corr = DP_SHR(DP_FNMA(b, q, a), 63);
  __ movapd(xmm1, xmm0);
  __ vfnmadd213sd(xmm1, xmm3, xmm4);
  __ movq(xmm5, xmm1);
  __ vpxor(xmm1, xmm1, xmm1, Assembler::AVX_128bit);
  __ vpcmpgtq(xmm5, xmm1, xmm5, Assembler::AVX_128bit);
//     q = DP_PSUBQ(q, corr);
  __ vpaddq(xmm0, xmm5, xmm0, Assembler::AVX_128bit);
//     q = DP_TRUNC(q);
  __ vroundsd(xmm0, xmm0, xmm0, 3);
//     a = DP_FNMA(b, q, a);
  __ vfnmadd213sd(xmm0, xmm3, xmm4);
  __ align32();
//     while (b <= a)
  __ bind(L_1090);
  __ ucomisd(xmm0, xmm3);
  __ jcc(Assembler::below, L_11b9);
//     {
//       q = DP_DIV(a, b);
  __ vdivsd(xmm4, xmm0, xmm3);
//       corr = DP_SHR(DP_FNMA(b, q, a), 63);
  __ movapd(xmm5, xmm4);
  __ vfnmadd213sd(xmm5, xmm3, xmm0);
  __ movq(xmm5, xmm5);
  __ vpcmpgtq(xmm5, xmm1, xmm5, Assembler::AVX_128bit);
//       q = DP_PSUBQ(q, corr);
  __ vpaddq(xmm4, xmm5, xmm4, Assembler::AVX_128bit);
//       q = DP_TRUNC(q);
  __ vroundsd(xmm4, xmm4, xmm4, 3);
//       a = DP_FNMA(b, q, a);
  __ vfnmadd231sd(xmm0, xmm3, xmm4);
  __ jmp(L_1090);
//     }
//     return DP_XOR(a, sgn_a);
//   }

//   __asm { ldmxcsr DWORD PTR [mxcsr_rz] }
  __ bind(L_10c1);
  __ ldmxcsr(Address(rsp, 0x04));

//   q = DP_DIV(a, b);
  __ vdivpd(xmm0, xmm4, xmm3, Assembler::AVX_128bit);
//   q = DP_TRUNC(q);
  __ vroundsd(xmm0, xmm0, xmm0, 3);

//   eq = TRANSFER_HIGH_INT32(q);
  __ extractps(rax, xmm0, 1);

//   if (__builtin_expect((eq >= 0x7fefffffu), (0==1))) goto SPECIAL_FMOD;
  __ cmpl(rax, 0x7feffffe);
  __ jcc(Assembler::above, L_10e7);

//   a = DP_FNMA(b, q, a);
  __ vfnmadd213sd(xmm0, xmm3, xmm4);
  __ jmp(L_11af);

// SPECIAL_FMOD:

//   // y==0 or x==Inf?
//   if ((b == 0.0) || (!(a <= DP_CONST(7fefffffffffffff))))
  __ bind(L_10e7);
  __ vpxor(xmm5, xmm5, xmm5, Assembler::AVX_128bit);
  __ ucomisd(xmm3, xmm5);
  __ jcc(Assembler::notEqual, L_10f3);
  __ jcc(Assembler::noParity, L_111c);

  __ bind(L_10f3);
  __ movsd(xmm5, ExternalAddress((address)CONST_MAX), rax);
  __ ucomisd(xmm5, xmm4);
  __ jcc(Assembler::below, L_111c);
//     return res;
//   }
//   // y is NaN?
//   if (!(b <= DP_CONST(7ff0000000000000))) {
  __ movsd(xmm0, ExternalAddress((address)CONST_INF), rax);
  __ ucomisd(xmm0, xmm3);
  __ jcc(Assembler::aboveEqual, L_112a);
//     res = y + y;
  __ vaddsd(xmm0, xmm0, xmm1);
//     __asm { ldmxcsr DWORD PTR[mxcsr] }
  __ ldmxcsr(Address(rsp, 0));
  __ jmp(L_11bd);
//   {
//     res = DP_FNMA(b, q, a);    // NaN
  __ bind(L_111c);
  __ vfnmadd213sd(xmm0, xmm3, xmm4);
//     __asm { ldmxcsr DWORD PTR[mxcsr] }
  __ ldmxcsr(Address(rsp, 0));
  __ jmp(L_11bd);
//     return res;
//   }

//   // b* 2*1023
//   bs = b * DP_CONST(7fe0000000000000);
  __ bind(L_112a);
  __ vmulsd(xmm1, xmm3, ExternalAddress((address)CONST_e307), rax);

//   q = DP_DIV(a, bs);
  __ vdivsd(xmm0, xmm4, xmm1);
//   q = DP_TRUNC(q);
  __ vroundsd(xmm0, xmm0, xmm0, 3);

//   eq = TRANSFER_HIGH_INT32(q);
  __ extractps(rax, xmm0, 1);

//   if (eq >= 0x7fefffffu)
  __ cmpl(rax, 0x7fefffff);
  __ jcc(Assembler::below, L_116e);
//   {
//     // b* 2*1023 * 2^1023
//     bs2 = bs * DP_CONST(7fe0000000000000);
  __ vmulsd(xmm0, xmm1, ExternalAddress((address)CONST_e307), rax);
//     while (bs2 <= a)
  __ ucomisd(xmm4, xmm0);
  __ jcc(Assembler::below, L_1173);
//     {
//       q = DP_DIV(a, bs2);
  __ bind(L_1157);
  __ vdivsd(xmm5, xmm4, xmm0);
//       q = DP_TRUNC(q);
  __ vroundsd(xmm5, xmm5, xmm5, 3);
//       a = DP_FNMA(bs2, q, a);
  __ vfnmadd231sd(xmm4, xmm0, xmm5);
//     while (bs2 <= a)
  __ ucomisd(xmm4, xmm0);
  __ jcc(Assembler::aboveEqual, L_1157);
  __ jmp(L_1173);
//     }
//   }
//   else
//   a = DP_FNMA(bs, q, a);
  __ bind(L_116e);
  __ vfnmadd231sd(xmm4, xmm1, xmm0);

//   while (bs <= a)
  __ bind(L_1173);
  __ ucomisd(xmm4, xmm1);
  __ jcc(Assembler::aboveEqual, L_117f);
  __ movapd(xmm0, xmm4);
  __ jmp(L_11af);
//   {
//     q = DP_DIV(a, bs);
  __ bind(L_117f);
  __ vdivsd(xmm0, xmm4, xmm1);
//     q = DP_TRUNC(q);
  __ vroundsd(xmm0, xmm0, xmm0, 3);
//     a = DP_FNMA(bs, q, a);
  __ vfnmadd213sd(xmm0, xmm1, xmm4);

//   while (bs <= a)
  __ ucomisd(xmm0, xmm1);
  __ movapd(xmm4, xmm0);
  __ jcc(Assembler::aboveEqual, L_117f);
  __ jmp(L_11af);
  __ align32();
//   {
//     q = DP_DIV(a, b);
  __ bind(L_11a0);
  __ vdivsd(xmm1, xmm0, xmm3);
//     q = DP_TRUNC(q);
  __ vroundsd(xmm1, xmm1, xmm1, 3);
//     a = DP_FNMA(b, q, a);
  __ vfnmadd231sd(xmm0, xmm3, xmm1);

// FMOD_CONT:
//   while (b <= a)
  __ bind(L_11af);
  __ ucomisd(xmm0, xmm3);
  __ jcc(Assembler::aboveEqual, L_11a0);
//   }

//   __asm { ldmxcsr DWORD PTR[mxcsr] }
  __ ldmxcsr(Address(rsp, 0));
  __ bind(L_11b9);
  __ vpxor(xmm0, xmm2, xmm0, Assembler::AVX_128bit);
//   }

//   goto FMOD_CONT;

// }
  __ bind(L_11bd);
  __ pop(rax);

  } else {                                       // SSE version
    assert(false, "SSE not implemented");
  }

  __ leave(); // required for proper stackwalking of RuntimeStub frame
  __ ret(0);

  return start;
}

#undef __
