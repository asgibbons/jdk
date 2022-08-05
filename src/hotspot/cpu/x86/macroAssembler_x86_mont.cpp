/*
* Copyright (c) 2022, Intel Corporation. All rights reserved.
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
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "runtime/stubRoutines.hpp"
#include "macroAssembler_x86.hpp"

#if _LP64
void MacroAssembler::montgomeryMultiply52x20(Register out, Register a, Register b, Register m, Register inv)
{
  // Windows regs    |  Linux regs
  // c_rarg0 (rcx)   |  c_rarg0 (rdi)  out
  // c_rarg1 (rdx)   |  c_rarg1 (rsi)  a
  // c_rarg2 (r8)    |  c_rarg2 (rdx)  b
  // c_rarg3 (r9)    |  c_rarg3 (rcx)  m
  // r10             |  c_rarg4 (r8)   inv

  const Register rtmp0 = rax;
#ifdef _WIN64
  const Register rtmp1 = rdi;
  const Register rtmp2 = rsi;
#else
  const Register rtmp1 = r9;
  const Register rtmp2 = r10;
#endif
  const Register rtmp3 = r11;
  const Register rtmp4 = r12;
  const Register rtmp5 = r13;
  const Register rtmp6 = r14;
  const Register rtmp7 = r15;
  const Register rtmp8 = rbx;

   // On entry:
   //  out points to where the result should be stored (16 qwords)
   //  out + 40 qwords is a
   //  out + 80 qwords is b
   //  out + 120 qwords is m
   //  kk0 is the inverse (-(1/p) mod b)
   //
   // This routine uses all GP registers, and ymm0 - ymm12
   // Need to save r8, r14 and r13


  //  4:  55                     push   rbp
  //  5:  c5 d1 ef ed            vpxor  xmm5,xmm5,xmm5   // R1h
  //  9:  31 c0                  xor    eax,eax
  //  b:  48 89 e5               mov    rbp,rsp
  //  e:  41 57                  push   r15
  // 10:  62 f1 fd 28 6f fd      vmovdqa64 ymm7,ymm5     // R0h
  // 16:  62 f1 fd 28 6f e5      vmovdqa64 ymm4,ymm5
  // 1c:  41 56                  push   r14
  // 1e:  62 f1 fd 28 6f f5      vmovdqa64 ymm6,ymm5     // R1
  // 24:  62 f1 fd 28 6f c5      vmovdqa64 ymm0,ymm5     // R0
  // 2a:  41 55                  push   r13
  // 2c:  49 b9 ff ff ff ff ff   movabs r9,0xfffffffffffff
  // 33:  ff 0f 00 
  // 36:  62 71 fd 28 6f c5      vmovdqa64 ymm8,ymm5
  // 3c:  41 54                  push   r12
  // 3e:  53                     push   rbx
  // 3f:  48 8d 9a a0 00 00 00   lea    rbx,[rdx+0xa0]
  // 46:  48 83 e4 e0            and    rsp,0xffffffffffffffe0
  // 4a:  48 89 54 24 f8         mov    QWORD PTR [rsp-0x8],rdx
  // 4f:  4c 8b 29               mov    r13,QWORD PTR [rcx]
  // 52:  4c 8b 26               mov    r12,QWORD PTR [rsi]
  push(rbx);
  push(r12);
  push(r13);
  push(r14);
  push(r15);
#ifdef _WIN64
  push(rsi);
  push(rdi);
#endif
  vpxor(xmm5, xmm5, xmm5, Assembler::AVX_256bit);
  vmovdqu(xmm7, xmm5);
  vmovdqu(xmm4, xmm5);
  vmovdqu(xmm6, xmm5);
  vmovdqu(xmm0, xmm5);
  vmovdqu(xmm8, xmm5);
  mov64(rtmp1, 0xfffffffffffff);
  lea(rtmp8, Address(out, 0xa0 + 2 * 40 * wordSize));    // Points to b[20] - loop terminator
  movq(rtmp5, Address(c_rarg3, 0));
  movq(rtmp4, Address(c_rarg1, 0));
  xorq(rtmp0, rtmp0);

  align32();
  Label L_loop, L_mask;
  bind(L_loop);

  // 55:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
  // 5c:  00 00 00 00 
  // 60:  48 8b 54 24 f8         mov    rdx,QWORD PTR [rsp-0x8]
  // 65:  62 f1 fd 28 6f cc      vmovdqa64 ymm1,ymm4     // R2
  // 6b:  4c 8b 12               mov    r10,QWORD PTR [rdx]
  // 6e:  4c 89 d2               mov    rdx,r10
  // 71:  62 d2 fd 28 7c da      vpbroadcastq ymm3,r10   // Bi
  // 77:  c4 42 ab f6 dc         mulx   r11,r10,r12    // r11:r10 = rdx(b[i]) * r12(a[0])
  // 7c:  4c 01 d0               add    rax,r10
  // 7f:  4d 89 de               mov    r14,r11
  // 82:  49 89 c2               mov    r10,rax
  // 85:  49 83 d6 00            adc    r14,0x0
  // 89:  4d 0f af d0            imul   r10,r8       // r8 is k0
  // 8d:  45 31 ff               xor    r15d,r15d
  // 90:  4d 21 ca               and    r10,r9
  // 93:  4c 89 d2               mov    rdx,r10
  // 96:  62 d2 fd 28 7c d2      vpbroadcastq ymm2,r10   // Yi
  // 9c:  c4 42 ab f6 dd         mulx   r11,r10,r13
  // a1:  4c 01 d0               add    rax,r10
  // a4:  41 0f 92 c7            setb   r15b
  vmovdqu(xmm1, xmm4);
  movq(rtmp2, Address(c_rarg2, 0));
  // mulxq needs one input in rdx
  // save restore rdx using push/pop
  // rdx holds c_rarg1 on windows and c_rarg2 on linux
  // make sure that neither of c_rarg1 or c_rarg2 is used in between push/pop pair
  push(rdx);
  movq(rdx, rtmp2);
  evpbroadcastq(xmm3, rtmp2, Assembler::AVX_256bit);
  mulxq(rtmp3, rtmp2, rtmp4);
  addq(rtmp0, rtmp2);
  movq(rtmp6, rtmp3);
  movq(rtmp2, rtmp0);
  adcq(rtmp6, 0);
  imulq(rtmp2, c_rarg4);
  xorl(rtmp7, rtmp7);
  andq(rtmp2, rtmp1);
  movq(rdx, rtmp2);
  evpbroadcastq(xmm2, rtmp2, Assembler::AVX_256bit);
  mulxq(rtmp3, rtmp2, rtmp5);
  addq(rtmp0, rtmp2);
  setb(Assembler::below, rtmp7);
  pop(rdx);


  // a8:  62 f2 e5 28 b4 06      evpmadd52luq ymm0,ymm3,YMMWORD PTR [rsi]
  // ae:  62 f2 e5 28 b4 7e 01   evpmadd52luq ymm7,ymm3,YMMWORD PTR [rsi+0x20]
  // b5:  62 f2 ed 28 b4 01      evpmadd52luq ymm0,ymm2,YMMWORD PTR [rcx]
  // bb:  62 f2 ed 28 b4 79 01   evpmadd52luq ymm7,ymm2,YMMWORD PTR [rcx+0x20]
  // c2:  62 f3 c5 28 03 c0 01   valignq ymm0,ymm7,ymm0,0x1
  // c9:  48 83 44 24 f8 08      add    QWORD PTR [rsp-0x8],0x8
  // cf:  62 f2 e5 28 b4 4e 04   evpmadd52luq ymm1,ymm3,YMMWORD PTR [rsi+0x80]
  // d6:  62 f2 ed 28 b4 49 04   evpmadd52luq ymm1,ymm2,YMMWORD PTR [rcx+0x80]
  // dd:  62 f3 bd 28 03 e1 01   valignq ymm4,ymm8,ymm1,0x1
  evpmadd52luq(xmm0, xmm3, Address(c_rarg1, 0), Assembler::AVX_256bit);
  evpmadd52luq(xmm7, xmm3, Address(c_rarg1, 0x20), Assembler::AVX_256bit);
  evpmadd52luq(xmm0, xmm2, Address(c_rarg3, 0), Assembler::AVX_256bit);
  evpmadd52luq(xmm7, xmm2, Address(c_rarg3, 0x20), Assembler::AVX_256bit);
  evalignq(xmm0, xmm7, xmm0, 1, Assembler::AVX_256bit);
  evpmadd52luq(xmm1, xmm3, Address(c_rarg1, 0x80), Assembler::AVX_256bit);
  evpmadd52luq(xmm1, xmm2, Address(c_rarg3, 0x80), Assembler::AVX_256bit);
  evalignq(xmm4, xmm8, xmm1, 1, Assembler::AVX_256bit);

  // e4:  4d 89 da               mov    r10,r11
  // e7:  4d 01 f2               add    r10,r14
  // ea:  4d 01 fa               add    r10,r15
  // ed:  48 c1 e8 34            shr    rax,0x34
  // f1:  49 c1 e2 0c            shl    r10,0xc
  // f5:  48 8b 54 24 f8         mov    rdx,QWORD PTR [rsp-0x8]
  // fa:  49 09 c2               or     r10,rax
  // fd:  c4 e1 f9 7e c0         vmovq  rax,xmm0
  movq(rtmp2, rtmp3);
  addq(rtmp2, rtmp6);
  addq(rtmp2, rtmp7);
  shrq(rtmp0, 0x34);
  shlq(rtmp2, 0xc);
  addq(c_rarg2, 8);
  orq(rtmp2, rtmp0);
  movq(rtmp0, xmm0);

//  102:  62 f2 e5 28 b4 76 02   evpmadd52luq ymm6,ymm3,YMMWORD PTR [rsi+0x40]
//  109:  62 f2 e5 28 b4 6e 03   evpmadd52luq ymm5,ymm3,YMMWORD PTR [rsi+0x60]
//  110:  62 f2 ed 28 b4 71 02   evpmadd52luq ymm6,ymm2,YMMWORD PTR [rcx+0x40]
//  117:  62 f2 ed 28 b4 69 03   evpmadd52luq ymm5,ymm2,YMMWORD PTR [rcx+0x60]
//  11e:  62 f3 cd 28 03 ff 01   valignq ymm7,ymm6,ymm7,0x1
  evpmadd52luq(xmm6, xmm3, Address(c_rarg1, 0x40), Assembler::AVX_256bit);
  evpmadd52luq(xmm5, xmm3, Address(c_rarg1, 0x60), Assembler::AVX_256bit);
  evpmadd52luq(xmm6, xmm2, Address(c_rarg3, 0x40), Assembler::AVX_256bit);
  evpmadd52luq(xmm5, xmm2, Address(c_rarg3, 0x60), Assembler::AVX_256bit);
  evalignq(xmm7, xmm6, xmm7, 1, Assembler::AVX_256bit);

//  125:  62 f2 e5 28 b5 06      evpmadd52huq ymm0,ymm3,YMMWORD PTR [rsi]
//  12b:  62 f3 d5 28 03 f6 01   valignq ymm6,ymm5,ymm6,0x1
//  132:  62 f2 e5 28 b5 7e 01   evpmadd52huq ymm7,ymm3,YMMWORD PTR [rsi+0x20]
//  139:  62 f3 f5 28 03 ed 01   valignq ymm5,ymm1,ymm5,0x1
//  140:  62 f2 e5 28 b5 76 02   evpmadd52huq ymm6,ymm3,YMMWORD PTR [rsi+0x40]
//  147:  62 f1 fd 28 6f cc      vmovdqa64 ymm1,ymm4
//  14d:  62 f2 e5 28 b5 6e 03   evpmadd52huq ymm5,ymm3,YMMWORD PTR [rsi+0x60]
//  154:  62 f2 e5 28 b5 4e 04   evpmadd52huq ymm1,ymm3,YMMWORD PTR [rsi+0x80]
//  15b:  4c 01 d0               add    rax,r10
//  15e:  62 f2 ed 28 b5 49 04   evpmadd52huq ymm1,ymm2,YMMWORD PTR [rcx+0x80]
//  165:  62 f2 ed 28 b5 01      evpmadd52huq ymm0,ymm2,YMMWORD PTR [rcx]
//  16b:  62 f1 fd 28 6f e1      vmovdqa64 ymm4,ymm1
//  171:  62 f2 ed 28 b5 79 01   evpmadd52huq ymm7,ymm2,YMMWORD PTR [rcx+0x20]
//  178:  62 f2 ed 28 b5 71 02   evpmadd52huq ymm6,ymm2,YMMWORD PTR [rcx+0x40]
//  17f:  62 f2 ed 28 b5 69 03   evpmadd52huq ymm5,ymm2,YMMWORD PTR [rcx+0x60]
  evpmadd52huq(xmm0, xmm3, Address(c_rarg1, 0x00), Assembler::AVX_256bit);
  evalignq(xmm6, xmm5, xmm6, 1, Assembler::AVX_256bit);
  evpmadd52huq(xmm7, xmm3, Address(c_rarg1, 0x20), Assembler::AVX_256bit);
  evalignq(xmm5, xmm1, xmm5, 1, Assembler::AVX_256bit);
  evpmadd52huq(xmm6, xmm3, Address(c_rarg1, 0x40), Assembler::AVX_256bit);
  vmovdqu(xmm1, xmm4);
  evpmadd52huq(xmm5, xmm3, Address(c_rarg1, 0x60), Assembler::AVX_256bit);
  evpmadd52huq(xmm1, xmm3, Address(c_rarg1, 0x80), Assembler::AVX_256bit);
  addq(rax, r10);
  evpmadd52huq(xmm1, xmm2, Address(c_rarg3, 0x80), Assembler::AVX_256bit);
  evpmadd52huq(xmm0, xmm2, Address(c_rarg3, 0x00), Assembler::AVX_256bit);
  vmovdqu(xmm4, xmm1);
  evpmadd52huq(xmm7, xmm2, Address(c_rarg3, 0x20), Assembler::AVX_256bit);
  evpmadd52huq(xmm6, xmm2, Address(c_rarg3, 0x40), Assembler::AVX_256bit);
  evpmadd52huq(xmm5, xmm2, Address(c_rarg3, 0x60), Assembler::AVX_256bit);

//  186:  48 39 d3               cmp    rbx,rdx
//  189:  0f 85 d1 fe ff ff      jne    60 <k1_ifma256_amm52x20+0x60>
  cmpq(rtmp8, c_rarg2);
  jcc(Assembler::notEqual, L_loop);

// NORMALIZATION
//  18f:  c5 ed 73 d5 34         vpsrlq ymm2,ymm5,0x34
//  194:  c5 b5 73 d7 34         vpsrlq ymm9,ymm7,0x34
//  199:  c5 ad 73 d6 34         vpsrlq ymm10,ymm6,0x34
//  19e:  62 d3 ed 28 03 da 03   valignq ymm3,ymm2,ymm10,0x3
//  1a5:  62 f2 fd 28 7c c8      vpbroadcastq ymm1,rax         // ymm1 = Bi, rax = acc0
//  1ab:  62 53 ad 28 03 d1 03   valignq ymm10,ymm10,ymm9,0x3
//  1b2:  c4 e3 7d 02 c1 03      vpblendd ymm0,ymm0,ymm1,0x3
//  1b8:  c5 a5 73 d0 34         vpsrlq ymm11,ymm0,0x34
//  1bd:  c5 f5 73 d4 34         vpsrlq ymm1,ymm4,0x34
//  1c2:  62 73 f5 28 03 e2 03   valignq ymm12,ymm1,ymm2,0x3
//  1c9:  62 53 a5 28 03 c0 03   valignq ymm8,ymm11,ymm8,0x3
  vpsrlq(xmm2, xmm5, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm9, xmm7, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm10, xmm6, 0x34, Assembler::AVX_256bit);
  evalignq(xmm3, xmm2, xmm10, 3, Assembler::AVX_256bit);
  evpbroadcastq(xmm1, rtmp0, Assembler::AVX_256bit);
  evalignq(xmm10, xmm10, xmm9, 3, Assembler::AVX_256bit);
  vpblendd(xmm0, xmm0, xmm1, 0x3, Assembler::AVX_256bit),
  vpsrlq(xmm11, xmm0, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm1, xmm4, 0x34, Assembler::AVX_256bit);
  evalignq(xmm12, xmm1, xmm2, 3, Assembler::AVX_256bit);
  evalignq(xmm8, xmm11, xmm8, 3, Assembler::AVX_256bit);

//  1d0:  62 f1 fd 28 6f 15 00   vmovdqa64 ymm2,YMMWORD PTR [rip+0x0]        # 1da <k1_ifma256_amm52x20+0x1da> // mask - low-order 52 bits set
//  1d7:  00 00 00 
//  1da:  62 53 b5 28 03 cb 03   valignq ymm9,ymm9,ymm11,0x3
//  1e1:  c5 cd db f2            vpand  ymm6,ymm6,ymm2
//  1e5:  c4 c1 4d d4 f2         vpaddq ymm6,ymm6,ymm10
//  1ea:  62 f3 ed 28 1e f6 01   vpcmpltuq k6,ymm2,ymm6   // 0x7fffe1042206
//  1f1:  c5 fd db c2            vpand  ymm0,ymm0,ymm2
//  1f5:  c5 dd db ca            vpand  ymm1,ymm4,ymm2
//  1f9:  c5 d5 db ea            vpand  ymm5,ymm5,ymm2
//  1fd:  c4 c1 7d d4 c0         vpaddq ymm0,ymm0,ymm8
//  202:  c4 c1 75 d4 cc         vpaddq ymm1,ymm1,ymm12
//  207:  c5 d5 d4 eb            vpaddq ymm5,ymm5,ymm3
//  20b:  62 f3 ed 28 1e c0 01   vpcmpltuq k0,ymm2,ymm0
//  212:  62 f3 ed 28 1e cd 01   vpcmpltuq k1,ymm2,ymm5
//  219:  62 f3 ed 28 1e e1 00   vpcmpequq k4,ymm2,ymm1
//  220:  62 f3 ed 28 1e f9 01   vpcmpltuq k7,ymm2,ymm1
  // vmovdqu(xmm2, ExternalAddress((address) L_mask));
  evpbroadcastq(xmm2, rtmp1, Assembler::AVX_256bit);
  evalignq(xmm9, xmm9, xmm11, 3, Assembler::AVX_256bit);
  vpand(xmm6, xmm6, xmm2, Assembler::AVX_256bit);
  vpaddq(xmm6, xmm6, xmm10, Assembler::AVX_256bit);
  evpcmpq(k6, knoreg, xmm2, xmm6, Assembler::noOverflow, false, Assembler::AVX_256bit);
  vpand(xmm0, xmm0, xmm2, Assembler::AVX_256bit);
  vpand(xmm1, xmm4, xmm2, Assembler::AVX_256bit);
  vpand(xmm5, xmm5, xmm2, Assembler::AVX_256bit);
  vpaddq(xmm0, xmm0, xmm8, Assembler::AVX_256bit);
  vpaddq(xmm1, xmm1, xmm12, Assembler::AVX_256bit);
  vpaddq(xmm5, xmm5, xmm3, Assembler::AVX_256bit);
  evpcmpq(k0, knoreg, xmm2, xmm0, Assembler::noOverflow, false, Assembler::AVX_256bit);
  evpcmpq(k1, knoreg, xmm2, xmm5, Assembler::noOverflow, false, Assembler::AVX_256bit);
  evpcmpq(k4, knoreg, xmm2, xmm1, Assembler::overflow, false, Assembler::AVX_256bit);
  evpcmpq(k7, knoreg, xmm2, xmm1, Assembler::noOverflow, false, Assembler::AVX_256bit);

//  227:  c5 f9 93 f6            kmovb  esi,k6
//  22b:  62 f3 ed 28 1e f5 00   vpcmpequq k6,ymm2,ymm5
//  232:  c5 c5 db fa            vpand  ymm7,ymm7,ymm2
//  236:  62 f3 ed 28 1e d0 00   vpcmpequq k2,ymm2,ymm0
//  23d:  62 f3 ed 28 1e de 00   vpcmpequq k3,ymm2,ymm6
//  244:  c4 c1 45 d4 f9         vpaddq ymm7,ymm7,ymm9
//  249:  62 f3 ed 28 1e ef 00   vpcmpequq k5,ymm2,ymm7
//  250:  c5 79 93 c0            kmovb  r8d,k0
//  254:  c5 f9 93 c7            kmovb  eax,k7
//  258:  62 f3 ed 28 1e c7 01   vpcmpltuq k0,ymm2,ymm7
  kmovbl(c_rarg1, k6);
  evpcmpq(k6, knoreg, xmm2, xmm5, Assembler::overflow, false, Assembler::AVX_256bit);
  vpand(xmm7, xmm7, xmm2, Assembler::AVX_256bit);
  evpcmpq(k2, knoreg, xmm2, xmm0, Assembler::overflow, false, Assembler::AVX_256bit);
  evpcmpq(k3, knoreg, xmm2, xmm6, Assembler::overflow, false, Assembler::AVX_256bit);
  vpaddq(xmm7, xmm7, xmm9, Assembler::AVX_256bit);
  evpcmpq(k5, knoreg, xmm2, xmm7, Assembler::overflow, false, Assembler::AVX_256bit);
  kmovbl(c_rarg4, k0);
  kmovbl(rtmp0, k7);
  evpcmpq(k0, knoreg, xmm2, xmm7, Assembler::noOverflow, false, Assembler::AVX_256bit);

//  25f:  c1 e0 10               shl    eax,0x10
//  262:  c5 79 93 c9            kmovb  r9d,k1
//  266:  c5 f9 93 d4            kmovb  edx,k4
//  26a:  41 c1 e1 0c            shl    r9d,0xc
//  26e:  c1 e2 10               shl    edx,0x10
//  271:  c5 f9 93 de            kmovb  ebx,k6
//  275:  c1 e3 0c               shl    ebx,0xc
//  278:  09 da                  or     edx,ebx
//  27a:  44 09 c8               or     eax,r9d
//  27d:  c5 79 93 e2            kmovb  r12d,k2
//  281:  44 09 c0               or     eax,r8d
//  284:  44 09 e2               or     edx,r12d
//  287:  c1 e6 08               shl    esi,0x8
//  28a:  c5 79 93 db            kmovb  r11d,k3
//  28e:  41 c1 e3 08            shl    r11d,0x8
//  292:  44 09 da               or     edx,r11d
//  295:  09 f0                  or     eax,esi
//  297:  c5 f9 93 c8            kmovb  ecx,k0
//  29b:  c5 79 93 d5            kmovb  r10d,k5
//  29f:  c1 e1 04               shl    ecx,0x4
//  2a2:  41 c1 e2 04            shl    r10d,0x4
//  2a6:  44 09 d2               or     edx,r10d
//  2a9:  09 c8                  or     eax,ecx
  shll(rtmp0, 0x10);
  kmovbl(rtmp1, k1);
  kmovbl(c_rarg2, k4);
  shll(rtmp1, 0xc);
  shll(c_rarg2, 0x10);
  kmovbl(rtmp8, k6);
  shll(rtmp8, 0xc);
  orl(c_rarg2, rtmp8);
  orl(rtmp0, rtmp1);
  kmovbl(rtmp4, k2);
  orl(rtmp0, c_rarg4);
  orl(c_rarg2, rtmp4);
  shll(c_rarg1, 8);
  kmovbl(rtmp3, k3);
  shll(rtmp3, 8);
  orl(c_rarg2, rtmp3);
  orl(rtmp0, c_rarg1);
  kmovbl(c_rarg3, k0);
  kmovbl(rtmp2, k5);
  shll(c_rarg3, 4);
  shll(rtmp2, 4);
  orl(c_rarg2, rtmp2);
  orl(rtmp0, c_rarg3);

//  2ab:  8d 04 42               lea    eax,[rdx+rax*2]  // compiles into lea eax,[edx+eax*2]
//  2ae:  31 c2                  xor    edx,eax
//  2b0:  0f b6 c6               movzx  eax,dh
//  2b3:  c5 f9 92 d0            kmovb  k2,eax
//  2b7:  89 d0                  mov    eax,edx
//  2b9:  c1 e8 10               shr    eax,0x10
//  2bc:  c5 f9 92 d8            kmovb  k3,eax
//  2c0:  89 d0                  mov    eax,edx
//  2c2:  c5 f9 92 ca            kmovb  k1,edx
//  2c6:  c1 e8 04               shr    eax,0x4
//  2c9:  c1 ea 0c               shr    edx,0xc
  //  leal(rax, Address(rdx, rax, Address::times_2));
  emit_int8((unsigned char) 0x8d);
  emit_int8((unsigned char) 0x04);
  emit_int8((unsigned char) 0x42);
  xorl(c_rarg2, rtmp0);
  //movzwl(rax, rdx);
  //shrl(rax, 8);
  //andl(rax, 0xff);
  emit_int8((unsigned char) 0x0f);
  emit_int8((unsigned char) 0xb6);
  emit_int8((unsigned char) 0xc6);
  kmovbl(k2, rtmp0);
  movl(rtmp0, c_rarg2);
  shrl(rtmp0, 0x10);
  kmovbl(k3, rtmp0);
  movl(rtmp0, c_rarg2);
  kmovbl(k1, c_rarg2);
  shrl(rtmp0, 4);
  shrl(c_rarg2, 0xc);

//  2cc:  62 f1 fd 29 fb c2      vpsubq ymm0{k1},ymm0,ymm2  // ??????????
//  2d2:  62 f1 cd 2a fb f2      vpsubq ymm6{k2},ymm6,ymm2
//  2d8:  62 f1 f5 2b fb ca      vpsubq ymm1{k3},ymm1,ymm2
//  2de:  c5 f9 92 e0            kmovb  k4,eax
//  2e2:  c5 f9 92 ea            kmovb  k5,edx
//  2e6:  62 f1 c5 2c fb fa      vpsubq ymm7{k4},ymm7,ymm2
//  2ec:  62 f1 d5 2d fb ea      vpsubq ymm5{k5},ymm5,ymm2
//  2f2:  c5 fd db c2            vpand  ymm0,ymm0,ymm2
//  2f6:  c5 c5 db fa            vpand  ymm7,ymm7,ymm2
//  2fa:  c5 cd db f2            vpand  ymm6,ymm6,ymm2
//  2fe:  c5 d5 db ea            vpand  ymm5,ymm5,ymm2
//  302:  c5 f5 db ca            vpand  ymm1,ymm1,ymm2
//  306:  62 f1 fe 28 7f 07      vmovdqu64 YMMWORD PTR [rdi],ymm0
//  30c:  62 f1 fe 28 7f 7f 01   vmovdqu64 YMMWORD PTR [rdi+0x20],ymm7
//  313:  62 f1 fe 28 7f 77 02   vmovdqu64 YMMWORD PTR [rdi+0x40],ymm6
//  31a:  62 f1 fe 28 7f 6f 03   vmovdqu64 YMMWORD PTR [rdi+0x60],ymm5
//  321:  62 f1 fe 28 7f 4f 04   vmovdqu64 YMMWORD PTR [rdi+0x80],ymm1
//  328:  c5 f8 77               vzeroupper 
  evpsubq(xmm0, k1, xmm0, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm6, k2, xmm6, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm1, k3, xmm1, xmm2, true, Assembler::AVX_256bit);
  kmovbl(k4, rtmp0);
  kmovbl(k5, c_rarg2);
  evpsubq(xmm7, k4, xmm7, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm5, k5, xmm5, xmm2, true, Assembler::AVX_256bit);
  vpand(xmm0, xmm0, xmm2, Assembler::AVX_256bit);
  vpand(xmm7, xmm7, xmm2, Assembler::AVX_256bit);
  vpand(xmm6, xmm6, xmm2, Assembler::AVX_256bit);
  vpand(xmm5, xmm5, xmm2, Assembler::AVX_256bit);
  vpand(xmm1, xmm1, xmm2, Assembler::AVX_256bit);
  evmovdquq(Address(c_rarg0, 0x00), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(c_rarg0, 0x20), xmm7, Assembler::AVX_256bit);
  evmovdquq(Address(c_rarg0, 0x40), xmm6, Assembler::AVX_256bit);
  evmovdquq(Address(c_rarg0, 0x60), xmm5, Assembler::AVX_256bit);
  evmovdquq(Address(c_rarg0, 0x80), xmm1, Assembler::AVX_256bit);
  vzeroupper();

#ifdef _WIN64
  pop(rdi);
  pop(rsi);
#endif
  pop(r15);
  pop(r14);
  pop(r13);
  pop(r12);
  pop(rbx);
}

#if 0
void MacroAssembler::montgomeryMultiply52x30(Register out, Register a, Register b, Register m, Register inv)
{
  // Windows regs    |  Linux regs
  // c_rarg0 (rcx)   |  c_rarg0 (rdi)  out
  // c_rarg1 (rdx)   |  c_rarg1 (rsi)  a
  // c_rarg2 (r8)    |  c_rarg2 (rdx)  b
  // c_rarg3 (r9)    |  c_rarg3 (rcx)  m
  // r10             |  c_rarg4 (r8)   inv

  const Register rtmp0 = rax;
#ifdef _WIN64
  const Register rtmp1 = rdi;
  const Register rtmp2 = rsi;
#else
  const Register rtmp1 = r9;
  const Register rtmp2 = r10;
#endif
  const Register rtmp3 = r11;
  const Register rtmp4 = r12;
  const Register rtmp5 = r13;
  const Register rtmp6 = r14;
  const Register rtmp7 = r15;
  const Register rtmp8 = rbx;

#if 0
   4:  55                     push   rbp
   5:  c5 e9 ef d2            vpxor  xmm2,xmm2,xmm2
   9:  31 c0                  xor    eax,eax
   b:  48 89 e5               mov    rbp,rsp
   e:  41 57                  push   r15
  10:  62 f1 fd 28 6f e2      vmovdqa64 ymm4,ymm2
  16:  62 f1 fd 28 6f f2      vmovdqa64 ymm6,ymm2
  1c:  41 56                  push   r14
  1e:  62 71 fd 28 6f c2      vmovdqa64 ymm8,ymm2
  24:  62 f1 fd 28 6f da      vmovdqa64 ymm3,ymm2
  2a:  41 55                  push   r13
  2c:  62 f1 fd 28 6f ea      vmovdqa64 ymm5,ymm2
  32:  62 f1 fd 28 6f fa      vmovdqa64 ymm7,ymm2
  38:  41 54                  push   r12
  3a:  62 f1 fd 28 6f c2      vmovdqa64 ymm0,ymm2
  40:  49 b9 ff ff ff ff ff   movabs r9,0xfffffffffffff
  47:  ff 0f 00 
  4a:  53                     push   rbx
  4b:  62 71 fd 28 6f ca      vmovdqa64 ymm9,ymm2
  51:  48 8d 9a f0 00 00 00   lea    rbx,[rdx+0xf0]
  58:  48 83 e4 e0            and    rsp,0xffffffffffffffe0
  5c:  49 89 d7               mov    r15,rdx
  5f:  4c 8b 29               mov    r13,QWORD PTR [rcx]
  62:  4c 8b 26               mov    r12,QWORD PTR [rsi]
  65:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
  6c:  00 00 00 00 
  70:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
  77:  00 00 00 00 
  7b:  0f 1f 44 00 00         nop    DWORD PTR [rax+rax*1+0x0]
  80:  4d 8b 17               mov    r10,QWORD PTR [r15]
  83:  62 f1 fd 28 6f ca      vmovdqa64 ymm1,ymm2
  89:  4c 89 d2               mov    rdx,r10
  8c:  62 52 fd 28 7c da      vpbroadcastq ymm11,r10
  92:  c4 42 ab f6 dc         mulx   r11,r10,r12
  97:  4c 01 d0               add    rax,r10
  9a:  4d 89 de               mov    r14,r11
  9d:  49 89 c2               mov    r10,rax
  a0:  49 83 d6 00            adc    r14,0x0
  a4:  4d 0f af d0            imul   r10,r8
  a8:  4d 21 ca               and    r10,r9
  ab:  4c 89 d2               mov    rdx,r10
  ae:  62 52 fd 28 7c d2      vpbroadcastq ymm10,r10
  b4:  c4 42 ab f6 dd         mulx   r11,r10,r13
  b9:  4c 89 54 24 f0         mov    QWORD PTR [rsp-0x10],r10
  be:  4c 89 5c 24 f8         mov    QWORD PTR [rsp-0x8],r11
  c3:  45 31 d2               xor    r10d,r10d
  c6:  48 03 44 24 f0         add    rax,QWORD PTR [rsp-0x10]
  cb:  41 0f 92 c2            setb   r10b
  cf:  62 f2 a5 28 b4 06      evpmadd52luq ymm0,ymm11,YMMWORD PTR [rsi]
  d5:  62 72 a5 28 b4 46 01   evpmadd52luq ymm8,ymm11,YMMWORD PTR [rsi+0x20]
  dc:  62 f2 ad 28 b4 01      evpmadd52luq ymm0,ymm10,YMMWORD PTR [rcx]
  e2:  62 72 ad 28 b4 41 01   evpmadd52luq ymm8,ymm10,YMMWORD PTR [rcx+0x20]
  e9:  62 f3 bd 28 03 c0 01   valignq ymm0,ymm8,ymm0,0x1
  f0:  62 f2 a5 28 b4 4e 07   evpmadd52luq ymm1,ymm11,YMMWORD PTR [rsi+0xe0]
  f7:  4c 8b 5c 24 f8         mov    r11,QWORD PTR [rsp-0x8]
  fc:  62 f2 ad 28 b4 49 07   evpmadd52luq ymm1,ymm10,YMMWORD PTR [rcx+0xe0]
 103:  62 f3 b5 28 03 d1 01   valignq ymm2,ymm9,ymm1,0x1
 10a:  4d 01 de               add    r14,r11
 10d:  4d 01 d6               add    r14,r10
 110:  48 c1 e8 34            shr    rax,0x34
 114:  49 c1 e6 0c            shl    r14,0xc
 118:  49 09 c6               or     r14,rax
 11b:  c4 e1 f9 7e c0         vmovq  rax,xmm0
 120:  62 f2 a5 28 b4 7e 02   evpmadd52luq ymm7,ymm11,YMMWORD PTR [rsi+0x40]
 127:  62 f2 a5 28 b4 76 03   evpmadd52luq ymm6,ymm11,YMMWORD PTR [rsi+0x60]
 12e:  62 f2 ad 28 b4 79 02   evpmadd52luq ymm7,ymm10,YMMWORD PTR [rcx+0x40]
 135:  62 f2 ad 28 b4 71 03   evpmadd52luq ymm6,ymm10,YMMWORD PTR [rcx+0x60]
 13c:  62 53 c5 28 03 c0 01   valignq ymm8,ymm7,ymm8,0x1
 143:  62 f2 a5 28 b4 6e 04   evpmadd52luq ymm5,ymm11,YMMWORD PTR [rsi+0x80]
 14a:  62 f3 cd 28 03 ff 01   valignq ymm7,ymm6,ymm7,0x1
 151:  62 f2 ad 28 b4 69 04   evpmadd52luq ymm5,ymm10,YMMWORD PTR [rcx+0x80]
 158:  62 f2 a5 28 b4 66 05   evpmadd52luq ymm4,ymm11,YMMWORD PTR [rsi+0xa0]
 15f:  62 f3 d5 28 03 f6 01   valignq ymm6,ymm5,ymm6,0x1
 166:  62 f2 ad 28 b4 61 05   evpmadd52luq ymm4,ymm10,YMMWORD PTR [rcx+0xa0]
 16d:  62 f2 a5 28 b4 5e 06   evpmadd52luq ymm3,ymm11,YMMWORD PTR [rsi+0xc0]
 174:  62 f3 dd 28 03 ed 01   valignq ymm5,ymm4,ymm5,0x1
 17b:  62 f2 ad 28 b4 59 06   evpmadd52luq ymm3,ymm10,YMMWORD PTR [rcx+0xc0]
 182:  62 f2 a5 28 b5 06      evpmadd52huq ymm0,ymm11,YMMWORD PTR [rsi]
 188:  62 f3 e5 28 03 e4 01   valignq ymm4,ymm3,ymm4,0x1
 18f:  62 72 a5 28 b5 46 01   evpmadd52huq ymm8,ymm11,YMMWORD PTR [rsi+0x20]
 196:  62 f3 f5 28 03 db 01   valignq ymm3,ymm1,ymm3,0x1
 19d:  62 f2 a5 28 b5 76 03   evpmadd52huq ymm6,ymm11,YMMWORD PTR [rsi+0x60]
 1a4:  62 f1 fd 28 6f ca      vmovdqa64 ymm1,ymm2
 1aa:  62 f2 a5 28 b5 6e 04   evpmadd52huq ymm5,ymm11,YMMWORD PTR [rsi+0x80]
 1b1:  62 f2 a5 28 b5 66 05   evpmadd52huq ymm4,ymm11,YMMWORD PTR [rsi+0xa0]
 1b8:  62 f2 a5 28 b5 5e 06   evpmadd52huq ymm3,ymm11,YMMWORD PTR [rsi+0xc0]
 1bf:  62 f2 a5 28 b5 4e 07   evpmadd52huq ymm1,ymm11,YMMWORD PTR [rsi+0xe0]
 1c6:  4c 01 f0               add    rax,r14
 1c9:  62 f2 a5 28 b5 7e 02   evpmadd52huq ymm7,ymm11,YMMWORD PTR [rsi+0x40]
 1d0:  62 f2 ad 28 b5 01      evpmadd52huq ymm0,ymm10,YMMWORD PTR [rcx]
 1d6:  62 72 ad 28 b5 41 01   evpmadd52huq ymm8,ymm10,YMMWORD PTR [rcx+0x20]
 1dd:  62 f2 ad 28 b5 79 02   evpmadd52huq ymm7,ymm10,YMMWORD PTR [rcx+0x40]
 1e4:  49 83 c7 08            add    r15,0x8
 1e8:  62 f2 ad 28 b5 49 07   evpmadd52huq ymm1,ymm10,YMMWORD PTR [rcx+0xe0]
 1ef:  62 f2 ad 28 b5 71 03   evpmadd52huq ymm6,ymm10,YMMWORD PTR [rcx+0x60]
 1f6:  62 f1 fd 28 6f d1      vmovdqa64 ymm2,ymm1
 1fc:  62 f2 ad 28 b5 69 04   evpmadd52huq ymm5,ymm10,YMMWORD PTR [rcx+0x80]
 203:  62 f2 ad 28 b5 61 05   evpmadd52huq ymm4,ymm10,YMMWORD PTR [rcx+0xa0]
 20a:  62 f2 ad 28 b5 59 06   evpmadd52huq ymm3,ymm10,YMMWORD PTR [rcx+0xc0]
 211:  4c 39 fb               cmp    rbx,r15
 214:  0f 85 66 fe ff ff      jne    80 <k1_ifma256_amm52x30+0x80>
 21a:  62 f2 fd 28 7c c8      vpbroadcastq ymm1,rax
 220:  c4 e3 7d 02 c1 03      vpblendd ymm0,ymm0,ymm1,0x3
 226:  62 f1 f5 20 73 d0 34   vpsrlq ymm17,ymm0,0x34
 22d:  62 53 f5 20 03 c9 03   valignq ymm9,ymm17,ymm9,0x3
 234:  c5 a5 73 d3 34         vpsrlq ymm11,ymm3,0x34
 239:  c5 ad 73 d4 34         vpsrlq ymm10,ymm4,0x34
 23e:  c5 95 73 d2 34         vpsrlq ymm13,ymm2,0x34
 243:  62 53 a5 28 03 f2 03   valignq ymm14,ymm11,ymm10,0x3
 24a:  c5 f5 73 d6 34         vpsrlq ymm1,ymm6,0x34
 24f:  62 53 95 28 03 eb 03   valignq ymm13,ymm13,ymm11,0x3
 256:  c4 c1 1d 73 d0 34      vpsrlq ymm12,ymm8,0x34
 25c:  62 f1 fd 20 73 d7 34   vpsrlq ymm16,ymm7,0x34
 263:  c5 85 73 d5 34         vpsrlq ymm15,ymm5,0x34
 268:  62 33 f5 28 03 d8 03   valignq ymm11,ymm1,ymm16,0x3
 26f:  62 53 ad 28 03 d7 03   valignq ymm10,ymm10,ymm15,0x3
 276:  62 c3 fd 20 03 c4 03   valignq ymm16,ymm16,ymm12,0x3
 27d:  62 73 85 28 03 f9 03   valignq ymm15,ymm15,ymm1,0x3
 284:  62 f1 fd 28 6f 0d 00   vmovdqa64 ymm1,YMMWORD PTR [rip+0x0]        # 28e <k1_ifma256_amm52x30+0x28e>
 28b:  00 00 00 
 28e:  62 33 9d 28 03 e1 03   valignq ymm12,ymm12,ymm17,0x3
 295:  c5 fd db c1            vpand  ymm0,ymm0,ymm1
 299:  c4 41 7d d4 c9         vpaddq ymm9,ymm0,ymm9
 29e:  62 d3 f5 28 1e c1 01   vpcmpltuq k0,ymm1,ymm9
 2a5:  c5 e5 db d9            vpand  ymm3,ymm3,ymm1
 2a9:  c5 ed db d1            vpand  ymm2,ymm2,ymm1
 2ad:  c4 c1 65 d4 de         vpaddq ymm3,ymm3,ymm14
 2b2:  c4 c1 6d d4 d5         vpaddq ymm2,ymm2,ymm13
 2b7:  c5 c5 db f9            vpand  ymm7,ymm7,ymm1
 2bb:  c5 dd db e1            vpand  ymm4,ymm4,ymm1
 2bf:  62 f3 f5 28 1e db 01   vpcmpltuq k3,ymm1,ymm3
 2c6:  62 f3 f5 28 1e fa 01   vpcmpltuq k7,ymm1,ymm2
 2cd:  62 b1 c5 28 d4 f8      vpaddq ymm7,ymm7,ymm16
 2d3:  c4 c1 5d d4 e2         vpaddq ymm4,ymm4,ymm10
 2d8:  62 f3 f5 28 1e f4 01   vpcmpltuq k6,ymm1,ymm4
 2df:  c5 d5 db e9            vpand  ymm5,ymm5,ymm1
 2e3:  c5 79 93 f0            kmovb  r14d,k0
 2e7:  62 f3 f5 28 1e c7 01   vpcmpltuq k0,ymm1,ymm7
 2ee:  c4 c1 55 d4 ef         vpaddq ymm5,ymm5,ymm15
 2f3:  c5 cd db f1            vpand  ymm6,ymm6,ymm1
 2f7:  62 f3 f5 28 1e d5 01   vpcmpltuq k2,ymm1,ymm5
 2fe:  c4 c1 4d d4 f3         vpaddq ymm6,ymm6,ymm11
 303:  c5 f9 91 5c 24 f0      kmovb  BYTE PTR [rsp-0x10],k3
 309:  c5 f9 91 7c 24 e8      kmovb  BYTE PTR [rsp-0x18],k7
 30f:  62 f3 f5 28 1e db 00   vpcmpequq k3,ymm1,ymm3
 316:  62 f3 f5 28 1e fa 00   vpcmpequq k7,ymm1,ymm2
 31d:  62 f3 f5 28 1e ee 01   vpcmpltuq k5,ymm1,ymm6
 324:  c5 3d db c1            vpand  ymm8,ymm8,ymm1
 328:  c5 f9 93 d0            kmovb  edx,k0
 32c:  c5 79 93 c6            kmovb  r8d,k6
 330:  62 d3 f5 28 1e c1 00   vpcmpequq k0,ymm1,ymm9
 337:  62 f3 f5 28 1e f4 00   vpcmpequq k6,ymm1,ymm4
 33e:  c4 41 3d d4 c4         vpaddq ymm8,ymm8,ymm12
 343:  62 d3 f5 28 1e e0 01   vpcmpltuq k4,ymm1,ymm8
 34a:  c5 f9 93 f2            kmovb  esi,k2
 34e:  c5 79 93 eb            kmovb  r13d,k3
 352:  62 f3 f5 28 1e d5 00   vpcmpequq k2,ymm1,ymm5
 359:  41 c1 e5 18            shl    r13d,0x18
 35d:  c5 79 93 ff            kmovb  r15d,k7
 361:  41 c1 e7 1c            shl    r15d,0x1c
 365:  45 09 fd               or     r13d,r15d
 368:  c5 f9 93 cd            kmovb  ecx,k5
 36c:  c5 79 93 e0            kmovb  r12d,k0
 370:  62 f3 f5 28 1e ee 00   vpcmpequq k5,ymm1,ymm6
 377:  45 0f b6 fc            movzx  r15d,r12b
 37b:  c5 f9 93 c6            kmovb  eax,k6
 37f:  c1 e0 14               shl    eax,0x14
 382:  62 f3 f5 28 1e cf 00   vpcmpequq k1,ymm1,ymm7
 389:  45 09 fd               or     r13d,r15d
 38c:  41 89 c7               mov    r15d,eax
 38f:  45 09 ef               or     r15d,r13d
 392:  c5 f9 91 64 24 ef      kmovb  BYTE PTR [rsp-0x11],k4
 398:  c5 f9 93 da            kmovb  ebx,k2
 39c:  62 d3 f5 28 1e e0 00   vpcmpequq k4,ymm1,ymm8
 3a3:  c1 e3 10               shl    ebx,0x10
 3a6:  41 09 df               or     r15d,ebx
 3a9:  c5 79 93 dd            kmovb  r11d,k5
 3ad:  41 c1 e3 0c            shl    r11d,0xc
 3b1:  45 09 df               or     r15d,r11d
 3b4:  c5 79 93 d1            kmovb  r10d,k1
 3b8:  41 c1 e2 08            shl    r10d,0x8
 3bc:  45 09 fa               or     r10d,r15d
 3bf:  c5 79 93 cc            kmovb  r9d,k4
 3c3:  41 0f b6 c1            movzx  eax,r9b
 3c7:  c1 e0 04               shl    eax,0x4
 3ca:  45 89 d1               mov    r9d,r10d
 3cd:  41 09 c1               or     r9d,eax
 3d0:  44 8b 54 24 f0         mov    r10d,DWORD PTR [rsp-0x10]
 3d5:  8b 44 24 e8            mov    eax,DWORD PTR [rsp-0x18]
 3d9:  41 c1 e2 18            shl    r10d,0x18
 3dd:  c1 e0 1c               shl    eax,0x1c
 3e0:  44 09 d0               or     eax,r10d
 3e3:  41 09 c6               or     r14d,eax
 3e6:  41 c1 e0 14            shl    r8d,0x14
 3ea:  45 09 f0               or     r8d,r14d
 3ed:  c1 e6 10               shl    esi,0x10
 3f0:  44 09 c6               or     esi,r8d
 3f3:  0f b6 44 24 ef         movzx  eax,BYTE PTR [rsp-0x11]
 3f8:  c1 e1 0c               shl    ecx,0xc
 3fb:  09 f1                  or     ecx,esi
 3fd:  c1 e2 08               shl    edx,0x8
 400:  09 ca                  or     edx,ecx
 402:  c1 e0 04               shl    eax,0x4
 405:  09 c2                  or     edx,eax
 407:  41 8d 04 51            lea    eax,[r9+rdx*2]
 40b:  41 31 c1               xor    r9d,eax
 40e:  44 89 c8               mov    eax,r9d
 411:  0f b6 c4               movzx  eax,ah
 414:  c5 f9 92 d0            kmovb  k2,eax
 418:  44 89 c8               mov    eax,r9d
 41b:  c1 e8 10               shr    eax,0x10
 41e:  c5 f9 92 d8            kmovb  k3,eax
 422:  44 89 c8               mov    eax,r9d
 425:  c1 e8 18               shr    eax,0x18
 428:  c5 f9 92 e0            kmovb  k4,eax
 42c:  44 89 c8               mov    eax,r9d
 42f:  c1 e8 04               shr    eax,0x4
 432:  c5 f9 92 e8            kmovb  k5,eax
 436:  44 89 c8               mov    eax,r9d
 439:  c1 e8 0c               shr    eax,0xc
 43c:  c5 f9 92 f0            kmovb  k6,eax
 440:  44 89 c8               mov    eax,r9d
 443:  c4 c1 79 92 c9         kmovb  k1,r9d
 448:  c1 e8 14               shr    eax,0x14
 44b:  41 c1 e9 1c            shr    r9d,0x1c
 44f:  62 71 b5 29 fb c9      vpsubq ymm9{k1},ymm9,ymm1
 455:  62 f1 c5 2a fb f9      vpsubq ymm7{k2},ymm7,ymm1
 45b:  62 f1 d5 2b fb e9      vpsubq ymm5{k3},ymm5,ymm1
 461:  62 f1 e5 2c fb d9      vpsubq ymm3{k4},ymm3,ymm1
 467:  62 71 bd 2d fb c1      vpsubq ymm8{k5},ymm8,ymm1
 46d:  62 f1 cd 2e fb f1      vpsubq ymm6{k6},ymm6,ymm1
 473:  c5 f9 92 f8            kmovb  k7,eax
 477:  c4 c1 79 92 c9         kmovb  k1,r9d
 47c:  62 f1 dd 2f fb e1      vpsubq ymm4{k7},ymm4,ymm1
 482:  62 f1 ed 29 fb d1      vpsubq ymm2{k1},ymm2,ymm1
 488:  c5 35 db c9            vpand  ymm9,ymm9,ymm1
 48c:  c5 3d db c1            vpand  ymm8,ymm8,ymm1
 490:  c5 c5 db f9            vpand  ymm7,ymm7,ymm1
 494:  c5 cd db f1            vpand  ymm6,ymm6,ymm1
 498:  c5 d5 db e9            vpand  ymm5,ymm5,ymm1
 49c:  c5 dd db e1            vpand  ymm4,ymm4,ymm1
 4a0:  c5 e5 db d9            vpand  ymm3,ymm3,ymm1
 4a4:  c5 ed db d1            vpand  ymm2,ymm2,ymm1
 4a8:  62 71 fe 28 7f 0f      vmovdqu64 YMMWORD PTR [rdi],ymm9
 4ae:  62 71 fe 28 7f 47 01   vmovdqu64 YMMWORD PTR [rdi+0x20],ymm8
 4b5:  62 f1 fe 28 7f 7f 02   vmovdqu64 YMMWORD PTR [rdi+0x40],ymm7
 4bc:  62 f1 fe 28 7f 77 03   vmovdqu64 YMMWORD PTR [rdi+0x60],ymm6
 4c3:  62 f1 fe 28 7f 6f 04   vmovdqu64 YMMWORD PTR [rdi+0x80],ymm5
 4ca:  62 f1 fe 28 7f 67 05   vmovdqu64 YMMWORD PTR [rdi+0xa0],ymm4
 4d1:  62 f1 fe 28 7f 5f 06   vmovdqu64 YMMWORD PTR [rdi+0xc0],ymm3
 4d8:  62 f1 fe 28 7f 57 07   vmovdqu64 YMMWORD PTR [rdi+0xe0],ymm2
 4df:  c5 f8 77               vzeroupper 
 4e2:  48 8d 65 d8            lea    rsp,[rbp-0x28]
 4e6:  5b                     pop    rbx
 4e7:  41 5c                  pop    r12
 4e9:  41 5d                  pop    r13
 4eb:  41 5e                  pop    r14
 4ed:  41 5f                  pop    r15
 4ef:  5d                     pop    rbp
 4f0:  c3                     ret    
 4f1:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
 4f8:  00 00 00 00 
 4fc:  0f 1f 40 00            nop    DWORD PTR [rax+0x0]

0000000000000500 <k1_ifma256_ams52x30>:
 500:  f3 0f 1e fa            endbr64 
 504:  49 89 c8               mov    r8,rcx
 507:  48 89 d1               mov    rcx,rdx
 50a:  48 89 f2               mov    rdx,rsi
 50d:  e9 00 00 00 00         jmp    512 <k1_ifma256_ams52x30+0x12>
#endif
}


void MacroAssembler::montgomeryMultiply52x40(Register out, Register a, Register b, Register m, Register inv)
{
  // Windows regs    |  Linux regs
  // c_rarg0 (rcx)   |  c_rarg0 (rdi)  out
  // c_rarg1 (rdx)   |  c_rarg1 (rsi)  a
  // c_rarg2 (r8)    |  c_rarg2 (rdx)  b
  // c_rarg3 (r9)    |  c_rarg3 (rcx)  m
  // r10             |  c_rarg4 (r8)   inv

  const Register rtmp0 = rax;
#ifdef _WIN64
  const Register rtmp1 = rdi;
  const Register rtmp2 = rsi;
#else
  const Register rtmp1 = r9;
  const Register rtmp2 = r10;
#endif
  const Register rtmp3 = r11;
  const Register rtmp4 = r12;
  const Register rtmp5 = r13;
  const Register rtmp6 = r14;
  const Register rtmp7 = r15;
  const Register rtmp8 = rbx;

#if 0

   4:  55                     push   rbp
   5:  c5 e9 ef d2            vpxor  xmm2,xmm2,xmm2
   9:  31 c0                  xor    eax,eax
   b:  48 89 e5               mov    rbp,rsp
   e:  41 57                  push   r15
  10:  62 f1 fd 28 6f e2      vmovdqa64 ymm4,ymm2
  16:  62 f1 fd 28 6f f2      vmovdqa64 ymm6,ymm2
  1c:  41 56                  push   r14
  1e:  62 71 fd 28 6f c2      vmovdqa64 ymm8,ymm2
  24:  62 71 fd 28 6f d2      vmovdqa64 ymm10,ymm2
  2a:  41 55                  push   r13
  2c:  62 f1 fd 28 6f da      vmovdqa64 ymm3,ymm2
  32:  62 f1 fd 28 6f ea      vmovdqa64 ymm5,ymm2
  38:  41 54                  push   r12
  3a:  62 f1 fd 28 6f fa      vmovdqa64 ymm7,ymm2
  40:  62 71 fd 28 6f ca      vmovdqa64 ymm9,ymm2
  46:  53                     push   rbx
  47:  62 f1 fd 28 6f c2      vmovdqa64 ymm0,ymm2
  4d:  48 8d 9a 40 01 00 00   lea    rbx,[rdx+0x140]
  54:  48 83 e4 e0            and    rsp,0xffffffffffffffe0
  58:  49 b9 ff ff ff ff ff   movabs r9,0xfffffffffffff
  5f:  ff 0f 00 
  62:  62 71 fd 28 6f da      vmovdqa64 ymm11,ymm2
  68:  49 89 d7               mov    r15,rdx
  6b:  4c 8b 29               mov    r13,QWORD PTR [rcx]
  6e:  4c 8b 26               mov    r12,QWORD PTR [rsi]
  71:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
  78:  00 00 00 00 
  7c:  0f 1f 40 00            nop    DWORD PTR [rax+0x0]
  80:  4d 8b 17               mov    r10,QWORD PTR [r15]
  83:  62 f1 fd 28 6f ca      vmovdqa64 ymm1,ymm2
  89:  4c 89 d2               mov    rdx,r10
  8c:  62 52 fd 28 7c ea      vpbroadcastq ymm13,r10
  92:  c4 42 ab f6 dc         mulx   r11,r10,r12
  97:  4c 01 d0               add    rax,r10
  9a:  4d 89 de               mov    r14,r11
  9d:  49 89 c2               mov    r10,rax
  a0:  49 83 d6 00            adc    r14,0x0
  a4:  4d 0f af d0            imul   r10,r8
  a8:  4d 21 ca               and    r10,r9
  ab:  4c 89 d2               mov    rdx,r10
  ae:  62 52 fd 28 7c e2      vpbroadcastq ymm12,r10
  b4:  c4 42 ab f6 dd         mulx   r11,r10,r13
  b9:  4c 89 54 24 f0         mov    QWORD PTR [rsp-0x10],r10
  be:  4c 89 5c 24 f8         mov    QWORD PTR [rsp-0x8],r11
  c3:  45 31 d2               xor    r10d,r10d
  c6:  48 03 44 24 f0         add    rax,QWORD PTR [rsp-0x10]
  cb:  41 0f 92 c2            setb   r10b
  cf:  62 f2 95 28 b4 06      evpmadd52luq ymm0,ymm13,YMMWORD PTR [rsi]
  d5:  62 72 95 28 b4 56 01   evpmadd52luq ymm10,ymm13,YMMWORD PTR [rsi+0x20]
  dc:  62 f2 9d 28 b4 01      evpmadd52luq ymm0,ymm12,YMMWORD PTR [rcx]
  e2:  62 72 9d 28 b4 51 01   evpmadd52luq ymm10,ymm12,YMMWORD PTR [rcx+0x20]
  e9:  62 f3 ad 28 03 c0 01   valignq ymm0,ymm10,ymm0,0x1
  f0:  4c 8b 5c 24 f8         mov    r11,QWORD PTR [rsp-0x8]
  f5:  62 f2 95 28 b4 4e 09   evpmadd52luq ymm1,ymm13,YMMWORD PTR [rsi+0x120]
  fc:  62 f2 9d 28 b4 49 09   evpmadd52luq ymm1,ymm12,YMMWORD PTR [rcx+0x120]
 103:  62 f3 a5 28 03 d1 01   valignq ymm2,ymm11,ymm1,0x1
 10a:  4d 01 de               add    r14,r11
 10d:  4d 01 d6               add    r14,r10
 110:  48 c1 e8 34            shr    rax,0x34
 114:  49 c1 e6 0c            shl    r14,0xc
 118:  49 09 c6               or     r14,rax
 11b:  c4 e1 f9 7e c0         vmovq  rax,xmm0
 120:  62 72 95 28 b4 4e 02   evpmadd52luq ymm9,ymm13,YMMWORD PTR [rsi+0x40]
 127:  62 72 95 28 b4 46 03   evpmadd52luq ymm8,ymm13,YMMWORD PTR [rsi+0x60]
 12e:  62 72 9d 28 b4 49 02   evpmadd52luq ymm9,ymm12,YMMWORD PTR [rcx+0x40]
 135:  62 72 9d 28 b4 41 03   evpmadd52luq ymm8,ymm12,YMMWORD PTR [rcx+0x60]
 13c:  62 53 b5 28 03 d2 01   valignq ymm10,ymm9,ymm10,0x1
 143:  62 f2 95 28 b4 7e 04   evpmadd52luq ymm7,ymm13,YMMWORD PTR [rsi+0x80]
 14a:  62 53 bd 28 03 c9 01   valignq ymm9,ymm8,ymm9,0x1
 151:  62 f2 9d 28 b4 79 04   evpmadd52luq ymm7,ymm12,YMMWORD PTR [rcx+0x80]
 158:  62 f2 95 28 b4 76 05   evpmadd52luq ymm6,ymm13,YMMWORD PTR [rsi+0xa0]
 15f:  62 53 c5 28 03 c0 01   valignq ymm8,ymm7,ymm8,0x1
 166:  62 f2 9d 28 b4 71 05   evpmadd52luq ymm6,ymm12,YMMWORD PTR [rcx+0xa0]
 16d:  62 f2 95 28 b4 6e 06   evpmadd52luq ymm5,ymm13,YMMWORD PTR [rsi+0xc0]
 174:  62 f3 cd 28 03 ff 01   valignq ymm7,ymm6,ymm7,0x1
 17b:  62 f2 9d 28 b4 69 06   evpmadd52luq ymm5,ymm12,YMMWORD PTR [rcx+0xc0]
 182:  62 f2 95 28 b4 66 07   evpmadd52luq ymm4,ymm13,YMMWORD PTR [rsi+0xe0]
 189:  62 f3 d5 28 03 f6 01   valignq ymm6,ymm5,ymm6,0x1
 190:  62 f2 9d 28 b4 61 07   evpmadd52luq ymm4,ymm12,YMMWORD PTR [rcx+0xe0]
 197:  62 f2 95 28 b4 5e 08   evpmadd52luq ymm3,ymm13,YMMWORD PTR [rsi+0x100]
 19e:  62 f3 dd 28 03 ed 01   valignq ymm5,ymm4,ymm5,0x1
 1a5:  62 f2 9d 28 b4 59 08   evpmadd52luq ymm3,ymm12,YMMWORD PTR [rcx+0x100]
 1ac:  62 f2 95 28 b5 06      evpmadd52huq ymm0,ymm13,YMMWORD PTR [rsi]
 1b2:  62 72 95 28 b5 56 01   evpmadd52huq ymm10,ymm13,YMMWORD PTR [rsi+0x20]
 1b9:  62 72 95 28 b5 4e 02   evpmadd52huq ymm9,ymm13,YMMWORD PTR [rsi+0x40]
 1c0:  62 72 95 28 b5 46 03   evpmadd52huq ymm8,ymm13,YMMWORD PTR [rsi+0x60]
 1c7:  62 f2 95 28 b5 7e 04   evpmadd52huq ymm7,ymm13,YMMWORD PTR [rsi+0x80]
 1ce:  62 f2 95 28 b5 76 05   evpmadd52huq ymm6,ymm13,YMMWORD PTR [rsi+0xa0]
 1d5:  4c 01 f0               add    rax,r14
 1d8:  62 f2 95 28 b5 6e 06   evpmadd52huq ymm5,ymm13,YMMWORD PTR [rsi+0xc0]
 1df:  49 83 c7 08            add    r15,0x8
 1e3:  62 f3 e5 28 03 e4 01   valignq ymm4,ymm3,ymm4,0x1
 1ea:  62 f2 9d 28 b5 01      evpmadd52huq ymm0,ymm12,YMMWORD PTR [rcx]
 1f0:  62 f3 f5 28 03 db 01   valignq ymm3,ymm1,ymm3,0x1
 1f7:  62 f2 95 28 b5 66 07   evpmadd52huq ymm4,ymm13,YMMWORD PTR [rsi+0xe0]
 1fe:  62 f1 fd 28 6f ca      vmovdqa64 ymm1,ymm2
 204:  62 f2 95 28 b5 5e 08   evpmadd52huq ymm3,ymm13,YMMWORD PTR [rsi+0x100]
 20b:  62 f2 95 28 b5 4e 09   evpmadd52huq ymm1,ymm13,YMMWORD PTR [rsi+0x120]
 212:  62 72 9d 28 b5 51 01   evpmadd52huq ymm10,ymm12,YMMWORD PTR [rcx+0x20]
 219:  62 f2 9d 28 b5 49 09   evpmadd52huq ymm1,ymm12,YMMWORD PTR [rcx+0x120]
 220:  62 72 9d 28 b5 49 02   evpmadd52huq ymm9,ymm12,YMMWORD PTR [rcx+0x40]
 227:  62 f1 fd 28 6f d1      vmovdqa64 ymm2,ymm1
 22d:  62 72 9d 28 b5 41 03   evpmadd52huq ymm8,ymm12,YMMWORD PTR [rcx+0x60]
 234:  62 f2 9d 28 b5 79 04   evpmadd52huq ymm7,ymm12,YMMWORD PTR [rcx+0x80]
 23b:  62 f2 9d 28 b5 71 05   evpmadd52huq ymm6,ymm12,YMMWORD PTR [rcx+0xa0]
 242:  62 f2 9d 28 b5 69 06   evpmadd52huq ymm5,ymm12,YMMWORD PTR [rcx+0xc0]
 249:  62 f2 9d 28 b5 61 07   evpmadd52huq ymm4,ymm12,YMMWORD PTR [rcx+0xe0]
 250:  62 f2 9d 28 b5 59 08   evpmadd52huq ymm3,ymm12,YMMWORD PTR [rcx+0x100]
 257:  4c 39 fb               cmp    rbx,r15
 25a:  0f 85 20 fe ff ff      jne    80 <k1_ifma256_amm52x40+0x80>
 260:  62 f2 fd 28 7c c8      vpbroadcastq ymm1,rax
 266:  c4 e3 7d 02 c1 03      vpblendd ymm0,ymm0,ymm1,0x3
 26c:  62 f1 d5 20 73 d0 34   vpsrlq ymm21,ymm0,0x34
 273:  62 53 d5 20 03 db 03   valignq ymm11,ymm21,ymm11,0x3
 27a:  c5 f5 73 d7 34         vpsrlq ymm1,ymm7,0x34
 27f:  62 d1 fd 20 73 d2 34   vpsrlq ymm16,ymm10,0x34
 286:  62 d1 dd 20 73 d1 34   vpsrlq ymm20,ymm9,0x34
 28d:  c4 c1 05 73 d0 34      vpsrlq ymm15,ymm8,0x34
 293:  c5 8d 73 d6 34         vpsrlq ymm14,ymm6,0x34
 298:  62 f1 ed 20 73 d5 34   vpsrlq ymm18,ymm5,0x34
 29f:  c5 95 73 d4 34         vpsrlq ymm13,ymm4,0x34
 2a4:  62 f1 f5 20 73 d3 34   vpsrlq ymm17,ymm3,0x34
 2ab:  c5 9d 73 d2 34         vpsrlq ymm12,ymm2,0x34
 2b0:  62 c3 f5 28 03 df 03   valignq ymm19,ymm1,ymm15,0x3
 2b7:  62 33 9d 28 03 e1 03   valignq ymm12,ymm12,ymm17,0x3
 2be:  62 33 85 28 03 fc 03   valignq ymm15,ymm15,ymm20,0x3
 2c5:  62 c3 f5 20 03 cd 03   valignq ymm17,ymm17,ymm13,0x3
 2cc:  62 a3 dd 20 03 e0 03   valignq ymm20,ymm20,ymm16,0x3
 2d3:  62 33 95 28 03 ea 03   valignq ymm13,ymm13,ymm18,0x3
 2da:  62 c3 ed 20 03 d6 03   valignq ymm18,ymm18,ymm14,0x3
 2e1:  62 73 8d 28 03 f1 03   valignq ymm14,ymm14,ymm1,0x3
 2e8:  62 f1 fd 28 6f 0d 00   vmovdqa64 ymm1,YMMWORD PTR [rip+0x0]        # 2f2 <k1_ifma256_amm52x40+0x2f2>
 2ef:  00 00 00 
 2f2:  62 a3 fd 20 03 c5 03   valignq ymm16,ymm16,ymm21,0x3
 2f9:  c5 fd db c1            vpand  ymm0,ymm0,ymm1
 2fd:  c4 41 7d d4 db         vpaddq ymm11,ymm0,ymm11
 302:  62 d3 f5 28 1e c3 01   vpcmpltuq k0,ymm1,ymm11
 309:  c5 35 db c9            vpand  ymm9,ymm9,ymm1
 30d:  c5 d5 db e9            vpand  ymm5,ymm5,ymm1
 311:  62 31 b5 28 d4 cc      vpaddq ymm9,ymm9,ymm20
 317:  62 b1 d5 28 d4 ea      vpaddq ymm5,ymm5,ymm18
 31d:  62 f3 f5 28 1e ed 01   vpcmpltuq k5,ymm1,ymm5
 324:  c5 79 93 f8            kmovb  r15d,k0
 328:  62 d3 f5 28 1e c1 01   vpcmpltuq k0,ymm1,ymm9
 32f:  c5 3d db c1            vpand  ymm8,ymm8,ymm1
 333:  c4 41 3d d4 c7         vpaddq ymm8,ymm8,ymm15
 338:  c5 c5 db f9            vpand  ymm7,ymm7,ymm1
 33c:  c5 dd db e1            vpand  ymm4,ymm4,ymm1
 340:  62 b1 c5 28 d4 fb      vpaddq ymm7,ymm7,ymm19
 346:  c4 c1 5d d4 e5         vpaddq ymm4,ymm4,ymm13
 34b:  c5 f9 91 44 24 f0      kmovb  BYTE PTR [rsp-0x10],k0
 351:  c5 f9 93 d5            kmovb  edx,k5
 355:  62 d3 f5 28 1e c0 01   vpcmpltuq k0,ymm1,ymm8
 35c:  62 d3 f5 28 1e e9 00   vpcmpequq k5,ymm1,ymm9
 363:  62 f3 f5 28 1e e7 01   vpcmpltuq k4,ymm1,ymm7
 36a:  62 f3 f5 28 1e d4 01   vpcmpltuq k2,ymm1,ymm4
 371:  c5 e5 db d9            vpand  ymm3,ymm3,ymm1
 375:  c5 ed db d1            vpand  ymm2,ymm2,ymm1
 379:  62 b1 e5 28 d4 d9      vpaddq ymm3,ymm3,ymm17
 37f:  c4 c1 6d d4 d4         vpaddq ymm2,ymm2,ymm12
 384:  c5 f9 91 44 24 ed      kmovb  BYTE PTR [rsp-0x13],k0
 38a:  c5 79 93 e5            kmovb  r12d,k5
 38e:  62 f3 f5 28 1e c3 00   vpcmpequq k0,ymm1,ymm3
 395:  62 f3 f5 28 1e ea 00   vpcmpequq k5,ymm1,ymm2
 39c:  c5 f9 91 64 24 ef      kmovb  BYTE PTR [rsp-0x11],k4
 3a2:  c5 f9 91 54 24 eb      kmovb  BYTE PTR [rsp-0x15],k2
 3a8:  62 d3 f5 28 1e e3 00   vpcmpequq k4,ymm1,ymm11
 3af:  62 d3 f5 28 1e d0 00   vpcmpequq k2,ymm1,ymm8
 3b6:  62 f3 f5 28 1e da 01   vpcmpltuq k3,ymm1,ymm2
 3bd:  c5 79 93 d0            kmovb  r10d,k0
 3c1:  c5 79 93 f5            kmovb  r14d,k5
 3c5:  49 c1 e2 20            shl    r10,0x20
 3c9:  49 c1 e6 24            shl    r14,0x24
 3cd:  62 f3 f5 28 1e f3 01   vpcmpltuq k6,ymm1,ymm3
 3d4:  4d 09 f2               or     r10,r14
 3d7:  c5 79 93 ec            kmovb  r13d,k4
 3db:  c5 f9 93 c2            kmovb  eax,k2
 3df:  45 0f b6 f5            movzx  r14d,r13b
 3e3:  48 c1 e0 0c            shl    rax,0xc
 3e7:  4d 09 f2               or     r10,r14
 3ea:  c5 f9 91 5c 24 ea      kmovb  BYTE PTR [rsp-0x16],k3
 3f0:  49 89 c6               mov    r14,rax
 3f3:  0f b6 44 24 ea         movzx  eax,BYTE PTR [rsp-0x16]
 3f8:  c5 79 93 de            kmovb  r11d,k6
 3fc:  48 c1 e0 24            shl    rax,0x24
 400:  49 c1 e3 20            shl    r11,0x20
 404:  4c 09 d8               or     rax,r11
 407:  c5 cd db f1            vpand  ymm6,ymm6,ymm1
 40b:  c4 c1 4d d4 f6         vpaddq ymm6,ymm6,ymm14
 410:  4c 09 f8               or     rax,r15
 413:  c5 2d db d1            vpand  ymm10,ymm10,ymm1
 417:  44 0f b6 7c 24 eb      movzx  r15d,BYTE PTR [rsp-0x15]
 41d:  62 f3 f5 28 1e ce 01   vpcmpltuq k1,ymm1,ymm6
 424:  62 31 ad 28 d4 d0      vpaddq ymm10,ymm10,ymm16
 42a:  62 d3 f5 28 1e fa 01   vpcmpltuq k7,ymm1,ymm10
 431:  49 c1 e7 1c            shl    r15,0x1c
 435:  4c 09 f8               or     rax,r15
 438:  48 c1 e2 18            shl    rdx,0x18
 43c:  62 f3 f5 28 1e e4 00   vpcmpequq k4,ymm1,ymm4
 443:  48 09 d0               or     rax,rdx
 446:  c5 f9 91 4c 24 ec      kmovb  BYTE PTR [rsp-0x14],k1
 44c:  0f b6 54 24 ec         movzx  edx,BYTE PTR [rsp-0x14]
 451:  c5 f9 91 7c 24 ee      kmovb  BYTE PTR [rsp-0x12],k7
 457:  62 f3 f5 28 1e fd 00   vpcmpequq k7,ymm1,ymm5
 45e:  62 f3 f5 28 1e de 00   vpcmpequq k3,ymm1,ymm6
 465:  48 c1 e2 14            shl    rdx,0x14
 469:  62 f3 f5 28 1e f7 00   vpcmpequq k6,ymm1,ymm7
 470:  48 09 d0               or     rax,rdx
 473:  c5 79 93 cc            kmovb  r9d,k4
 477:  0f b6 54 24 ef         movzx  edx,BYTE PTR [rsp-0x11]
 47c:  49 c1 e1 1c            shl    r9,0x1c
 480:  4d 09 ca               or     r10,r9
 483:  c5 79 93 c7            kmovb  r8d,k7
 487:  49 c1 e0 18            shl    r8,0x18
 48b:  4d 09 c2               or     r10,r8
 48e:  48 c1 e2 10            shl    rdx,0x10
 492:  c5 f9 93 f3            kmovb  esi,k3
 496:  48 c1 e6 14            shl    rsi,0x14
 49a:  49 09 f2               or     r10,rsi
 49d:  48 09 d0               or     rax,rdx
 4a0:  c5 f9 93 ce            kmovb  ecx,k6
 4a4:  0f b6 54 24 ed         movzx  edx,BYTE PTR [rsp-0x13]
 4a9:  48 c1 e1 10            shl    rcx,0x10
 4ad:  62 d3 f5 28 1e ca 00   vpcmpequq k1,ymm1,ymm10
 4b4:  49 09 ca               or     r10,rcx
 4b7:  4d 09 d6               or     r14,r10
 4ba:  48 c1 e2 0c            shl    rdx,0xc
 4be:  4d 89 e2               mov    r10,r12
 4c1:  49 c1 e2 08            shl    r10,0x8
 4c5:  48 09 d0               or     rax,rdx
 4c8:  0f b6 54 24 f0         movzx  edx,BYTE PTR [rsp-0x10]
 4cd:  4d 09 d6               or     r14,r10
 4d0:  c5 f9 93 d9            kmovb  ebx,k1
 4d4:  48 c1 e3 04            shl    rbx,0x4
 4d8:  49 09 de               or     r14,rbx
 4db:  48 c1 e2 08            shl    rdx,0x8
 4df:  48 09 d0               or     rax,rdx
 4e2:  0f b6 54 24 ee         movzx  edx,BYTE PTR [rsp-0x12]
 4e7:  48 c1 e2 04            shl    rdx,0x4
 4eb:  48 09 d0               or     rax,rdx
 4ee:  49 8d 1c 46            lea    rbx,[r14+rax*2]
 4f2:  4c 31 f3               xor    rbx,r14
 4f5:  0f b6 c7               movzx  eax,bh
 4f8:  c5 f9 92 d0            kmovb  k2,eax
 4fc:  48 89 d8               mov    rax,rbx
 4ff:  48 c1 e8 10            shr    rax,0x10
 503:  c5 f9 92 d8            kmovb  k3,eax
 507:  89 d8                  mov    eax,ebx
 509:  c1 e8 18               shr    eax,0x18
 50c:  c5 f9 92 e0            kmovb  k4,eax
 510:  48 89 d8               mov    rax,rbx
 513:  48 c1 e8 20            shr    rax,0x20
 517:  c5 f9 92 e8            kmovb  k5,eax
 51b:  48 89 d8               mov    rax,rbx
 51e:  48 c1 e8 04            shr    rax,0x4
 522:  c5 f9 92 f0            kmovb  k6,eax
 526:  48 89 d8               mov    rax,rbx
 529:  48 c1 e8 0c            shr    rax,0xc
 52d:  c5 f9 92 f8            kmovb  k7,eax
 531:  48 89 d8               mov    rax,rbx
 534:  48 c1 e8 14            shr    rax,0x14
 538:  c5 f9 92 cb            kmovb  k1,ebx
 53c:  62 71 a5 29 fb d9      vpsubq ymm11{k1},ymm11,ymm1
 542:  c5 f9 92 c8            kmovb  k1,eax
 546:  48 89 d8               mov    rax,rbx
 549:  48 c1 e8 1c            shr    rax,0x1c
 54d:  48 c1 eb 24            shr    rbx,0x24
 551:  62 71 b5 2a fb c9      vpsubq ymm9{k2},ymm9,ymm1
 557:  62 f1 c5 2b fb f9      vpsubq ymm7{k3},ymm7,ymm1
 55d:  62 f1 d5 2c fb e9      vpsubq ymm5{k4},ymm5,ymm1
 563:  62 f1 e5 2d fb d9      vpsubq ymm3{k5},ymm3,ymm1
 569:  62 71 ad 2e fb d1      vpsubq ymm10{k6},ymm10,ymm1
 56f:  62 71 bd 2f fb c1      vpsubq ymm8{k7},ymm8,ymm1
 575:  62 f1 cd 29 fb f1      vpsubq ymm6{k1},ymm6,ymm1
 57b:  c5 f9 92 d0            kmovb  k2,eax
 57f:  c5 f9 92 db            kmovb  k3,ebx
 583:  62 f1 dd 2a fb e1      vpsubq ymm4{k2},ymm4,ymm1
 589:  62 f1 ed 2b fb d1      vpsubq ymm2{k3},ymm2,ymm1
 58f:  c5 25 db d9            vpand  ymm11,ymm11,ymm1
 593:  c5 2d db d1            vpand  ymm10,ymm10,ymm1
 597:  c5 35 db c9            vpand  ymm9,ymm9,ymm1
 59b:  c5 3d db c1            vpand  ymm8,ymm8,ymm1
 59f:  c5 c5 db f9            vpand  ymm7,ymm7,ymm1
 5a3:  c5 cd db f1            vpand  ymm6,ymm6,ymm1
 5a7:  c5 d5 db e9            vpand  ymm5,ymm5,ymm1
 5ab:  c5 dd db e1            vpand  ymm4,ymm4,ymm1
 5af:  c5 e5 db d9            vpand  ymm3,ymm3,ymm1
 5b3:  c5 ed db d1            vpand  ymm2,ymm2,ymm1
 5b7:  62 71 fe 28 7f 1f      vmovdqu64 YMMWORD PTR [rdi],ymm11
 5bd:  62 71 fe 28 7f 57 01   vmovdqu64 YMMWORD PTR [rdi+0x20],ymm10
 5c4:  62 71 fe 28 7f 4f 02   vmovdqu64 YMMWORD PTR [rdi+0x40],ymm9
 5cb:  62 71 fe 28 7f 47 03   vmovdqu64 YMMWORD PTR [rdi+0x60],ymm8
 5d2:  62 f1 fe 28 7f 7f 04   vmovdqu64 YMMWORD PTR [rdi+0x80],ymm7
 5d9:  62 f1 fe 28 7f 77 05   vmovdqu64 YMMWORD PTR [rdi+0xa0],ymm6
 5e0:  62 f1 fe 28 7f 6f 06   vmovdqu64 YMMWORD PTR [rdi+0xc0],ymm5
 5e7:  62 f1 fe 28 7f 67 07   vmovdqu64 YMMWORD PTR [rdi+0xe0],ymm4
 5ee:  62 f1 fe 28 7f 5f 08   vmovdqu64 YMMWORD PTR [rdi+0x100],ymm3
 5f5:  62 f1 fe 28 7f 57 09   vmovdqu64 YMMWORD PTR [rdi+0x120],ymm2
 5fc:  c5 f8 77               vzeroupper 
 5ff:  48 8d 65 d8            lea    rsp,[rbp-0x28]
 603:  5b                     pop    rbx
 604:  41 5c                  pop    r12
 606:  41 5d                  pop    r13
 608:  41 5e                  pop    r14
 60a:  41 5f                  pop    r15
 60c:  5d                     pop    rbp
 60d:  c3                     ret    
 60e:  66 66 2e 0f 1f 84 00   data16 nop WORD PTR cs:[rax+rax*1+0x0]
 615:  00 00 00 00 
 619:  0f 1f 80 00 00 00 00   nop    DWORD PTR [rax+0x0]

0000000000000620 <k1_ifma256_ams52x40>:
 620:  f3 0f 1e fa            endbr64 
 624:  49 89 c8               mov    r8,rcx
 627:  48 89 d1               mov    rcx,rdx
 62a:  48 89 f2               mov    rdx,rsi
 62d:  e9 00 00 00 00         jmp    632 <k1_ifma256_ams52x40+0x12>
#endif
}

#if 0

/* pair of 52-bit digits occupys 13 bytes (the fact is using in implementation beloow) */
__INLINE Ipp64u getDig52(const Ipp8u* pStr, int strLen)
{
       0:	55                   	push   rbp
       1:	48 89 e5             	mov    rbp,rsp
       4:	48 89 7d e8          	mov    QWORD PTR [rbp-0x18],rdi
       8:	89 75 e4             	mov    DWORD PTR [rbp-0x1c],esi
   Ipp64u digit = 0;
       b:	48 c7 45 f8 00 00 00 	mov    QWORD PTR [rbp-0x8],0x0
      12:	00 
   for(; strLen>0; strLen--) {
      13:	eb 22                	jmp    37 <getDig52+0x37>
      digit <<= 8;
      15:	48 c1 65 f8 08       	shl    QWORD PTR [rbp-0x8],0x8
      digit += (Ipp64u)(pStr[strLen-1]);
      1a:	8b 45 e4             	mov    eax,DWORD PTR [rbp-0x1c]
      1d:	48 98                	cdqe   
      1f:	48 8d 50 ff          	lea    rdx,[rax-0x1]
      23:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
      27:	48 01 d0             	add    rax,rdx
      2a:	0f b6 00             	movzx  eax,BYTE PTR [rax]
      2d:	0f b6 c0             	movzx  eax,al
      30:	48 01 45 f8          	add    QWORD PTR [rbp-0x8],rax
   for(; strLen>0; strLen--) {
      34:	ff 4d e4             	dec    DWORD PTR [rbp-0x1c]
      37:	83 7d e4 00          	cmp    DWORD PTR [rbp-0x1c],0x0
      3b:	7f d8                	jg     15 <getDig52+0x15>
   }
   return digit;
      3d:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
}
      41:	5d                   	pop    rbp
      42:	c3                   	ret    
      43:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
      4a:	00 00 00 00 
      4e:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
      55:	00 00 00 00 
      59:	0f 1f 80 00 00 00 00 	nop    DWORD PTR [rax+0x0]

0000000000000060 <regular_dig52>:

/* regular => redundant conversion */
static void regular_dig52(Ipp64u* out, int outLen /* in qwords */, const Ipp64u* in, int inBitSize)
{
      60:	f3 0f 1e fa          	endbr64 
      64:	55                   	push   rbp
      65:	48 89 e5             	mov    rbp,rsp
      68:	48 83 ec 28          	sub    rsp,0x28
      6c:	48 89 7d e8          	mov    QWORD PTR [rbp-0x18],rdi
      70:	89 75 e4             	mov    DWORD PTR [rbp-0x1c],esi
      73:	48 89 55 d8          	mov    QWORD PTR [rbp-0x28],rdx
      77:	89 4d e0             	mov    DWORD PTR [rbp-0x20],ecx
   Ipp8u* inStr = (Ipp8u*)in;
      7a:	48 8b 45 d8          	mov    rax,QWORD PTR [rbp-0x28]
      7e:	48 89 45 f0          	mov    QWORD PTR [rbp-0x10],rax

   for(; inBitSize>=(2*EXP_DIGIT_SIZE_AVX512); inBitSize-=(2*EXP_DIGIT_SIZE_AVX512), out+=2) {
      82:	eb 58                	jmp    dc <regular_dig52+0x7c>
      out[0] = (*(Ipp64u*)inStr) & EXP_DIGIT_MASK_AVX512;
      84:	48 8b 45 f0          	mov    rax,QWORD PTR [rbp-0x10]
      88:	48 8b 10             	mov    rdx,QWORD PTR [rax]
      8b:	48 b8 ff ff ff ff ff 	movabs rax,0xfffffffffffff
      92:	ff 0f 00 
      95:	48 21 c2             	and    rdx,rax
      98:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
      9c:	48 89 10             	mov    QWORD PTR [rax],rdx
      inStr += 6;
      9f:	48 83 45 f0 06       	add    QWORD PTR [rbp-0x10],0x6
      out[1] = ((*(Ipp64u*)inStr) >> 4) & EXP_DIGIT_MASK_AVX512;
      a4:	48 8b 45 f0          	mov    rax,QWORD PTR [rbp-0x10]
      a8:	48 8b 00             	mov    rax,QWORD PTR [rax]
      ab:	48 c1 e8 04          	shr    rax,0x4
      af:	48 89 c1             	mov    rcx,rax
      b2:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
      b6:	48 83 c0 08          	add    rax,0x8
      ba:	48 ba ff ff ff ff ff 	movabs rdx,0xfffffffffffff
      c1:	ff 0f 00 
      c4:	48 21 ca             	and    rdx,rcx
      c7:	48 89 10             	mov    QWORD PTR [rax],rdx
      inStr += 7;
      ca:	48 83 45 f0 07       	add    QWORD PTR [rbp-0x10],0x7
      outLen -= 2;
      cf:	83 6d e4 02          	sub    DWORD PTR [rbp-0x1c],0x2
   for(; inBitSize>=(2*EXP_DIGIT_SIZE_AVX512); inBitSize-=(2*EXP_DIGIT_SIZE_AVX512), out+=2) {
      d3:	83 6d e0 68          	sub    DWORD PTR [rbp-0x20],0x68
      d7:	48 83 45 e8 10       	add    QWORD PTR [rbp-0x18],0x10
      dc:	83 7d e0 67          	cmp    DWORD PTR [rbp-0x20],0x67
      e0:	7f a2                	jg     84 <regular_dig52+0x24>
   }
   if(inBitSize>EXP_DIGIT_SIZE_AVX512) {
      e2:	83 7d e0 34          	cmp    DWORD PTR [rbp-0x20],0x34
      e6:	7e 71                	jle    159 <regular_dig52+0xf9>
      Ipp64u digit = getDig52(inStr, 7);
      e8:	48 8b 45 f0          	mov    rax,QWORD PTR [rbp-0x10]
      ec:	be 07 00 00 00       	mov    esi,0x7
      f1:	48 89 c7             	mov    rdi,rax
      f4:	e8 07 ff ff ff       	call   0 <getDig52>
      f9:	48 89 45 f8          	mov    QWORD PTR [rbp-0x8],rax
      out[0] = digit & EXP_DIGIT_MASK_AVX512;
      fd:	48 b8 ff ff ff ff ff 	movabs rax,0xfffffffffffff
     104:	ff 0f 00 
     107:	48 23 45 f8          	and    rax,QWORD PTR [rbp-0x8]
     10b:	48 89 c2             	mov    rdx,rax
     10e:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
     112:	48 89 10             	mov    QWORD PTR [rax],rdx
      inStr += 6;
     115:	48 83 45 f0 06       	add    QWORD PTR [rbp-0x10],0x6
      inBitSize -= EXP_DIGIT_SIZE_AVX512;
     11a:	83 6d e0 34          	sub    DWORD PTR [rbp-0x20],0x34
      digit = getDig52(inStr, BITS2WORD8_SIZE(inBitSize));
     11e:	8b 45 e0             	mov    eax,DWORD PTR [rbp-0x20]
     121:	83 c0 07             	add    eax,0x7
     124:	c1 f8 03             	sar    eax,0x3
     127:	89 c2                	mov    edx,eax
     129:	48 8b 45 f0          	mov    rax,QWORD PTR [rbp-0x10]
     12d:	89 d6                	mov    esi,edx
     12f:	48 89 c7             	mov    rdi,rax
     132:	e8 c9 fe ff ff       	call   0 <getDig52>
     137:	48 89 45 f8          	mov    QWORD PTR [rbp-0x8],rax
      out[1] = digit>>4;
     13b:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
     13f:	48 83 c0 08          	add    rax,0x8
     143:	48 8b 55 f8          	mov    rdx,QWORD PTR [rbp-0x8]
     147:	48 c1 ea 04          	shr    rdx,0x4
     14b:	48 89 10             	mov    QWORD PTR [rax],rdx
      out += 2;
     14e:	48 83 45 e8 10       	add    QWORD PTR [rbp-0x18],0x10
      outLen -= 2;
     153:	83 6d e4 02          	sub    DWORD PTR [rbp-0x1c],0x2
     157:	eb 43                	jmp    19c <regular_dig52+0x13c>
   }
   else if(inBitSize>0) {
     159:	83 7d e0 00          	cmp    DWORD PTR [rbp-0x20],0x0
     15d:	7e 3d                	jle    19c <regular_dig52+0x13c>
      out[0] = getDig52(inStr, BITS2WORD8_SIZE(inBitSize));
     15f:	8b 45 e0             	mov    eax,DWORD PTR [rbp-0x20]
     162:	83 c0 07             	add    eax,0x7
     165:	c1 f8 03             	sar    eax,0x3
     168:	89 c2                	mov    edx,eax
     16a:	48 8b 45 f0          	mov    rax,QWORD PTR [rbp-0x10]
     16e:	89 d6                	mov    esi,edx
     170:	48 89 c7             	mov    rdi,rax
     173:	e8 88 fe ff ff       	call   0 <getDig52>
     178:	48 8b 55 e8          	mov    rdx,QWORD PTR [rbp-0x18]
     17c:	48 89 02             	mov    QWORD PTR [rdx],rax
      out++;
     17f:	48 83 45 e8 08       	add    QWORD PTR [rbp-0x18],0x8
      outLen--;
     184:	ff 4d e4             	dec    DWORD PTR [rbp-0x1c]
   }
   for(; outLen>0; outLen--,out++) out[0] = 0;
     187:	eb 13                	jmp    19c <regular_dig52+0x13c>
     189:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
     18d:	48 c7 00 00 00 00 00 	mov    QWORD PTR [rax],0x0
     194:	ff 4d e4             	dec    DWORD PTR [rbp-0x1c]
     197:	48 83 45 e8 08       	add    QWORD PTR [rbp-0x18],0x8
     19c:	83 7d e4 00          	cmp    DWORD PTR [rbp-0x1c],0x0
     1a0:	7f e7                	jg     189 <regular_dig52+0x129>
}
     1a2:	90                   	nop
     1a3:	90                   	nop
     1a4:	c9                   	leave  
     1a5:	c3                   	ret    
     1a6:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     1ad:	00 00 00 00 
     1b1:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     1b8:	00 00 00 00 
     1bc:	0f 1f 40 00          	nop    DWORD PTR [rax+0x0]

00000000000001c0 <putDig52>:
/*
   converts "redundant" (base = 2^DIGIT_SIZE) representation
   into regular (base = 2^64)
*/
__INLINE void putDig52(Ipp8u* pStr, int strLen, Ipp64u digit)
{
     1c0:	55                   	push   rbp
     1c1:	48 89 e5             	mov    rbp,rsp
     1c4:	48 89 7d f8          	mov    QWORD PTR [rbp-0x8],rdi
     1c8:	89 75 f4             	mov    DWORD PTR [rbp-0xc],esi
     1cb:	48 89 55 e8          	mov    QWORD PTR [rbp-0x18],rdx
   for(; strLen>0; strLen--) {
     1cf:	eb 1a                	jmp    1eb <putDig52+0x2b>
      *pStr++ = (Ipp8u)(digit&0xFF);
     1d1:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     1d5:	48 8d 50 01          	lea    rdx,[rax+0x1]
     1d9:	48 89 55 f8          	mov    QWORD PTR [rbp-0x8],rdx
     1dd:	48 8b 55 e8          	mov    rdx,QWORD PTR [rbp-0x18]
     1e1:	88 10                	mov    BYTE PTR [rax],dl
      digit >>= 8;
     1e3:	48 c1 6d e8 08       	shr    QWORD PTR [rbp-0x18],0x8
   for(; strLen>0; strLen--) {
     1e8:	ff 4d f4             	dec    DWORD PTR [rbp-0xc]
     1eb:	83 7d f4 00          	cmp    DWORD PTR [rbp-0xc],0x0
     1ef:	7f e0                	jg     1d1 <putDig52+0x11>
   }
}
     1f1:	90                   	nop
     1f2:	90                   	nop
     1f3:	5d                   	pop    rbp
     1f4:	c3                   	ret    
     1f5:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     1fc:	00 00 00 00 

0000000000000200 <dig52_regular>:

static void dig52_regular(Ipp64u* out, const Ipp64u* in, int outBitSize)
{
     200:	f3 0f 1e fa          	endbr64 
     204:	55                   	push   rbp
     205:	48 89 e5             	mov    rbp,rsp
     208:	48 83 ec 28          	sub    rsp,0x28
     20c:	48 89 7d e8          	mov    QWORD PTR [rbp-0x18],rdi
     210:	48 89 75 e0          	mov    QWORD PTR [rbp-0x20],rsi
     214:	89 55 dc             	mov    DWORD PTR [rbp-0x24],edx
   int i;
   int outLen = BITS2WORD64_SIZE(outBitSize);
     217:	8b 45 dc             	mov    eax,DWORD PTR [rbp-0x24]
     21a:	83 c0 3f             	add    eax,0x3f
     21d:	c1 f8 06             	sar    eax,0x6
     220:	89 45 f4             	mov    DWORD PTR [rbp-0xc],eax
   for(i=0; i<outLen; i++) out[i] = 0;
     223:	c7 45 f0 00 00 00 00 	mov    DWORD PTR [rbp-0x10],0x0
     22a:	eb 1e                	jmp    24a <dig52_regular+0x4a>
     22c:	8b 45 f0             	mov    eax,DWORD PTR [rbp-0x10]
     22f:	48 98                	cdqe   
     231:	48 8d 14 c5 00 00 00 	lea    rdx,[rax*8+0x0]
     238:	00 
     239:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
     23d:	48 01 d0             	add    rax,rdx
     240:	48 c7 00 00 00 00 00 	mov    QWORD PTR [rax],0x0
     247:	ff 45 f0             	inc    DWORD PTR [rbp-0x10]
     24a:	8b 45 f0             	mov    eax,DWORD PTR [rbp-0x10]
     24d:	3b 45 f4             	cmp    eax,DWORD PTR [rbp-0xc]
     250:	7c da                	jl     22c <dig52_regular+0x2c>

   {
      Ipp8u* outStr = (Ipp8u*)out;
     252:	48 8b 45 e8          	mov    rax,QWORD PTR [rbp-0x18]
     256:	48 89 45 f8          	mov    QWORD PTR [rbp-0x8],rax
      for(; outBitSize>=(2*EXP_DIGIT_SIZE_AVX512); outBitSize-=(2*EXP_DIGIT_SIZE_AVX512), in+=2) {
     25a:	eb 41                	jmp    29d <dig52_regular+0x9d>
         (*(Ipp64u*)outStr) = in[0];
     25c:	48 8b 45 e0          	mov    rax,QWORD PTR [rbp-0x20]
     260:	48 8b 10             	mov    rdx,QWORD PTR [rax]
     263:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     267:	48 89 10             	mov    QWORD PTR [rax],rdx
         outStr+=6;
     26a:	48 83 45 f8 06       	add    QWORD PTR [rbp-0x8],0x6
         (*(Ipp64u*)outStr) ^= in[1] << 4;
     26f:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     273:	48 8b 00             	mov    rax,QWORD PTR [rax]
     276:	48 8b 55 e0          	mov    rdx,QWORD PTR [rbp-0x20]
     27a:	48 83 c2 08          	add    rdx,0x8
     27e:	48 8b 12             	mov    rdx,QWORD PTR [rdx]
     281:	48 c1 e2 04          	shl    rdx,0x4
     285:	48 31 c2             	xor    rdx,rax
     288:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     28c:	48 89 10             	mov    QWORD PTR [rax],rdx
         outStr+=7;
     28f:	48 83 45 f8 07       	add    QWORD PTR [rbp-0x8],0x7
      for(; outBitSize>=(2*EXP_DIGIT_SIZE_AVX512); outBitSize-=(2*EXP_DIGIT_SIZE_AVX512), in+=2) {
     294:	83 6d dc 68          	sub    DWORD PTR [rbp-0x24],0x68
     298:	48 83 45 e0 10       	add    QWORD PTR [rbp-0x20],0x10
     29d:	83 7d dc 67          	cmp    DWORD PTR [rbp-0x24],0x67
     2a1:	7f b9                	jg     25c <dig52_regular+0x5c>
      }
      if(outBitSize>EXP_DIGIT_SIZE_AVX512) {
     2a3:	83 7d dc 34          	cmp    DWORD PTR [rbp-0x24],0x34
     2a7:	7e 5c                	jle    305 <dig52_regular+0x105>
         putDig52(outStr, 7, in[0]);
     2a9:	48 8b 45 e0          	mov    rax,QWORD PTR [rbp-0x20]
     2ad:	48 8b 10             	mov    rdx,QWORD PTR [rax]
     2b0:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     2b4:	be 07 00 00 00       	mov    esi,0x7
     2b9:	48 89 c7             	mov    rdi,rax
     2bc:	e8 ff fe ff ff       	call   1c0 <putDig52>
         outStr+=6;
     2c1:	48 83 45 f8 06       	add    QWORD PTR [rbp-0x8],0x6
         outBitSize -= EXP_DIGIT_SIZE_AVX512;
     2c6:	83 6d dc 34          	sub    DWORD PTR [rbp-0x24],0x34
         putDig52(outStr, BITS2WORD8_SIZE(outBitSize), (in[1]<<4 | in[0]>>48));
     2ca:	48 8b 45 e0          	mov    rax,QWORD PTR [rbp-0x20]
     2ce:	48 83 c0 08          	add    rax,0x8
     2d2:	48 8b 00             	mov    rax,QWORD PTR [rax]
     2d5:	48 c1 e0 04          	shl    rax,0x4
     2d9:	48 89 c2             	mov    rdx,rax
     2dc:	48 8b 45 e0          	mov    rax,QWORD PTR [rbp-0x20]
     2e0:	48 8b 00             	mov    rax,QWORD PTR [rax]
     2e3:	48 c1 e8 30          	shr    rax,0x30
     2e7:	48 09 c2             	or     rdx,rax
     2ea:	8b 45 dc             	mov    eax,DWORD PTR [rbp-0x24]
     2ed:	83 c0 07             	add    eax,0x7
     2f0:	c1 f8 03             	sar    eax,0x3
     2f3:	89 c1                	mov    ecx,eax
     2f5:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     2f9:	89 ce                	mov    esi,ecx
     2fb:	48 89 c7             	mov    rdi,rax
     2fe:	e8 bd fe ff ff       	call   1c0 <putDig52>
      }
      else if(outBitSize) {
         putDig52(outStr, BITS2WORD8_SIZE(outBitSize), in[0]);
      }
   }
}
     303:	eb 26                	jmp    32b <dig52_regular+0x12b>
      else if(outBitSize) {
     305:	83 7d dc 00          	cmp    DWORD PTR [rbp-0x24],0x0
     309:	74 20                	je     32b <dig52_regular+0x12b>
         putDig52(outStr, BITS2WORD8_SIZE(outBitSize), in[0]);
     30b:	48 8b 45 e0          	mov    rax,QWORD PTR [rbp-0x20]
     30f:	48 8b 10             	mov    rdx,QWORD PTR [rax]
     312:	8b 45 dc             	mov    eax,DWORD PTR [rbp-0x24]
     315:	83 c0 07             	add    eax,0x7
     318:	c1 f8 03             	sar    eax,0x3
     31b:	89 c1                	mov    ecx,eax
     31d:	48 8b 45 f8          	mov    rax,QWORD PTR [rbp-0x8]
     321:	89 ce                	mov    esi,ecx
     323:	48 89 c7             	mov    rdi,rax
     326:	e8 95 fe ff ff       	call   1c0 <putDig52>
}
     32b:	90                   	nop
     32c:	c9                   	leave  
     32d:	c3                   	ret    
     32e:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     335:	00 00 00 00 
     339:	0f 1f 80 00 00 00 00 	nop    DWORD PTR [rax+0x0]

0000000000000340 <loadu64>:
// #if (SIMD_LEN == 256)
  SIMD_TYPE(256)
  #define SIMD_BYTES  (SIMD_LEN/8)
  #define SIMD_QWORDS (SIMD_LEN/64)

  __INLINE U64 loadu64(const void *p) {
     340:	55                   	push   rbp
     341:	48 89 e5             	mov    rbp,rsp
     344:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     348:	48 89 7c 24 e8       	mov    QWORD PTR [rsp-0x18],rdi
     34d:	48 8b 44 24 e8       	mov    rax,QWORD PTR [rsp-0x18]
     352:	48 89 44 24 f8       	mov    QWORD PTR [rsp-0x8],rax
}

extern __inline __m256i __attribute__((__gnu_inline__, __always_inline__, __artificial__))
_mm256_loadu_si256 (__m256i_u const *__P)
{
  return *__P;
     357:	48 8b 44 24 f8       	mov    rax,QWORD PTR [rsp-0x8]
     35c:	62 f1 fe 28 6f 00    	vmovdqu64 ymm0,YMMWORD PTR [rax]
    return _mm256_loadu_si256((U64*)p);
  }
     362:	c9                   	leave  
     363:	c3                   	ret    
     364:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     36b:	00 00 00 00 
     36f:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     376:	00 00 00 00 
     37a:	66 0f 1f 44 00 00    	nop    WORD PTR [rax+rax*1+0x0]

0000000000000380 <storeu64>:

  __INLINE void storeu64(const void *p, U64 v) {
     380:	55                   	push   rbp
     381:	48 89 e5             	mov    rbp,rsp
     384:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     388:	48 89 7c 24 c8       	mov    QWORD PTR [rsp-0x38],rdi
     38d:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x60],ymm0
     394:	fd 
     395:	48 8b 44 24 c8       	mov    rax,QWORD PTR [rsp-0x38]
     39a:	48 89 44 24 d8       	mov    QWORD PTR [rsp-0x28],rax
     39f:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x60]
     3a6:	fd 
     3a7:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x20],ymm0
     3ae:	ff 
}

extern __inline void __attribute__((__gnu_inline__, __always_inline__, __artificial__))
_mm256_storeu_si256 (__m256i_u *__P, __m256i __A)
{
  *__P = __A;
     3af:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x20]
     3b6:	ff 
     3b7:	48 8b 44 24 d8       	mov    rax,QWORD PTR [rsp-0x28]
     3bc:	62 f1 fe 28 7f 00    	vmovdqu64 YMMWORD PTR [rax],ymm0
}
     3c2:	90                   	nop
    _mm256_storeu_si256((U64*)p, v);
  }
     3c3:	90                   	nop
     3c4:	c9                   	leave  
     3c5:	c3                   	ret    
     3c6:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     3cd:	00 00 00 00 
     3d1:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     3d8:	00 00 00 00 
     3dc:	0f 1f 40 00          	nop    DWORD PTR [rax+0x0]

00000000000003e0 <fma52lo>:

  #define set64 _mm256_set1_epi64x

  #ifdef __GNUC__
      static U64 fma52lo(U64 a, U64 b, U64 c)
      {
     3e0:	f3 0f 1e fa          	endbr64 
     3e4:	55                   	push   rbp
     3e5:	48 89 e5             	mov    rbp,rsp
     3e8:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     3ec:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x20],ymm0
     3f3:	ff 
     3f4:	62 f1 fd 28 7f 4c 24 	vmovdqa64 YMMWORD PTR [rsp-0x40],ymm1
     3fb:	fe 
     3fc:	62 f1 fd 28 7f 54 24 	vmovdqa64 YMMWORD PTR [rsp-0x60],ymm2
     403:	fd 
        __asm__ ( "vpmadd52luq %2, %1, %0" : "+x" (a): "x" (b), "x" (c) );
     404:	62 f1 fd 28 6f 4c 24 	vmovdqa64 ymm1,YMMWORD PTR [rsp-0x40]
     40b:	fe 
     40c:	62 f1 fd 28 6f 54 24 	vmovdqa64 ymm2,YMMWORD PTR [rsp-0x60]
     413:	fd 
     414:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x20]
     41b:	ff 
     41c:	62 f2 f5 28 b4 c2    	vpmadd52luq ymm0,ymm1,ymm2
     422:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x20],ymm0
     429:	ff 
        return a;
     42a:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x20]
     431:	ff 
      }
     432:	c9                   	leave  
     433:	c3                   	ret    
     434:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     43b:	00 00 00 00 
     43f:	90                   	nop

0000000000000440 <fma52hi>:

      static U64 fma52hi(U64 a, U64 b, U64 c)
      {
     440:	f3 0f 1e fa          	endbr64 
     444:	55                   	push   rbp
     445:	48 89 e5             	mov    rbp,rsp
     448:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     44c:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x20],ymm0
     453:	ff 
     454:	62 f1 fd 28 7f 4c 24 	vmovdqa64 YMMWORD PTR [rsp-0x40],ymm1
     45b:	fe 
     45c:	62 f1 fd 28 7f 54 24 	vmovdqa64 YMMWORD PTR [rsp-0x60],ymm2
     463:	fd 
        __asm__ ( "vpmadd52huq %2, %1, %0" : "+x" (a): "x" (b), "x" (c) );
     464:	62 f1 fd 28 6f 4c 24 	vmovdqa64 ymm1,YMMWORD PTR [rsp-0x40]
     46b:	fe 
     46c:	62 f1 fd 28 6f 54 24 	vmovdqa64 ymm2,YMMWORD PTR [rsp-0x60]
     473:	fd 
     474:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x20]
     47b:	ff 
     47c:	62 f2 f5 28 b5 c2    	vpmadd52huq ymm0,ymm1,ymm2
     482:	62 f1 fd 28 7f 44 24 	vmovdqa64 YMMWORD PTR [rsp-0x20],ymm0
     489:	ff 
        return a;
     48a:	62 f1 fd 28 6f 44 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x20]
     491:	ff 
      }
     492:	c9                   	leave  
     493:	c3                   	ret    
     494:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     49b:	00 00 00 00 
     49f:	90                   	nop

00000000000004a0 <add64>:

  #define fma52lo_mem(r, a, b, c, o) _mm_madd52lo_epu64_(r, a, b, c, o)
  #define fma52hi_mem(r, a, b, c, o) _mm_madd52hi_epu64_(r, a, b, c, o)

  __INLINE U64 add64(U64 a, U64 b)
  {
     4a0:	55                   	push   rbp
     4a1:	48 89 e5             	mov    rbp,rsp
     4a4:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     4a8:	48 83 ec 08          	sub    rsp,0x8
     4ac:	62 f1 fd 28 7f 84 24 	vmovdqa64 YMMWORD PTR [rsp-0x58],ymm0
     4b3:	a8 ff ff ff 
     4b7:	62 f1 fd 28 7f 8c 24 	vmovdqa64 YMMWORD PTR [rsp-0x78],ymm1
     4be:	88 ff ff ff 
     4c2:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x58]
     4c9:	a8 ff ff ff 
     4cd:	62 f1 fd 28 7f 84 24 	vmovdqa64 YMMWORD PTR [rsp-0x38],ymm0
     4d4:	c8 ff ff ff 
     4d8:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x78]
     4df:	88 ff ff ff 
     4e3:	62 f1 fd 28 7f 84 24 	vmovdqa64 YMMWORD PTR [rsp-0x18],ymm0
     4ea:	e8 ff ff ff 

extern __inline __m256i
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_add_epi64 (__m256i __A, __m256i __B)
{
  return (__m256i) ((__v4du)__A + (__v4du)__B);
     4ee:	62 f1 fd 28 6f 8c 24 	vmovdqa64 ymm1,YMMWORD PTR [rsp-0x38]
     4f5:	c8 ff ff ff 
     4f9:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x18]
     500:	e8 ff ff ff 
     504:	c5 f5 d4 c0          	vpaddq ymm0,ymm1,ymm0
    return _mm256_add_epi64(a, b);
  }
     508:	c9                   	leave  
     509:	c3                   	ret    
     50a:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     511:	00 00 00 00 
     515:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     51c:	00 00 00 00 

0000000000000520 <get_zero64>:
}

extern __inline __m256i __attribute__((__gnu_inline__, __always_inline__, __artificial__))
_mm256_setzero_si256 (void)
{
  return __extension__ (__m256i)(__v4di){ 0, 0, 0, 0 };
     520:	c5 f9 ef c0          	vpxor  xmm0,xmm0,xmm0
  }

  __INLINE U64 get_zero64()
  {
    return _mm256_setzero_si256();
  }
     524:	c3                   	ret    
     525:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     52c:	00 00 00 00 
     530:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     537:	00 00 00 00 
     53b:	0f 1f 44 00 00       	nop    DWORD PTR [rax+rax*1+0x0]

0000000000000540 <get64>:
  }

  #define or64 _mm256_or_si256
  #define xor64 _mm256_xor_si256

  static Ipp64u get64(U64 v, int idx) {
     540:	f3 0f 1e fa          	endbr64 
     544:	55                   	push   rbp
     545:	48 89 e5             	mov    rbp,rsp
     548:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     54c:	48 83 ec 08          	sub    rsp,0x8
     550:	62 f1 fd 28 7f 84 24 	vmovdqa64 YMMWORD PTR [rsp-0x78],ymm0
     557:	88 ff ff ff 
     55b:	89 7c 24 b4          	mov    DWORD PTR [rsp-0x4c],edi
    long long int res;
    switch (idx) {
     55f:	83 7c 24 b4 03       	cmp    DWORD PTR [rsp-0x4c],0x3
     564:	74 78                	je     5de <get64+0x9e>
     566:	83 7c 24 b4 03       	cmp    DWORD PTR [rsp-0x4c],0x3
     56b:	0f 8f 9c 00 00 00    	jg     60d <get64+0xcd>
     571:	83 7c 24 b4 01       	cmp    DWORD PTR [rsp-0x4c],0x1
     576:	74 0c                	je     584 <get64+0x44>
     578:	83 7c 24 b4 02       	cmp    DWORD PTR [rsp-0x4c],0x2
     57d:	74 31                	je     5b0 <get64+0x70>
     57f:	e9 89 00 00 00       	jmp    60d <get64+0xcd>
      case 1: res = _mm256_extract_epi64(v, 1); break;
     584:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x78]
     58b:	88 ff ff ff 
     58f:	c5 f8 29 44 24 e8    	vmovaps XMMWORD PTR [rsp-0x18],xmm0
     595:	62 f1 fd 08 6f 84 24 	vmovdqa64 xmm0,XMMWORD PTR [rsp-0x18]
     59c:	e8 ff ff ff 
     5a0:	c4 e3 f9 16 c0 01    	vpextrq rax,xmm0,0x1
     5a6:	48 89 44 24 c0       	mov    QWORD PTR [rsp-0x40],rax
     5ab:	e9 83 00 00 00       	jmp    633 <get64+0xf3>
      case 2: res = _mm256_extract_epi64(v, 2); break;
     5b0:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x78]
     5b7:	88 ff ff ff 
     5bb:	c4 e3 7d 39 c0 01    	vextracti128 xmm0,ymm0,0x1
     5c1:	c5 f8 29 44 24 d8    	vmovaps XMMWORD PTR [rsp-0x28],xmm0
     5c7:	62 f1 fd 08 6f 84 24 	vmovdqa64 xmm0,XMMWORD PTR [rsp-0x28]
     5ce:	d8 ff ff ff 
     5d2:	c4 e1 f9 7e c0       	vmovq  rax,xmm0
     5d7:	48 89 44 24 c0       	mov    QWORD PTR [rsp-0x40],rax
     5dc:	eb 55                	jmp    633 <get64+0xf3>
      case 3: res = _mm256_extract_epi64(v, 3); break;
     5de:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x78]
     5e5:	88 ff ff ff 
     5e9:	c4 e3 7d 39 c0 01    	vextracti128 xmm0,ymm0,0x1
     5ef:	c5 f8 29 44 24 c8    	vmovaps XMMWORD PTR [rsp-0x38],xmm0
     5f5:	62 f1 fd 08 6f 84 24 	vmovdqa64 xmm0,XMMWORD PTR [rsp-0x38]
     5fc:	c8 ff ff ff 
     600:	c4 e3 f9 16 c0 01    	vpextrq rax,xmm0,0x1
     606:	48 89 44 24 c0       	mov    QWORD PTR [rsp-0x40],rax
     60b:	eb 26                	jmp    633 <get64+0xf3>
      default: res = _mm256_extract_epi64(v, 0);
     60d:	62 f1 fd 28 6f 84 24 	vmovdqa64 ymm0,YMMWORD PTR [rsp-0x78]
     614:	88 ff ff ff 
     618:	c5 f8 29 44 24 f8    	vmovaps XMMWORD PTR [rsp-0x8],xmm0
     61e:	62 f1 fd 08 6f 84 24 	vmovdqa64 xmm0,XMMWORD PTR [rsp-0x8]
     625:	f8 ff ff ff 
     629:	c4 e1 f9 7e c0       	vmovq  rax,xmm0
     62e:	48 89 44 24 c0       	mov    QWORD PTR [rsp-0x40],rax
    }
    return (Ipp64u)res;
     633:	48 8b 44 24 c0       	mov    rax,QWORD PTR [rsp-0x40]
  }
     638:	c9                   	leave  
     639:	c3                   	ret    
     63a:	66 0f 1f 44 00 00    	nop    WORD PTR [rax+rax*1+0x0]

0000000000000640 <extract_multiplier>:
#define AMS ifma256_ams52x20

__INLINE void extract_multiplier(Ipp64u *red_Y,
                           const Ipp64u red_table[1U << EXP_WIN_SIZE][LEN52],
                                 int red_table_idx)
{
     640:	4c 8d 54 24 08       	lea    r10,[rsp+0x8]
     645:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
     649:	41 ff 72 f8          	push   QWORD PTR [r10-0x8]
     64d:	55                   	push   rbp
     64e:	48 89 e5             	mov    rbp,rsp
     651:	41 52                	push   r10
     653:	48 81 ec 68 03 00 00 	sub    rsp,0x368
     65a:	48 89 bd b8 fc ff ff 	mov    QWORD PTR [rbp-0x348],rdi
     661:	48 89 b5 b0 fc ff ff 	mov    QWORD PTR [rbp-0x350],rsi
     668:	89 95 ac fc ff ff    	mov    DWORD PTR [rbp-0x354],edx
    U64 idx = set64(red_table_idx);
     66e:	8b 85 ac fc ff ff    	mov    eax,DWORD PTR [rbp-0x354]
     674:	48 98                	cdqe   
     676:	48 89 85 e0 fc ff ff 	mov    QWORD PTR [rbp-0x320],rax
}

extern __inline __m256i __attribute__((__gnu_inline__, __always_inline__, __artificial__))
_mm256_set1_epi64x (long long __A)
{
  return __extension__ (__m256i)(__v4di){ __A, __A, __A, __A };
     67d:	c4 e2 7d 59 85 e0 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x320]
     684:	ff ff 
     686:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x250],ymm0
     68d:	fd ff ff 
     690:	48 c7 85 d8 fc ff ff 	mov    QWORD PTR [rbp-0x328],0x0
     697:	00 00 00 00 
     69b:	c4 e2 7d 59 85 d8 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x328]
     6a2:	ff ff 
    U64 cur_idx = set64(0);
     6a4:	62 f1 fd 28 7f 85 f0 	vmovdqa64 YMMWORD PTR [rbp-0x310],ymm0
     6ab:	fc ff ff 

    U64 t0, t1, t2, t3, t4;
    t0 = t1 = t2 = t3 = t4 = get_zero64();
     6ae:	b8 00 00 00 00       	mov    eax,0x0
     6b3:	e8 68 fe ff ff       	call   520 <get_zero64>
     6b8:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x270],ymm0
     6bf:	fd ff ff 
     6c2:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x270]
     6c9:	fd ff ff 
     6cc:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x290],ymm0
     6d3:	fd ff ff 
     6d6:	62 f1 fd 28 6f 85 70 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x290]
     6dd:	fd ff ff 
     6e0:	62 f1 fd 28 7f 85 50 	vmovdqa64 YMMWORD PTR [rbp-0x2b0],ymm0
     6e7:	fd ff ff 
     6ea:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2b0]
     6f1:	fd ff ff 
     6f4:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0x2d0],ymm0
     6fb:	fd ff ff 
     6fe:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2d0]
     705:	fd ff ff 
     708:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x2f0],ymm0
     70f:	fd ff ff 

    for (int t = 0; t < (1U << EXP_WIN_SIZE); ++t, cur_idx = add64(cur_idx, set64(1))) {
     712:	c7 85 d4 fc ff ff 00 	mov    DWORD PTR [rbp-0x32c],0x0
     719:	00 00 00 
     71c:	e9 8b 03 00 00       	jmp    aac <extract_multiplier+0x46c>
        __mmask8 m = _mm256_cmp_epi64_mask(idx, cur_idx, _MM_CMPINT_EQ);
     721:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
     728:	fd ff ff 
     72b:	62 f1 fd 28 6f 8d f0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x310]
     732:	fc ff ff 
     735:	c5 f5 46 c9          	kxnorb k1,k1,k1
     739:	62 f3 fd 29 1f c1 00 	vpcmpeqq k0{k1},ymm0,ymm1
     740:	c5 f9 91 85 ce fc ff 	kmovb  BYTE PTR [rbp-0x332],k0
     747:	ff 

        t0 = _mm256_mask_xor_epi64(t0, m, t0, loadu64(&red_table[t][4*0]));
     748:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     74e:	48 63 d0             	movsxd rdx,eax
     751:	48 89 d0             	mov    rax,rdx
     754:	48 c1 e0 02          	shl    rax,0x2
     758:	48 01 d0             	add    rax,rdx
     75b:	48 c1 e0 05          	shl    rax,0x5
     75f:	48 89 c2             	mov    rdx,rax
     762:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
     769:	48 01 d0             	add    rax,rdx
     76c:	48 89 c7             	mov    rdi,rax
     76f:	e8 cc fb ff ff       	call   340 <loadu64>
     774:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
     77b:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2f0]
     782:	fd ff ff 
     785:	62 f1 fd 28 7f 8d 50 	vmovdqa64 YMMWORD PTR [rbp-0xb0],ymm1
     78c:	ff ff ff 
     78f:	88 85 d3 fc ff ff    	mov    BYTE PTR [rbp-0x32d],al
     795:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2f0]
     79c:	fd ff ff 
     79f:	62 f1 fd 28 7f 8d 70 	vmovdqa64 YMMWORD PTR [rbp-0x90],ymm1
     7a6:	ff ff ff 
     7a9:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x70],ymm0
     7b0:	ff ff ff 
extern __inline __m256i
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_xor_epi64 (__m256i __W, __mmask8 __U, __m256i __A,
		       __m256i __B)
{
  return (__m256i) __builtin_ia32_pxorq256_mask ((__v4di) __A,
     7b3:	0f b6 85 d3 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32d]
     7ba:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x70]
     7c1:	ff ff ff 
     7c4:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0xb0]
     7cb:	ff ff ff 
     7ce:	c5 f9 92 d0          	kmovb  k2,eax
     7d2:	62 f1 f5 2a ef 85 70 	vpxorq ymm0{k2},ymm1,YMMWORD PTR [rbp-0x90]
     7d9:	ff ff ff 
     7dc:	90                   	nop
     7dd:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x2f0],ymm0
     7e4:	fd ff ff 
        t1 = _mm256_mask_xor_epi64(t1, m, t1, loadu64(&red_table[t][4*1]));
     7e7:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     7ed:	48 63 d0             	movsxd rdx,eax
     7f0:	48 89 d0             	mov    rax,rdx
     7f3:	48 c1 e0 02          	shl    rax,0x2
     7f7:	48 01 d0             	add    rax,rdx
     7fa:	48 c1 e0 05          	shl    rax,0x5
     7fe:	48 89 c2             	mov    rdx,rax
     801:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
     808:	48 01 d0             	add    rax,rdx
     80b:	48 83 c0 20          	add    rax,0x20
     80f:	48 89 c7             	mov    rdi,rax
     812:	e8 29 fb ff ff       	call   340 <loadu64>
     817:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
     81e:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2d0]
     825:	fd ff ff 
     828:	62 f1 fd 28 7f 8d f0 	vmovdqa64 YMMWORD PTR [rbp-0x110],ymm1
     82f:	fe ff ff 
     832:	88 85 d2 fc ff ff    	mov    BYTE PTR [rbp-0x32e],al
     838:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2d0]
     83f:	fd ff ff 
     842:	62 f1 fd 28 7f 8d 10 	vmovdqa64 YMMWORD PTR [rbp-0xf0],ymm1
     849:	ff ff ff 
     84c:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0xd0],ymm0
     853:	ff ff ff 
     856:	0f b6 85 d2 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32e]
     85d:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0xd0]
     864:	ff ff ff 
     867:	62 f1 fd 28 6f 85 f0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x110]
     86e:	fe ff ff 
     871:	c5 f9 92 d8          	kmovb  k3,eax
     875:	62 f1 f5 2b ef 85 10 	vpxorq ymm0{k3},ymm1,YMMWORD PTR [rbp-0xf0]
     87c:	ff ff ff 
     87f:	90                   	nop
     880:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0x2d0],ymm0
     887:	fd ff ff 
        t2 = _mm256_mask_xor_epi64(t2, m, t2, loadu64(&red_table[t][4*2]));
     88a:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     890:	48 63 d0             	movsxd rdx,eax
     893:	48 89 d0             	mov    rax,rdx
     896:	48 c1 e0 02          	shl    rax,0x2
     89a:	48 01 d0             	add    rax,rdx
     89d:	48 c1 e0 05          	shl    rax,0x5
     8a1:	48 89 c2             	mov    rdx,rax
     8a4:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
     8ab:	48 01 d0             	add    rax,rdx
     8ae:	48 83 c0 40          	add    rax,0x40
     8b2:	48 89 c7             	mov    rdi,rax
     8b5:	e8 86 fa ff ff       	call   340 <loadu64>
     8ba:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
     8c1:	62 f1 fd 28 6f 8d 50 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2b0]
     8c8:	fd ff ff 
     8cb:	62 f1 fd 28 7f 8d 90 	vmovdqa64 YMMWORD PTR [rbp-0x170],ymm1
     8d2:	fe ff ff 
     8d5:	88 85 d1 fc ff ff    	mov    BYTE PTR [rbp-0x32f],al
     8db:	62 f1 fd 28 6f 8d 50 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2b0]
     8e2:	fd ff ff 
     8e5:	62 f1 fd 28 7f 8d b0 	vmovdqa64 YMMWORD PTR [rbp-0x150],ymm1
     8ec:	fe ff ff 
     8ef:	62 f1 fd 28 7f 85 d0 	vmovdqa64 YMMWORD PTR [rbp-0x130],ymm0
     8f6:	fe ff ff 
     8f9:	0f b6 85 d1 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32f]
     900:	62 f1 fd 28 6f 8d d0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x130]
     907:	fe ff ff 
     90a:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x170]
     911:	fe ff ff 
     914:	c5 f9 92 e0          	kmovb  k4,eax
     918:	62 f1 f5 2c ef 85 b0 	vpxorq ymm0{k4},ymm1,YMMWORD PTR [rbp-0x150]
     91f:	fe ff ff 
     922:	90                   	nop
     923:	62 f1 fd 28 7f 85 50 	vmovdqa64 YMMWORD PTR [rbp-0x2b0],ymm0
     92a:	fd ff ff 
        t3 = _mm256_mask_xor_epi64(t3, m, t3, loadu64(&red_table[t][4*3]));
     92d:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     933:	48 63 d0             	movsxd rdx,eax
     936:	48 89 d0             	mov    rax,rdx
     939:	48 c1 e0 02          	shl    rax,0x2
     93d:	48 01 d0             	add    rax,rdx
     940:	48 c1 e0 05          	shl    rax,0x5
     944:	48 89 c2             	mov    rdx,rax
     947:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
     94e:	48 01 d0             	add    rax,rdx
     951:	48 83 c0 60          	add    rax,0x60
     955:	48 89 c7             	mov    rdi,rax
     958:	e8 e3 f9 ff ff       	call   340 <loadu64>
     95d:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
     964:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x290]
     96b:	fd ff ff 
     96e:	62 f1 fd 28 7f 8d 30 	vmovdqa64 YMMWORD PTR [rbp-0x1d0],ymm1
     975:	fe ff ff 
     978:	88 85 d0 fc ff ff    	mov    BYTE PTR [rbp-0x330],al
     97e:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x290]
     985:	fd ff ff 
     988:	62 f1 fd 28 7f 8d 50 	vmovdqa64 YMMWORD PTR [rbp-0x1b0],ymm1
     98f:	fe ff ff 
     992:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x190],ymm0
     999:	fe ff ff 
     99c:	0f b6 85 d0 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x330]
     9a3:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x190]
     9aa:	fe ff ff 
     9ad:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x1d0]
     9b4:	fe ff ff 
     9b7:	c5 f9 92 e8          	kmovb  k5,eax
     9bb:	62 f1 f5 2d ef 85 50 	vpxorq ymm0{k5},ymm1,YMMWORD PTR [rbp-0x1b0]
     9c2:	fe ff ff 
     9c5:	90                   	nop
     9c6:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x290],ymm0
     9cd:	fd ff ff 
        t4 = _mm256_mask_xor_epi64(t4, m, t4, loadu64(&red_table[t][4*4]));
     9d0:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     9d6:	48 63 d0             	movsxd rdx,eax
     9d9:	48 89 d0             	mov    rax,rdx
     9dc:	48 c1 e0 02          	shl    rax,0x2
     9e0:	48 01 d0             	add    rax,rdx
     9e3:	48 c1 e0 05          	shl    rax,0x5
     9e7:	48 89 c2             	mov    rdx,rax
     9ea:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
     9f1:	48 01 d0             	add    rax,rdx
     9f4:	48 83 e8 80          	sub    rax,0xffffffffffffff80
     9f8:	48 89 c7             	mov    rdi,rax
     9fb:	e8 40 f9 ff ff       	call   340 <loadu64>
     a00:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
     a07:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x270]
     a0e:	fd ff ff 
     a11:	62 f1 fd 28 7f 8d d0 	vmovdqa64 YMMWORD PTR [rbp-0x230],ymm1
     a18:	fd ff ff 
     a1b:	88 85 cf fc ff ff    	mov    BYTE PTR [rbp-0x331],al
     a21:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x270]
     a28:	fd ff ff 
     a2b:	62 f1 fd 28 7f 8d f0 	vmovdqa64 YMMWORD PTR [rbp-0x210],ymm1
     a32:	fd ff ff 
     a35:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x1f0],ymm0
     a3c:	fe ff ff 
     a3f:	0f b6 85 cf fc ff ff 	movzx  eax,BYTE PTR [rbp-0x331]
     a46:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x1f0]
     a4d:	fe ff ff 
     a50:	62 f1 fd 28 6f 85 d0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x230]
     a57:	fd ff ff 
     a5a:	c5 f9 92 f0          	kmovb  k6,eax
     a5e:	62 f1 f5 2e ef 85 f0 	vpxorq ymm0{k6},ymm1,YMMWORD PTR [rbp-0x210]
     a65:	fd ff ff 
     a68:	90                   	nop
     a69:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x270],ymm0
     a70:	fd ff ff 
    for (int t = 0; t < (1U << EXP_WIN_SIZE); ++t, cur_idx = add64(cur_idx, set64(1))) {
     a73:	ff 85 d4 fc ff ff    	inc    DWORD PTR [rbp-0x32c]
     a79:	48 c7 85 e8 fc ff ff 	mov    QWORD PTR [rbp-0x318],0x1
     a80:	01 00 00 00 
     a84:	c4 e2 7d 59 85 e8 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x318]
     a8b:	ff ff 
     a8d:	62 f1 fd 28 6f c8    	vmovdqa64 ymm1,ymm0
     a93:	62 f1 fd 28 6f 85 f0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x310]
     a9a:	fc ff ff 
     a9d:	e8 fe f9 ff ff       	call   4a0 <add64>
     aa2:	62 f1 fd 28 7f 85 f0 	vmovdqa64 YMMWORD PTR [rbp-0x310],ymm0
     aa9:	fc ff ff 
     aac:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
     ab2:	83 f8 1f             	cmp    eax,0x1f
     ab5:	0f 86 66 fc ff ff    	jbe    721 <extract_multiplier+0xe1>
     abb:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
     ac2:	fd ff ff 
     ac5:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x50],ymm0
     acc:	ff ff ff 
     acf:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
     ad6:	fd ff ff 
     ad9:	62 f1 fd 28 7f 85 d0 	vmovdqa64 YMMWORD PTR [rbp-0x30],ymm0
     ae0:	ff ff ff 

extern __inline __m256i
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_xor_si256 (__m256i __A, __m256i __B)
{
  return (__m256i) ((__v4du)__A ^ (__v4du)__B);
     ae3:	62 f1 fd 28 6f 8d b0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x50]
     aea:	ff ff ff 
     aed:	62 f1 fd 28 6f 85 d0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x30]
     af4:	ff ff ff 
     af7:	c5 f5 ef c0          	vpxor  ymm0,ymm1,ymm0
    }

    /* Clear index */
    idx = xor64(idx, idx);
     afb:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x250],ymm0
     b02:	fd ff ff 

    storeu64(&red_Y[4*0], t0);
     b05:	62 f1 fd 28 6f 85 10 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2f0]
     b0c:	fd ff ff 
     b0f:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
     b16:	48 89 c7             	mov    rdi,rax
     b19:	e8 62 f8 ff ff       	call   380 <storeu64>
    storeu64(&red_Y[4*1], t1);
     b1e:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
     b25:	48 83 c0 20          	add    rax,0x20
     b29:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2d0]
     b30:	fd ff ff 
     b33:	48 89 c7             	mov    rdi,rax
     b36:	e8 45 f8 ff ff       	call   380 <storeu64>
    storeu64(&red_Y[4*2], t2);
     b3b:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
     b42:	48 83 c0 40          	add    rax,0x40
     b46:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2b0]
     b4d:	fd ff ff 
     b50:	48 89 c7             	mov    rdi,rax
     b53:	e8 28 f8 ff ff       	call   380 <storeu64>
    storeu64(&red_Y[4*3], t3);
     b58:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
     b5f:	48 83 c0 60          	add    rax,0x60
     b63:	62 f1 fd 28 6f 85 70 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x290]
     b6a:	fd ff ff 
     b6d:	48 89 c7             	mov    rdi,rax
     b70:	e8 0b f8 ff ff       	call   380 <storeu64>
    storeu64(&red_Y[4*4], t4);
     b75:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
     b7c:	48 83 e8 80          	sub    rax,0xffffffffffffff80
     b80:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x270]
     b87:	fd ff ff 
     b8a:	48 89 c7             	mov    rdi,rax
     b8d:	e8 ee f7 ff ff       	call   380 <storeu64>
}
     b92:	90                   	nop
     b93:	48 81 c4 68 03 00 00 	add    rsp,0x368
     b9a:	41 5a                	pop    r10
     b9c:	5d                   	pop    rbp
     b9d:	49 8d 62 f8          	lea    rsp,[r10-0x8]
     ba1:	c3                   	ret    
     ba2:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     ba9:	00 00 00 00 
     bad:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
     bb4:	00 00 00 00 
     bb8:	0f 1f 84 00 00 00 00 	nop    DWORD PTR [rax+rax*1+0x0]
     bbf:	00 

0000000000000bc0 <k1_ifma256_exp52x20>:
                                 const Ipp64u *base,
                                 const Ipp64u *exp,
                                 const Ipp64u *modulus,
                                 const Ipp64u *toMont,
                                 const Ipp64u k0))
{
     bc0:	f3 0f 1e fa          	endbr64 
     bc4:	55                   	push   rbp
     bc5:	48 89 e5             	mov    rbp,rsp
     bc8:	48 83 e4 c0          	and    rsp,0xffffffffffffffc0
     bcc:	48 81 ec 00 10 00 00 	sub    rsp,0x1000
     bd3:	48 83 0c 24 00       	or     QWORD PTR [rsp],0x0
     bd8:	48 81 ec c0 06 00 00 	sub    rsp,0x6c0
     bdf:	48 89 7c 24 28       	mov    QWORD PTR [rsp+0x28],rdi
     be4:	48 89 74 24 20       	mov    QWORD PTR [rsp+0x20],rsi
     be9:	48 89 54 24 18       	mov    QWORD PTR [rsp+0x18],rdx
     bee:	48 89 4c 24 10       	mov    QWORD PTR [rsp+0x10],rcx
     bf3:	4c 89 44 24 08       	mov    QWORD PTR [rsp+0x8],r8
     bf8:	4c 89 0c 24          	mov    QWORD PTR [rsp],r9

   /* pre-computed table of base powers */
   __ALIGN64 Ipp64u red_table[1U << EXP_WIN_SIZE][LEN52];

   /* zero all temp buffers (due to zero padding) */
   ZEXPAND_BNU(red_Y, 0, LEN52);
     bfc:	c7 44 24 3c 00 00 00 	mov    DWORD PTR [rsp+0x3c],0x0
     c03:	00 
     c04:	eb 16                	jmp    c1c <k1_ifma256_exp52x20+0x5c>
     c06:	8b 44 24 3c          	mov    eax,DWORD PTR [rsp+0x3c]
     c0a:	48 98                	cdqe   
     c0c:	48 c7 84 c4 40 01 00 	mov    QWORD PTR [rsp+rax*8+0x140],0x0
     c13:	00 00 00 00 00 
     c18:	ff 44 24 3c          	inc    DWORD PTR [rsp+0x3c]
     c1c:	83 7c 24 3c 13       	cmp    DWORD PTR [rsp+0x3c],0x13
     c21:	7e e3                	jle    c06 <k1_ifma256_exp52x20+0x46>
   ZEXPAND_BNU((Ipp64u*)red_table, 0, LEN52 * (1U << EXP_WIN_SIZE));
     c23:	c7 44 24 40 00 00 00 	mov    DWORD PTR [rsp+0x40],0x0
     c2a:	00 
     c2b:	eb 24                	jmp    c51 <k1_ifma256_exp52x20+0x91>
     c2d:	8b 44 24 40          	mov    eax,DWORD PTR [rsp+0x40]
     c31:	48 98                	cdqe   
     c33:	48 8d 14 c5 00 00 00 	lea    rdx,[rax*8+0x0]
     c3a:	00 
     c3b:	48 8d 84 24 c0 02 00 	lea    rax,[rsp+0x2c0]
     c42:	00 
     c43:	48 01 d0             	add    rax,rdx
     c46:	48 c7 00 00 00 00 00 	mov    QWORD PTR [rax],0x0
     c4d:	ff 44 24 40          	inc    DWORD PTR [rsp+0x40]
     c51:	8b 44 24 40          	mov    eax,DWORD PTR [rsp+0x40]
     c55:	3d 7f 02 00 00       	cmp    eax,0x27f
     c5a:	76 d1                	jbe    c2d <k1_ifma256_exp52x20+0x6d>
   ZEXPAND_BNU(red_X, 0, LEN52); /* table[0] = mont(x^0) = mont(1) */
     c5c:	c7 44 24 44 00 00 00 	mov    DWORD PTR [rsp+0x44],0x0
     c63:	00 
     c64:	eb 16                	jmp    c7c <k1_ifma256_exp52x20+0xbc>
     c66:	8b 44 24 44          	mov    eax,DWORD PTR [rsp+0x44]
     c6a:	48 98                	cdqe   
     c6c:	48 c7 84 c4 00 02 00 	mov    QWORD PTR [rsp+rax*8+0x200],0x0
     c73:	00 00 00 00 00 
     c78:	ff 44 24 44          	inc    DWORD PTR [rsp+0x44]
     c7c:	83 7c 24 44 13       	cmp    DWORD PTR [rsp+0x44],0x13
     c81:	7e e3                	jle    c66 <k1_ifma256_exp52x20+0xa6>
   int idx;

   /*
   // compute table of powers base^i, i=0, ..., (2^EXP_WIN_SIZE) -1
   */
   red_X[0] = 1ULL;
     c83:	48 c7 84 24 00 02 00 	mov    QWORD PTR [rsp+0x200],0x1
     c8a:	00 01 00 00 00 
   AMM(red_table[0], red_X, toMont, modulus, k0);
     c8f:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
     c93:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
     c98:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
     c9d:	48 8d b4 24 00 02 00 	lea    rsi,[rsp+0x200]
     ca4:	00 
     ca5:	48 8d 84 24 c0 02 00 	lea    rax,[rsp+0x2c0]
     cac:	00 
     cad:	49 89 f8             	mov    r8,rdi
     cb0:	48 89 c7             	mov    rdi,rax
     cb3:	e8 00 00 00 00       	call   cb8 <k1_ifma256_exp52x20+0xf8>
   AMM(red_table[1], base, toMont, modulus, k0);
     cb8:	4c 8b 04 24          	mov    r8,QWORD PTR [rsp]
     cbc:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
     cc1:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
     cc6:	48 8b 44 24 20       	mov    rax,QWORD PTR [rsp+0x20]
     ccb:	48 8d b4 24 c0 02 00 	lea    rsi,[rsp+0x2c0]
     cd2:	00 
     cd3:	48 8d be a0 00 00 00 	lea    rdi,[rsi+0xa0]
     cda:	48 89 c6             	mov    rsi,rax
     cdd:	e8 00 00 00 00       	call   ce2 <k1_ifma256_exp52x20+0x122>

   for (idx = 1; idx < (1U << EXP_WIN_SIZE)/2; idx++) {
     ce2:	c7 44 24 48 01 00 00 	mov    DWORD PTR [rsp+0x48],0x1
     ce9:	00 
     cea:	e9 c7 00 00 00       	jmp    db6 <k1_ifma256_exp52x20+0x1f6>
      AMS(red_table[2*idx],   red_table[idx],  modulus, k0);
     cef:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
     cf6:	00 
     cf7:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
     cfb:	48 63 d0             	movsxd rdx,eax
     cfe:	48 89 d0             	mov    rax,rdx
     d01:	48 c1 e0 02          	shl    rax,0x2
     d05:	48 01 d0             	add    rax,rdx
     d08:	48 c1 e0 05          	shl    rax,0x5
     d0c:	48 8d 34 01          	lea    rsi,[rcx+rax*1]
     d10:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
     d14:	01 c0                	add    eax,eax
     d16:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
     d1d:	00 
     d1e:	48 63 d0             	movsxd rdx,eax
     d21:	48 89 d0             	mov    rax,rdx
     d24:	48 c1 e0 02          	shl    rax,0x2
     d28:	48 01 d0             	add    rax,rdx
     d2b:	48 c1 e0 05          	shl    rax,0x5
     d2f:	48 8d 3c 01          	lea    rdi,[rcx+rax*1]
     d33:	48 8b 14 24          	mov    rdx,QWORD PTR [rsp]
     d37:	48 8b 44 24 10       	mov    rax,QWORD PTR [rsp+0x10]
     d3c:	48 89 d1             	mov    rcx,rdx
     d3f:	48 89 c2             	mov    rdx,rax
     d42:	e8 00 00 00 00       	call   d47 <k1_ifma256_exp52x20+0x187>
      AMM(red_table[2*idx+1], red_table[2*idx],red_table[1], modulus, k0);
     d47:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
     d4b:	01 c0                	add    eax,eax
     d4d:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
     d54:	00 
     d55:	48 63 d0             	movsxd rdx,eax
     d58:	48 89 d0             	mov    rax,rdx
     d5b:	48 c1 e0 02          	shl    rax,0x2
     d5f:	48 01 d0             	add    rax,rdx
     d62:	48 c1 e0 05          	shl    rax,0x5
     d66:	48 8d 34 01          	lea    rsi,[rcx+rax*1]
     d6a:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
     d6e:	01 c0                	add    eax,eax
     d70:	ff c0                	inc    eax
     d72:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
     d79:	00 
     d7a:	48 63 d0             	movsxd rdx,eax
     d7d:	48 89 d0             	mov    rax,rdx
     d80:	48 c1 e0 02          	shl    rax,0x2
     d84:	48 01 d0             	add    rax,rdx
     d87:	48 c1 e0 05          	shl    rax,0x5
     d8b:	48 8d 3c 01          	lea    rdi,[rcx+rax*1]
     d8f:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     d93:	48 8b 44 24 10       	mov    rax,QWORD PTR [rsp+0x10]
     d98:	48 8d 94 24 c0 02 00 	lea    rdx,[rsp+0x2c0]
     d9f:	00 
     da0:	48 81 c2 a0 00 00 00 	add    rdx,0xa0
     da7:	49 89 c8             	mov    r8,rcx
     daa:	48 89 c1             	mov    rcx,rax
     dad:	e8 00 00 00 00       	call   db2 <k1_ifma256_exp52x20+0x1f2>
   for (idx = 1; idx < (1U << EXP_WIN_SIZE)/2; idx++) {
     db2:	ff 44 24 48          	inc    DWORD PTR [rsp+0x48]
     db6:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
     dba:	83 f8 0f             	cmp    eax,0xf
     dbd:	0f 86 2c ff ff ff    	jbe    cef <k1_ifma256_exp52x20+0x12f>
   }

   /* copy and expand exponents */
   ZEXPAND_COPY_BNU(expz, LEN64+1, exp, LEN64);
     dc3:	c7 44 24 4c 00 00 00 	mov    DWORD PTR [rsp+0x4c],0x0
     dca:	00 
     dcb:	eb 2b                	jmp    df8 <k1_ifma256_exp52x20+0x238>
     dcd:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
     dd1:	48 98                	cdqe   
     dd3:	48 8d 14 c5 00 00 00 	lea    rdx,[rax*8+0x0]
     dda:	00 
     ddb:	48 8b 44 24 18       	mov    rax,QWORD PTR [rsp+0x18]
     de0:	48 01 d0             	add    rax,rdx
     de3:	48 8b 10             	mov    rdx,QWORD PTR [rax]
     de6:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
     dea:	48 98                	cdqe   
     dec:	48 89 94 c4 80 00 00 	mov    QWORD PTR [rsp+rax*8+0x80],rdx
     df3:	00 
     df4:	ff 44 24 4c          	inc    DWORD PTR [rsp+0x4c]
     df8:	83 7c 24 4c 0f       	cmp    DWORD PTR [rsp+0x4c],0xf
     dfd:	7e ce                	jle    dcd <k1_ifma256_exp52x20+0x20d>
     dff:	eb 16                	jmp    e17 <k1_ifma256_exp52x20+0x257>
     e01:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
     e05:	48 98                	cdqe   
     e07:	48 c7 84 c4 80 00 00 	mov    QWORD PTR [rsp+rax*8+0x80],0x0
     e0e:	00 00 00 00 00 
     e13:	ff 44 24 4c          	inc    DWORD PTR [rsp+0x4c]
     e17:	83 7c 24 4c 10       	cmp    DWORD PTR [rsp+0x4c],0x10
     e1c:	7e e3                	jle    e01 <k1_ifma256_exp52x20+0x241>

   /* exponentiation */
   {
      int rem = BITSIZE_MODULUS % EXP_WIN_SIZE;
     e1e:	c7 44 24 58 04 00 00 	mov    DWORD PTR [rsp+0x58],0x4
     e25:	00 
      int delta = rem ? rem : EXP_WIN_SIZE;
     e26:	83 7c 24 58 00       	cmp    DWORD PTR [rsp+0x58],0x0
     e2b:	74 06                	je     e33 <k1_ifma256_exp52x20+0x273>
     e2d:	8b 44 24 58          	mov    eax,DWORD PTR [rsp+0x58]
     e31:	eb 05                	jmp    e38 <k1_ifma256_exp52x20+0x278>
     e33:	b8 05 00 00 00       	mov    eax,0x5
     e38:	89 44 24 5c          	mov    DWORD PTR [rsp+0x5c],eax
      Ipp64u table_idx_mask = EXP_WIN_MASK;
     e3c:	48 c7 44 24 68 1f 00 	mov    QWORD PTR [rsp+0x68],0x1f
     e43:	00 00 

      int exp_bit_no = BITSIZE_MODULUS - delta;
     e45:	b8 00 04 00 00       	mov    eax,0x400
     e4a:	2b 44 24 5c          	sub    eax,DWORD PTR [rsp+0x5c]
     e4e:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
      int exp_chunk_no = exp_bit_no / 64;
     e52:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
     e56:	8d 50 3f             	lea    edx,[rax+0x3f]
     e59:	85 c0                	test   eax,eax
     e5b:	0f 48 c2             	cmovs  eax,edx
     e5e:	c1 f8 06             	sar    eax,0x6
     e61:	89 44 24 60          	mov    DWORD PTR [rsp+0x60],eax
      int exp_chunk_shift = exp_bit_no % 64;
     e65:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
     e69:	99                   	cdq    
     e6a:	c1 ea 1a             	shr    edx,0x1a
     e6d:	01 d0                	add    eax,edx
     e6f:	83 e0 3f             	and    eax,0x3f
     e72:	29 d0                	sub    eax,edx
     e74:	89 44 24 64          	mov    DWORD PTR [rsp+0x64],eax

      /* process 1-st exp window - just init result */
      Ipp64u red_table_idx = expz[exp_chunk_no];
     e78:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
     e7c:	48 98                	cdqe   
     e7e:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
     e85:	00 
     e86:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax
      red_table_idx = red_table_idx >> exp_chunk_shift;
     e8b:	8b 44 24 64          	mov    eax,DWORD PTR [rsp+0x64]
     e8f:	89 c1                	mov    ecx,eax
     e91:	48 d3 6c 24 70       	shr    QWORD PTR [rsp+0x70],cl

      extract_multiplier(red_Y, (const Ipp64u(*)[LEN52])red_table, (int)red_table_idx);
     e96:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
     e9b:	89 c2                	mov    edx,eax
     e9d:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
     ea4:	00 
     ea5:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     eac:	00 
     ead:	48 89 ce             	mov    rsi,rcx
     eb0:	48 89 c7             	mov    rdi,rax
     eb3:	e8 88 f7 ff ff       	call   640 <extract_multiplier>

      /* process other exp windows */
      for (exp_bit_no -= EXP_WIN_SIZE; exp_bit_no >= 0; exp_bit_no -= EXP_WIN_SIZE) {
     eb8:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
     ebc:	83 e8 05             	sub    eax,0x5
     ebf:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
     ec3:	e9 91 01 00 00       	jmp    1059 <k1_ifma256_exp52x20+0x499>
         /* series of squaring */
         AMS(red_Y, red_Y, modulus, k0);
     ec8:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     ecc:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
     ed1:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
     ed8:	00 
     ed9:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     ee0:	00 
     ee1:	48 89 c7             	mov    rdi,rax
     ee4:	e8 00 00 00 00       	call   ee9 <k1_ifma256_exp52x20+0x329>
         AMS(red_Y, red_Y, modulus, k0);
     ee9:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     eed:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
     ef2:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
     ef9:	00 
     efa:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     f01:	00 
     f02:	48 89 c7             	mov    rdi,rax
     f05:	e8 00 00 00 00       	call   f0a <k1_ifma256_exp52x20+0x34a>
         AMS(red_Y, red_Y, modulus, k0);
     f0a:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     f0e:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
     f13:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
     f1a:	00 
     f1b:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     f22:	00 
     f23:	48 89 c7             	mov    rdi,rax
     f26:	e8 00 00 00 00       	call   f2b <k1_ifma256_exp52x20+0x36b>
         AMS(red_Y, red_Y, modulus, k0);
     f2b:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     f2f:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
     f34:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
     f3b:	00 
     f3c:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     f43:	00 
     f44:	48 89 c7             	mov    rdi,rax
     f47:	e8 00 00 00 00       	call   f4c <k1_ifma256_exp52x20+0x38c>
         AMS(red_Y, red_Y, modulus, k0);
     f4c:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
     f50:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
     f55:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
     f5c:	00 
     f5d:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
     f64:	00 
     f65:	48 89 c7             	mov    rdi,rax
     f68:	e8 00 00 00 00       	call   f6d <k1_ifma256_exp52x20+0x3ad>

         /* extract pre-computed multiplier from the table */
         {
            Ipp64u T;
            exp_chunk_no = exp_bit_no / 64;
     f6d:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
     f71:	8d 50 3f             	lea    edx,[rax+0x3f]
     f74:	85 c0                	test   eax,eax
     f76:	0f 48 c2             	cmovs  eax,edx
     f79:	c1 f8 06             	sar    eax,0x6
     f7c:	89 44 24 60          	mov    DWORD PTR [rsp+0x60],eax
            exp_chunk_shift = exp_bit_no % 64;
     f80:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
     f84:	99                   	cdq    
     f85:	c1 ea 1a             	shr    edx,0x1a
     f88:	01 d0                	add    eax,edx
     f8a:	83 e0 3f             	and    eax,0x3f
     f8d:	29 d0                	sub    eax,edx
     f8f:	89 44 24 64          	mov    DWORD PTR [rsp+0x64],eax

            red_table_idx = expz[exp_chunk_no];
     f93:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
     f97:	48 98                	cdqe   
     f99:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
     fa0:	00 
     fa1:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax
            T = expz[exp_chunk_no+1];
     fa6:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
     faa:	ff c0                	inc    eax
     fac:	48 98                	cdqe   
     fae:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
     fb5:	00 
     fb6:	48 89 44 24 78       	mov    QWORD PTR [rsp+0x78],rax

            red_table_idx = red_table_idx >> exp_chunk_shift;
     fbb:	8b 44 24 64          	mov    eax,DWORD PTR [rsp+0x64]
     fbf:	89 c1                	mov    ecx,eax
     fc1:	48 d3 6c 24 70       	shr    QWORD PTR [rsp+0x70],cl
            T = exp_chunk_shift == 0 ? 0 : T << (64 - exp_chunk_shift);
     fc6:	83 7c 24 64 00       	cmp    DWORD PTR [rsp+0x64],0x0
     fcb:	74 15                	je     fe2 <k1_ifma256_exp52x20+0x422>
     fcd:	b8 40 00 00 00       	mov    eax,0x40
     fd2:	2b 44 24 64          	sub    eax,DWORD PTR [rsp+0x64]
     fd6:	48 8b 54 24 78       	mov    rdx,QWORD PTR [rsp+0x78]
     fdb:	c4 e2 f9 f7 c2       	shlx   rax,rdx,rax
     fe0:	eb 05                	jmp    fe7 <k1_ifma256_exp52x20+0x427>
     fe2:	b8 00 00 00 00       	mov    eax,0x0
     fe7:	48 89 44 24 78       	mov    QWORD PTR [rsp+0x78],rax
            red_table_idx = (red_table_idx ^ T) & table_idx_mask;
     fec:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
     ff1:	48 33 44 24 78       	xor    rax,QWORD PTR [rsp+0x78]
     ff6:	48 23 44 24 68       	and    rax,QWORD PTR [rsp+0x68]
     ffb:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax

            extract_multiplier(red_X, (const Ipp64u(*)[LEN52])red_table, (int)red_table_idx);
    1000:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
    1005:	89 c2                	mov    edx,eax
    1007:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
    100e:	00 
    100f:	48 8d 84 24 00 02 00 	lea    rax,[rsp+0x200]
    1016:	00 
    1017:	48 89 ce             	mov    rsi,rcx
    101a:	48 89 c7             	mov    rdi,rax
    101d:	e8 1e f6 ff ff       	call   640 <extract_multiplier>
            AMM(red_Y, red_Y, red_X, modulus, k0);
    1022:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
    1026:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
    102b:	48 8d 94 24 00 02 00 	lea    rdx,[rsp+0x200]
    1032:	00 
    1033:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
    103a:	00 
    103b:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
    1042:	00 
    1043:	49 89 f8             	mov    r8,rdi
    1046:	48 89 c7             	mov    rdi,rax
    1049:	e8 00 00 00 00       	call   104e <k1_ifma256_exp52x20+0x48e>
      for (exp_bit_no -= EXP_WIN_SIZE; exp_bit_no >= 0; exp_bit_no -= EXP_WIN_SIZE) {
    104e:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
    1052:	83 e8 05             	sub    eax,0x5
    1055:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
    1059:	83 7c 24 50 00       	cmp    DWORD PTR [rsp+0x50],0x0
    105e:	0f 89 64 fe ff ff    	jns    ec8 <k1_ifma256_exp52x20+0x308>
         }
      }
   }

   /* clear exponents */
   PurgeBlock((Ipp64u*)expz, (LEN64+1)*(int)sizeof(Ipp64u));
    1064:	48 8d 84 24 80 00 00 	lea    rax,[rsp+0x80]
    106b:	00 
    106c:	be 88 00 00 00       	mov    esi,0x88
    1071:	48 89 c7             	mov    rdi,rax
    1074:	e8 00 00 00 00       	call   1079 <k1_ifma256_exp52x20+0x4b9>

   /* convert result back in regular 2^52 domain */
   ZEXPAND_BNU(red_X, 0, LEN52);
    1079:	c7 44 24 54 00 00 00 	mov    DWORD PTR [rsp+0x54],0x0
    1080:	00 
    1081:	eb 16                	jmp    1099 <k1_ifma256_exp52x20+0x4d9>
    1083:	8b 44 24 54          	mov    eax,DWORD PTR [rsp+0x54]
    1087:	48 98                	cdqe   
    1089:	48 c7 84 c4 00 02 00 	mov    QWORD PTR [rsp+rax*8+0x200],0x0
    1090:	00 00 00 00 00 
    1095:	ff 44 24 54          	inc    DWORD PTR [rsp+0x54]
    1099:	83 7c 24 54 13       	cmp    DWORD PTR [rsp+0x54],0x13
    109e:	7e e3                	jle    1083 <k1_ifma256_exp52x20+0x4c3>
   red_X[0] = 1ULL;
    10a0:	48 c7 84 24 00 02 00 	mov    QWORD PTR [rsp+0x200],0x1
    10a7:	00 01 00 00 00 
   AMM(out, red_Y, red_X, modulus, k0);
    10ac:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
    10b0:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
    10b5:	48 8d 94 24 00 02 00 	lea    rdx,[rsp+0x200]
    10bc:	00 
    10bd:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
    10c4:	00 
    10c5:	48 8b 44 24 28       	mov    rax,QWORD PTR [rsp+0x28]
    10ca:	49 89 f8             	mov    r8,rdi
    10cd:	48 89 c7             	mov    rdi,rax
    10d0:	e8 00 00 00 00       	call   10d5 <k1_ifma256_exp52x20+0x515>
}
    10d5:	90                   	nop
    10d6:	c9                   	leave  
    10d7:	c3                   	ret    

#endif

#endif
#endif
#endif
