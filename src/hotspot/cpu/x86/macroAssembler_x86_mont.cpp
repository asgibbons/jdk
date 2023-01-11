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

#ifdef _LP64
void MacroAssembler::montgomerySquare52x20(Register out, Register a, Register m, Register inv)
{
  montgomeryMultiply52x20(out, a, a, m, inv);
}

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
  evpcmpq(k6, k0, xmm2, xmm6, Assembler::noOverflow, false, Assembler::AVX_256bit);
  vpand(xmm0, xmm0, xmm2, Assembler::AVX_256bit);
  vpand(xmm1, xmm4, xmm2, Assembler::AVX_256bit);
  vpand(xmm5, xmm5, xmm2, Assembler::AVX_256bit);
  vpaddq(xmm0, xmm0, xmm8, Assembler::AVX_256bit);
  vpaddq(xmm1, xmm1, xmm12, Assembler::AVX_256bit);
  vpaddq(xmm5, xmm5, xmm3, Assembler::AVX_256bit);
  evpcmpq(k0, k0, xmm2, xmm0, Assembler::noOverflow, false, Assembler::AVX_256bit);
  evpcmpq(k1, k0, xmm2, xmm5, Assembler::noOverflow, false, Assembler::AVX_256bit);
  evpcmpq(k4, k0, xmm2, xmm1, Assembler::overflow, false, Assembler::AVX_256bit);
  evpcmpq(k7, k0, xmm2, xmm1, Assembler::noOverflow, false, Assembler::AVX_256bit);

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
  evpcmpq(k6, k0, xmm2, xmm5, Assembler::overflow, false, Assembler::AVX_256bit);
  vpand(xmm7, xmm7, xmm2, Assembler::AVX_256bit);
  evpcmpq(k2, k0, xmm2, xmm0, Assembler::overflow, false, Assembler::AVX_256bit);
  evpcmpq(k3, k0, xmm2, xmm6, Assembler::overflow, false, Assembler::AVX_256bit);
  vpaddq(xmm7, xmm7, xmm9, Assembler::AVX_256bit);
  evpcmpq(k5, k0, xmm2, xmm7, Assembler::overflow, false, Assembler::AVX_256bit);
  kmovbl(c_rarg4, k0);
  kmovbl(rtmp0, k7);
  evpcmpq(k0, k0, xmm2, xmm7, Assembler::noOverflow, false, Assembler::AVX_256bit);

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

void MacroAssembler::extract_multiplier1K(Register out, Register tab, Register ndx)
{

// 0000000000000640 <extract_multiplier>:
// #define AMS ifma256_ams52x20

// __INLINE void extract_multiplier(Ipp64u *red_Y,
//                            const Ipp64u red_table[1U << EXP_WIN_SIZE][LEN52],
//                                  int red_table_idx)
// {
//      640:	4c 8d 54 24 08       	lea    r10,[rsp+0x8]
//      645:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
//      649:	41 ff 72 f8          	push   QWORD PTR [r10-0x8]
//      64d:	55                   	push   rbp
//      64e:	48 89 e5             	mov    rbp,rsp
//      651:	41 52                	push   r10
//      653:	48 81 ec 68 03 00 00 	sub    rsp,0x368
//      65a:	48 89 bd b8 fc ff ff 	mov    QWORD PTR [rbp-0x348],rdi
//      661:	48 89 b5 b0 fc ff ff 	mov    QWORD PTR [rbp-0x350],rsi
//      668:	89 95 ac fc ff ff    	mov    DWORD PTR [rbp-0x354],edx
//     U64 idx = set64(red_table_idx);
//      66e:	8b 85 ac fc ff ff    	mov    eax,DWORD PTR [rbp-0x354]
//      674:	48 98                	cdqe   
//      676:	48 89 85 e0 fc ff ff 	mov    QWORD PTR [rbp-0x320],rax
// }

  lea(r10, Address(rsp, 0x8));
  andq(rsp, -32);
  pushq(Address(rsp, -8));
  push(rbp);
  movq(rbp, rsp);
  push(r10);
  subptr(rsp, 0x368);

#ifdef _WIN64
  movptr(rdi, rcx);
  movptr(rsi, rdx);
  movq(rdx, r8);
#endif

  movq(Address(rbp, -0x348), rdi);
  movq(Address(rbp, -0x350), rsi);
  movl(Address(rbp, -0x354), rdx);

  movl(rax, Address(rbp, -0x354));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x98);
  movq(Address(rbp, -0x320), rax);

// extern __inline __m256i __attribute__((__gnu_inline__, __always_inline__, __artificial__))
// _mm256_set1_epi64x (long long __A)
// {
//   return __extension__ (__m256i)(__v4di){ __A, __A, __A, __A };
//      67d:	c4 e2 7d 59 85 e0 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x320]
//      684:	ff ff 
//      686:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x250],ymm0
//      68d:	fd ff ff 
//      690:	48 c7 85 d8 fc ff ff 	mov    QWORD PTR [rbp-0x328],0x0
//      697:	00 00 00 00 
//      69b:	c4 e2 7d 59 85 d8 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x328]
//      6a2:	ff ff 
//     U64 cur_idx = set64(0);
//      6a4:	62 f1 fd 28 7f 85 f0 	vmovdqa64 YMMWORD PTR [rbp-0x310],ymm0
//      6ab:	fc ff ff 

  vpbroadcastq(xmm0, Address(rbp, -0x320), Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x250), xmm0, Assembler::AVX_256bit);
  movq(Address(rbp, -0x328), 0);
  vpbroadcastq(xmm0, Address(rbp, -0x328), Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x310), xmm0, Assembler::AVX_256bit);

//     U64 t0, t1, t2, t3, t4;
//     t0 = t1 = t2 = t3 = t4 = get_zero64();
//      6ae:	b8 00 00 00 00       	mov    eax,0x0
//      6b3:	e8 68 fe ff ff       	call   520 <get_zero64>
//      6b8:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x270],ymm0
//      6bf:	fd ff ff 
//      6c2:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x270]
//      6c9:	fd ff ff 
//      6cc:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x290],ymm0
//      6d3:	fd ff ff 
//      6d6:	62 f1 fd 28 6f 85 70 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x290]
//      6dd:	fd ff ff 
//      6e0:	62 f1 fd 28 7f 85 50 	vmovdqa64 YMMWORD PTR [rbp-0x2b0],ymm0
//      6e7:	fd ff ff 
//      6ea:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2b0]
//      6f1:	fd ff ff 
//      6f4:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0x2d0],ymm0
//      6fb:	fd ff ff 
//      6fe:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2d0]
//      705:	fd ff ff 
//      708:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x2f0],ymm0
//      70f:	fd ff ff 

  vpxor(xmm0, xmm0, xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x270), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x290), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x2b0), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x2d0), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rbp, -0x2f0), xmm0, Assembler::AVX_256bit);

//     for (int t = 0; t < (1U << EXP_WIN_SIZE); ++t, cur_idx = add64(cur_idx, set64(1))) {
//      712:	c7 85 d4 fc ff ff 00 	mov    DWORD PTR [rbp-0x32c],0x0
//      719:	00 00 00 
//      71c:	e9 8b 03 00 00       	jmp    aac <extract_multiplier+0x46c>

  Label L_e_aac, L_e_721;
  movl(Address(rbp, -0x32c), 0);
  jmp(L_e_aac);
  bind(L_e_721);
//         __mmask8 m = _mm256_cmp_epi64_mask(idx, cur_idx, _MM_CMPINT_EQ);
//      721:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
//      728:	fd ff ff 
//      72b:	62 f1 fd 28 6f 8d f0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x310]
//      732:	fc ff ff 
//      735:	c5 f5 46 c9          	kxnorb k1,k1,k1
//      739:	62 f3 fd 29 1f c1 00 	vpcmpeqq k0{k1},ymm0,ymm1
//      740:	c5 f9 91 85 ce fc ff 	kmovb  BYTE PTR [rbp-0x332],k0
//      747:	ff 

  evmovdquq(xmm0, Address(rbp, -0x250), Assembler::AVX_256bit);
  evmovdquq(xmm1, Address(rbp, -0x310), Assembler::AVX_256bit);
  kxnorbl(k1, k1, k1);
  evcmppd(k0, k1, xmm0, xmm1, EQ_UQ, Assembler::AVX_256bit);
  emit_int8((unsigned char) 0xc5);
  emit_int8((unsigned char) 0xf9);
  emit_int8((unsigned char) 0x91);
  emit_int8((unsigned char) 0x85);
  emit_int8((unsigned char) 0xce);
  emit_int8((unsigned char) 0xfc);
  emit_int8((unsigned char) 0xff);
  emit_int8((unsigned char) 0xff);

//         t0 = _mm256_mask_xor_epi64(t0, m, t0, loadu64(&red_table[t][4*0]));
//      748:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      74e:	48 63 d0             	movsxd rdx,eax
//      751:	48 89 d0             	mov    rax,rdx
//      754:	48 c1 e0 02          	shl    rax,0x2
//      758:	48 01 d0             	add    rax,rdx
//      75b:	48 c1 e0 05          	shl    rax,0x5
//      75f:	48 89 c2             	mov    rdx,rax
//      762:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
//      769:	48 01 d0             	add    rax,rdx
//      76c:	48 89 c7             	mov    rdi,rax
//      76f:	e8 cc fb ff ff       	call   340 <loadu64>
//      774:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
//      77b:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2f0]
//      782:	fd ff ff 
//      785:	62 f1 fd 28 7f 8d 50 	vmovdqa64 YMMWORD PTR [rbp-0xb0],ymm1
//      78c:	ff ff ff 
//      78f:	88 85 d3 fc ff ff    	mov    BYTE PTR [rbp-0x32d],al
//      795:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2f0]
//      79c:	fd ff ff 
//      79f:	62 f1 fd 28 7f 8d 70 	vmovdqa64 YMMWORD PTR [rbp-0x90],ymm1
//      7a6:	ff ff ff 
//      7a9:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x70],ymm0
//      7b0:	ff ff ff 

  movl(rax, Address(rbp, -0x32c));
#define movsxd emit_int8((unsigned char) 0x48); emit_int8((unsigned char) 0x63); emit_int8((unsigned char) 0xd0)
  movsxd;
  movq(rax, rdx);
  shlq(rax, 2);
  addq(rax, rdx);
  shlq(rax, 0x5);
  movq(rdx, rax);
  movq(rax, Address(rbp, -0x350));
  addq(rax, rdx);
  movq(rdi, rax);

// extern __inline __m256i
// __attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
// _mm256_mask_xor_epi64 (__m256i __W, __mmask8 __U, __m256i __A,
// 		       __m256i __B)
// {
//   return (__m256i) __builtin_ia32_pxorq256_mask ((__v4di) __A,
//      7b3:	0f b6 85 d3 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32d]
//      7ba:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x70]
//      7c1:	ff ff ff 
//      7c4:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0xb0]
//      7cb:	ff ff ff 
//      7ce:	c5 f9 92 d0          	kmovb  k2,eax
//      7d2:	62 f1 f5 2a ef 85 70 	vpxorq ymm0{k2},ymm1,YMMWORD PTR [rbp-0x90]
//      7d9:	ff ff ff 
//      7dc:	90                   	nop
//      7dd:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x2f0],ymm0
//      7e4:	fd ff ff 
//         t1 = _mm256_mask_xor_epi64(t1, m, t1, loadu64(&red_table[t][4*1]));
//      7e7:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      7ed:	48 63 d0             	movsxd rdx,eax
//      7f0:	48 89 d0             	mov    rax,rdx
//      7f3:	48 c1 e0 02          	shl    rax,0x2
//      7f7:	48 01 d0             	add    rax,rdx
//      7fa:	48 c1 e0 05          	shl    rax,0x5
//      7fe:	48 89 c2             	mov    rdx,rax
//      801:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
//      808:	48 01 d0             	add    rax,rdx
//      80b:	48 83 c0 20          	add    rax,0x20
//      80f:	48 89 c7             	mov    rdi,rax
//      812:	e8 29 fb ff ff       	call   340 <loadu64>
//      817:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
//      81e:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2d0]
//      825:	fd ff ff 
//      828:	62 f1 fd 28 7f 8d f0 	vmovdqa64 YMMWORD PTR [rbp-0x110],ymm1
//      82f:	fe ff ff 
//      832:	88 85 d2 fc ff ff    	mov    BYTE PTR [rbp-0x32e],al
//      838:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2d0]
//      83f:	fd ff ff 
//      842:	62 f1 fd 28 7f 8d 10 	vmovdqa64 YMMWORD PTR [rbp-0xf0],ymm1
//      849:	ff ff ff 
//      84c:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0xd0],ymm0
//      853:	ff ff ff 
//      856:	0f b6 85 d2 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32e]
//      85d:	62 f1 fd 28 6f 8d 30 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0xd0]
//      864:	ff ff ff 
//      867:	62 f1 fd 28 6f 85 f0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x110]
//      86e:	fe ff ff 
//      871:	c5 f9 92 d8          	kmovb  k3,eax
//      875:	62 f1 f5 2b ef 85 10 	vpxorq ymm0{k3},ymm1,YMMWORD PTR [rbp-0xf0]
//      87c:	ff ff ff 
//      87f:	90                   	nop
//      880:	62 f1 fd 28 7f 85 30 	vmovdqa64 YMMWORD PTR [rbp-0x2d0],ymm0
//      887:	fd ff ff 
//         t2 = _mm256_mask_xor_epi64(t2, m, t2, loadu64(&red_table[t][4*2]));
//      88a:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      890:	48 63 d0             	movsxd rdx,eax
//      893:	48 89 d0             	mov    rax,rdx
//      896:	48 c1 e0 02          	shl    rax,0x2
//      89a:	48 01 d0             	add    rax,rdx
//      89d:	48 c1 e0 05          	shl    rax,0x5
//      8a1:	48 89 c2             	mov    rdx,rax
//      8a4:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
//      8ab:	48 01 d0             	add    rax,rdx
//      8ae:	48 83 c0 40          	add    rax,0x40
//      8b2:	48 89 c7             	mov    rdi,rax
//      8b5:	e8 86 fa ff ff       	call   340 <loadu64>
//      8ba:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
//      8c1:	62 f1 fd 28 6f 8d 50 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2b0]
//      8c8:	fd ff ff 
//      8cb:	62 f1 fd 28 7f 8d 90 	vmovdqa64 YMMWORD PTR [rbp-0x170],ymm1
//      8d2:	fe ff ff 
//      8d5:	88 85 d1 fc ff ff    	mov    BYTE PTR [rbp-0x32f],al
//      8db:	62 f1 fd 28 6f 8d 50 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x2b0]
//      8e2:	fd ff ff 
//      8e5:	62 f1 fd 28 7f 8d b0 	vmovdqa64 YMMWORD PTR [rbp-0x150],ymm1
//      8ec:	fe ff ff 
//      8ef:	62 f1 fd 28 7f 85 d0 	vmovdqa64 YMMWORD PTR [rbp-0x130],ymm0
//      8f6:	fe ff ff 
//      8f9:	0f b6 85 d1 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x32f]
//      900:	62 f1 fd 28 6f 8d d0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x130]
//      907:	fe ff ff 
//      90a:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x170]
//      911:	fe ff ff 
//      914:	c5 f9 92 e0          	kmovb  k4,eax
//      918:	62 f1 f5 2c ef 85 b0 	vpxorq ymm0{k4},ymm1,YMMWORD PTR [rbp-0x150]
//      91f:	fe ff ff 
//      922:	90                   	nop
//      923:	62 f1 fd 28 7f 85 50 	vmovdqa64 YMMWORD PTR [rbp-0x2b0],ymm0
//      92a:	fd ff ff 
//         t3 = _mm256_mask_xor_epi64(t3, m, t3, loadu64(&red_table[t][4*3]));
//      92d:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      933:	48 63 d0             	movsxd rdx,eax
//      936:	48 89 d0             	mov    rax,rdx
//      939:	48 c1 e0 02          	shl    rax,0x2
//      93d:	48 01 d0             	add    rax,rdx
//      940:	48 c1 e0 05          	shl    rax,0x5
//      944:	48 89 c2             	mov    rdx,rax
//      947:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
//      94e:	48 01 d0             	add    rax,rdx
//      951:	48 83 c0 60          	add    rax,0x60
//      955:	48 89 c7             	mov    rdi,rax
//      958:	e8 e3 f9 ff ff       	call   340 <loadu64>
//      95d:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
//      964:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x290]
//      96b:	fd ff ff 
//      96e:	62 f1 fd 28 7f 8d 30 	vmovdqa64 YMMWORD PTR [rbp-0x1d0],ymm1
//      975:	fe ff ff 
//      978:	88 85 d0 fc ff ff    	mov    BYTE PTR [rbp-0x330],al
//      97e:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x290]
//      985:	fd ff ff 
//      988:	62 f1 fd 28 7f 8d 50 	vmovdqa64 YMMWORD PTR [rbp-0x1b0],ymm1
//      98f:	fe ff ff 
//      992:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x190],ymm0
//      999:	fe ff ff 
//      99c:	0f b6 85 d0 fc ff ff 	movzx  eax,BYTE PTR [rbp-0x330]
//      9a3:	62 f1 fd 28 6f 8d 70 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x190]
//      9aa:	fe ff ff 
//      9ad:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x1d0]
//      9b4:	fe ff ff 
//      9b7:	c5 f9 92 e8          	kmovb  k5,eax
//      9bb:	62 f1 f5 2d ef 85 50 	vpxorq ymm0{k5},ymm1,YMMWORD PTR [rbp-0x1b0]
//      9c2:	fe ff ff 
//      9c5:	90                   	nop
//      9c6:	62 f1 fd 28 7f 85 70 	vmovdqa64 YMMWORD PTR [rbp-0x290],ymm0
//      9cd:	fd ff ff 
//         t4 = _mm256_mask_xor_epi64(t4, m, t4, loadu64(&red_table[t][4*4]));
//      9d0:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      9d6:	48 63 d0             	movsxd rdx,eax
//      9d9:	48 89 d0             	mov    rax,rdx
//      9dc:	48 c1 e0 02          	shl    rax,0x2
//      9e0:	48 01 d0             	add    rax,rdx
//      9e3:	48 c1 e0 05          	shl    rax,0x5
//      9e7:	48 89 c2             	mov    rdx,rax
//      9ea:	48 8b 85 b0 fc ff ff 	mov    rax,QWORD PTR [rbp-0x350]
//      9f1:	48 01 d0             	add    rax,rdx
//      9f4:	48 83 e8 80          	sub    rax,0xffffffffffffff80
//      9f8:	48 89 c7             	mov    rdi,rax
//      9fb:	e8 40 f9 ff ff       	call   340 <loadu64>
//      a00:	0f b6 85 ce fc ff ff 	movzx  eax,BYTE PTR [rbp-0x332]
//      a07:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x270]
//      a0e:	fd ff ff 
//      a11:	62 f1 fd 28 7f 8d d0 	vmovdqa64 YMMWORD PTR [rbp-0x230],ymm1
//      a18:	fd ff ff 
//      a1b:	88 85 cf fc ff ff    	mov    BYTE PTR [rbp-0x331],al
//      a21:	62 f1 fd 28 6f 8d 90 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x270]
//      a28:	fd ff ff 
//      a2b:	62 f1 fd 28 7f 8d f0 	vmovdqa64 YMMWORD PTR [rbp-0x210],ymm1
//      a32:	fd ff ff 
//      a35:	62 f1 fd 28 7f 85 10 	vmovdqa64 YMMWORD PTR [rbp-0x1f0],ymm0
//      a3c:	fe ff ff 
//      a3f:	0f b6 85 cf fc ff ff 	movzx  eax,BYTE PTR [rbp-0x331]
//      a46:	62 f1 fd 28 6f 8d 10 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x1f0]
//      a4d:	fe ff ff 
//      a50:	62 f1 fd 28 6f 85 d0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x230]
//      a57:	fd ff ff 
//      a5a:	c5 f9 92 f0          	kmovb  k6,eax
//      a5e:	62 f1 f5 2e ef 85 f0 	vpxorq ymm0{k6},ymm1,YMMWORD PTR [rbp-0x210]
//      a65:	fd ff ff 
//      a68:	90                   	nop
//      a69:	62 f1 fd 28 7f 85 90 	vmovdqa64 YMMWORD PTR [rbp-0x270],ymm0
//      a70:	fd ff ff 
//     for (int t = 0; t < (1U << EXP_WIN_SIZE); ++t, cur_idx = add64(cur_idx, set64(1))) {
//      a73:	ff 85 d4 fc ff ff    	inc    DWORD PTR [rbp-0x32c]
//      a79:	48 c7 85 e8 fc ff ff 	mov    QWORD PTR [rbp-0x318],0x1
//      a80:	01 00 00 00 
//      a84:	c4 e2 7d 59 85 e8 fc 	vpbroadcastq ymm0,QWORD PTR [rbp-0x318]
//      a8b:	ff ff 
//      a8d:	62 f1 fd 28 6f c8    	vmovdqa64 ymm1,ymm0
//      a93:	62 f1 fd 28 6f 85 f0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x310]
//      a9a:	fc ff ff 
//      a9d:	e8 fe f9 ff ff       	call   4a0 <add64>
//      aa2:	62 f1 fd 28 7f 85 f0 	vmovdqa64 YMMWORD PTR [rbp-0x310],ymm0
//      aa9:	fc ff ff 

bind(L_e_aac);
//      aac:	8b 85 d4 fc ff ff    	mov    eax,DWORD PTR [rbp-0x32c]
//      ab2:	83 f8 1f             	cmp    eax,0x1f
//      ab5:	0f 86 66 fc ff ff    	jbe    721 <extract_multiplier+0xe1>
//      abb:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
//      ac2:	fd ff ff 
//      ac5:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x50],ymm0
//      acc:	ff ff ff 
//      acf:	62 f1 fd 28 6f 85 b0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x250]
//      ad6:	fd ff ff 
//      ad9:	62 f1 fd 28 7f 85 d0 	vmovdqa64 YMMWORD PTR [rbp-0x30],ymm0
//      ae0:	ff ff ff 

// extern __inline __m256i
// __attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
// _mm256_xor_si256 (__m256i __A, __m256i __B)
// {
//   return (__m256i) ((__v4du)__A ^ (__v4du)__B);
//      ae3:	62 f1 fd 28 6f 8d b0 	vmovdqa64 ymm1,YMMWORD PTR [rbp-0x50]
//      aea:	ff ff ff 
//      aed:	62 f1 fd 28 6f 85 d0 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x30]
//      af4:	ff ff ff 
//      af7:	c5 f5 ef c0          	vpxor  ymm0,ymm1,ymm0
//     }

//     /* Clear index */
//     idx = xor64(idx, idx);
//      afb:	62 f1 fd 28 7f 85 b0 	vmovdqa64 YMMWORD PTR [rbp-0x250],ymm0
//      b02:	fd ff ff 

//     storeu64(&red_Y[4*0], t0);
//      b05:	62 f1 fd 28 6f 85 10 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2f0]
//      b0c:	fd ff ff 
//      b0f:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
//      b16:	48 89 c7             	mov    rdi,rax
//      b19:	e8 62 f8 ff ff       	call   380 <storeu64>
//     storeu64(&red_Y[4*1], t1);
//      b1e:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
//      b25:	48 83 c0 20          	add    rax,0x20
//      b29:	62 f1 fd 28 6f 85 30 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2d0]
//      b30:	fd ff ff 
//      b33:	48 89 c7             	mov    rdi,rax
//      b36:	e8 45 f8 ff ff       	call   380 <storeu64>
//     storeu64(&red_Y[4*2], t2);
//      b3b:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
//      b42:	48 83 c0 40          	add    rax,0x40
//      b46:	62 f1 fd 28 6f 85 50 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x2b0]
//      b4d:	fd ff ff 
//      b50:	48 89 c7             	mov    rdi,rax
//      b53:	e8 28 f8 ff ff       	call   380 <storeu64>
//     storeu64(&red_Y[4*3], t3);
//      b58:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
//      b5f:	48 83 c0 60          	add    rax,0x60
//      b63:	62 f1 fd 28 6f 85 70 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x290]
//      b6a:	fd ff ff 
//      b6d:	48 89 c7             	mov    rdi,rax
//      b70:	e8 0b f8 ff ff       	call   380 <storeu64>
//     storeu64(&red_Y[4*4], t4);
//      b75:	48 8b 85 b8 fc ff ff 	mov    rax,QWORD PTR [rbp-0x348]
//      b7c:	48 83 e8 80          	sub    rax,0xffffffffffffff80
//      b80:	62 f1 fd 28 6f 85 90 	vmovdqa64 ymm0,YMMWORD PTR [rbp-0x270]
//      b87:	fd ff ff 
//      b8a:	48 89 c7             	mov    rdi,rax
//      b8d:	e8 ee f7 ff ff       	call   380 <storeu64>
// }
//      b92:	90                   	nop
//      b93:	48 81 c4 68 03 00 00 	add    rsp,0x368
//      b9a:	41 5a                	pop    r10
//      b9c:	5d                   	pop    rbp
//      b9d:	49 8d 62 f8          	lea    rsp,[r10-0x8]
//      ba1:	c3                   	ret    
}


void MacroAssembler::ifmaExp52x20(Register a, Register b, Register c, Register d, Register e, Register f)
{
  // Windows regs    |  Linux regs
  // c_rarg0 (rcx)   |  c_rarg0 (rdi)  out
  // c_rarg1 (rdx)   |  c_rarg1 (rsi)  base
  // c_rarg2 (r8)    |  c_rarg2 (rdx)  exp
  // c_rarg3 (r9)    |  c_rarg3 (rcx)  mod
  // r10             |  c_rarg4 (r8)   toMont
  // r11             |  c_rarg5 (r9)   inv

  const Register out = c_rarg0;
  const Register base = c_rarg1;
  const Register exp = c_rarg2;
  const Register mod = c_rarg3;
  // and updated with the incremented counter in the end
#ifndef _WIN64
  const Register toMont = c_rarg4;
  const Register inv = c_rarg5;
#else
  const Address toMont_mem(rbp, 6 * wordSize);
  const Register toMont = r8;
  const Address inv_mem(rbp, 7 * wordSize);
  const Register inv = r9;
#endif
  enter();
 // Save state before entering routine
  push(r12);
  push(r13);
  push(r14);
  push(r15);
  push(rbx);
  push(rsi);
  push(rdi);
#ifdef _WIN64
  // on win64, fill args from stack position
  movptr(rdi, rcx);
  movptr(rsi, rdx);
  movptr(rdx, r8);
  movptr(rcx, r9);
  movptr(toMont, toMont_mem);
  movptr(inv, inv_mem);
#endif
  push(rbp);
  movq(rbp, rsp);

// 0000000000000bc0 <k1_ifma256_exp52x20>:
//                                  const Ipp64u *base,
//                                  const Ipp64u *exp,
//                                  const Ipp64u *modulus,
//                                  const Ipp64u *toMont,
//                                  const Ipp64u k0))
// {
//      bc0:	f3 0f 1e fa          	endbr64 
//      bc4:	55                   	push   rbp
//      bc5:	48 89 e5             	mov    rbp,rsp
//      bc8:	48 83 e4 c0          	and    rsp,0xffffffffffffffc0
//      bcc:	48 81 ec 00 10 00 00 	sub    rsp,0x1000
//      bd3:	48 83 0c 24 00       	or     QWORD PTR [rsp],0x0
//      bd8:	48 81 ec c0 06 00 00 	sub    rsp,0x6c0
//      bdf:	48 89 7c 24 28       	mov    QWORD PTR [rsp+0x28],rdi
//      be4:	48 89 74 24 20       	mov    QWORD PTR [rsp+0x20],rsi
//      be9:	48 89 54 24 18       	mov    QWORD PTR [rsp+0x18],rdx
//      bee:	48 89 4c 24 10       	mov    QWORD PTR [rsp+0x10],rcx
//      bf3:	4c 89 44 24 08       	mov    QWORD PTR [rsp+0x8],r8
//      bf8:	4c 89 0c 24          	mov    QWORD PTR [rsp],r9

  // andq(rsp, 0xffffffffffffffc0);
  subq(rsp, 0x16c0);
  movq(Address(rsp, 0x28), rdi);
  movq(Address(rsp, 0x20), rsi);
  movq(Address(rsp, 0x18), rdx);
  movq(Address(rsp, 0x10), rcx);
  movq(Address(rsp, 0x08), r8);
  movq(Address(rsp, 0x00), r9);
  

//    /* pre-computed table of base powers */
//    __ALIGN64 Ipp64u red_table[1U << EXP_WIN_SIZE][LEN52];

//    /* zero all temp buffers (due to zero padding) */
//    ZEXPAND_BNU(red_Y, 0, LEN52);
//      bfc:	c7 44 24 3c 00 00 00 	mov    DWORD PTR [rsp+0x3c],0x0
//      c03:	00 
//      c04:	eb 16                	jmp    c1c <k1_ifma256_exp52x20+0x5c>
  Label L_c1c, L_c06;
//      c06:	8b 44 24 3c          	mov    eax,DWORD PTR [rsp+0x3c]
//      c0a:	48 98                	cdqe   
//      c0c:	48 c7 84 c4 40 01 00 	mov    QWORD PTR [rsp+rax*8+0x140],0x0
//      c13:	00 00 00 00 00 
//      c18:	ff 44 24 3c          	inc    DWORD PTR [rsp+0x3c]
//      c1c:	83 7c 24 3c 13       	cmp    DWORD PTR [rsp+0x3c],0x13
//      c21:	7e e3                	jle    c06 <k1_ifma256_exp52x20+0x46>

  movl(Address(rsp, 0x3c), 0x0);
  jmp(L_c1c);
  bind(L_c06);
  movl(rax, Address(rsp, 0x3c));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x98);
  movq(Address(rsp, rax, Address::times_8, 0x140), 0x00);
  incl(Address(rsp, 0x3c));
  bind(L_c1c);
  cmpl(Address(rsp, 0x3c), 0x13);
  jcc(Assembler::lessEqual, L_c06);
//    ZEXPAND_BNU((Ipp64u*)red_table, 0, LEN52 * (1U << EXP_WIN_SIZE));
//      c23:	c7 44 24 40 00 00 00 	mov    DWORD PTR [rsp+0x40],0x0
//      c2a:	00 
//      c2b:	eb 24                	jmp    c51 <k1_ifma256_exp52x20+0x91>
//      c2d:	8b 44 24 40          	mov    eax,DWORD PTR [rsp+0x40]
//      c31:	48 98                	cdqe   
//      c33:	48 8d 14 c5 00 00 00 	lea    rdx,[rax*8+0x0]
//      c3a:	00 
//      c3b:	48 8d 84 24 c0 02 00 	lea    rax,[rsp+0x2c0]
//      c42:	00 
//      c43:	48 01 d0             	add    rax,rdx
//      c46:	48 c7 00 00 00 00 00 	mov    QWORD PTR [rax],0x0
//      c4d:	ff 44 24 40          	inc    DWORD PTR [rsp+0x40]
//      c51:	8b 44 24 40          	mov    eax,DWORD PTR [rsp+0x40]
//      c55:	3d 7f 02 00 00       	cmp    eax,0x27f
//      c5a:	76 d1                	jbe    c2d <k1_ifma256_exp52x20+0x6d>
  Label L_c51, L_c2d;
  movl(Address(rsp, 0x40), 0x0);
  jmp(L_c51);
  bind(L_c2d);
  movl(rax, Address(rsp, 0x40));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x98);
  lea(rdx, Address(rax, 0, Address::times_8));
  lea(rax, Address(rsp, 0x2c0));
  addptr(rax, rdx);
  movq(Address(rax, 0), 0x00);
  incl(Address(rsp, 0x40));
  bind(L_c51);
  movl(rax, Address(rsp, 0x40));
  cmpl(rax, 0x27f);
  jcc(Assembler::lessEqual, L_c2d);
//    ZEXPAND_BNU(red_X, 0, LEN52); /* table[0] = mont(x^0) = mont(1) */
//      c5c:	c7 44 24 44 00 00 00 	mov    DWORD PTR [rsp+0x44],0x0
//      c63:	00 
//      c64:	eb 16                	jmp    c7c <k1_ifma256_exp52x20+0xbc>
//      c66:	8b 44 24 44          	mov    eax,DWORD PTR [rsp+0x44]
//      c6a:	48 98                	cdqe   
//      c6c:	48 c7 84 c4 00 02 00 	mov    QWORD PTR [rsp+rax*8+0x200],0x0
//      c73:	00 00 00 00 00 
//      c78:	ff 44 24 44          	inc    DWORD PTR [rsp+0x44]
//      c7c:	83 7c 24 44 13       	cmp    DWORD PTR [rsp+0x44],0x13
//      c81:	7e e3                	jle    c66 <k1_ifma256_exp52x20+0xa6>
  Label L_c66, L_c7c;
  movl(Address(rsp, 0x44), 0x0);
  jmp(L_c7c);
  bind(L_c66);
  movl(rax, Address(rsp, 0x44));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x98);
  movq(Address(rsp, rax, Address::times_8, 0x200), 0x00);
  incl(Address(rsp, 0x44));
  bind(L_c7c);
  cmpl(Address(rsp, 0x44), 0x13);
  jcc(Assembler::lessEqual, L_c66);
//    int idx;

//    /*
//    // compute table of powers base^i, i=0, ..., (2^EXP_WIN_SIZE) -1
//    */
//    red_X[0] = 1ULL;
//      c83:	48 c7 84 24 00 02 00 	mov    QWORD PTR [rsp+0x200],0x1
//      c8a:	00 01 00 00 00 
//    AMM(red_table[0], red_X, toMont, modulus, k0);
//      c8f:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
//      c93:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
//      c98:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
//      c9d:	48 8d b4 24 00 02 00 	lea    rsi,[rsp+0x200]
//      ca4:	00 
//      ca5:	48 8d 84 24 c0 02 00 	lea    rax,[rsp+0x2c0]
//      cac:	00 
//      cad:	49 89 f8             	mov    r8,rdi
//      cb0:	48 89 c7             	mov    rdi,rax
//      cb3:	e8 00 00 00 00       	call   cb8 <k1_ifma256_exp52x20+0xf8>  // ASG AMM

  movq(Address(rsp, 0x200), 0x01);
  movptr(rdi, Address(rsp, 0x00));
  movptr(rcx, Address(rsp, 0x10));
  movptr(rdx, Address(rsp, 0x08));
  lea(rsi, Address(rsp, 0x200));
  lea(rax, Address(rsp, 0x2c0));
  movptr(r8, rdi);
  movptr(rdi, rax);
  montgomeryMultiply52x20(rdi, rsi, rdx, rcx, r8);
//  call ????
//    AMM(red_table[1], base, toMont, modulus, k0);
//      cb8:	4c 8b 04 24          	mov    r8,QWORD PTR [rsp]
//      cbc:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
//      cc1:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
//      cc6:	48 8b 44 24 20       	mov    rax,QWORD PTR [rsp+0x20]
//      ccb:	48 8d b4 24 c0 02 00 	lea    rsi,[rsp+0x2c0]
//      cd2:	00 
//      cd3:	48 8d be a0 00 00 00 	lea    rdi,[rsi+0xa0]
//      cda:	48 89 c6             	mov    rsi,rax
//      cdd:	e8 00 00 00 00       	call   ce2 <k1_ifma256_exp52x20+0x122>  // ASG AMM

  movptr(r8, Address(rsp, 0x00));
  movptr(rcx, Address(rsp, 0x10));
  movptr(rdx, Address(rsp, 0x08));
  movptr(rax, Address(rsp, 0x20));
  lea(rsi, Address(rsp, 0x2c0));
  lea(rdi, Address(rsi, 0x0a0));
  movptr(rsi, rax);
  montgomeryMultiply52x20(rdi, rsi, rdx, rcx, r8);

//    for (idx = 1; idx < (1U << EXP_WIN_SIZE)/2; idx++) {
//      ce2:	c7 44 24 48 01 00 00 	mov    DWORD PTR [rsp+0x48],0x1
//      ce9:	00 
//      cea:	e9 c7 00 00 00       	jmp    db6 <k1_ifma256_exp52x20+0x1f6>

  Label L_db6, L_cef;
  movl(Address(rsp, 0x48), 0x01);
  jmp(L_db6);
  bind(L_cef);
//       AMS(red_table[2*idx],   red_table[idx],  modulus, k0);
//      cef:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//      cf6:	00 
//      cf7:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
//      cfb:	48 63 d0             	movsxd rdx,eax
//      cfe:	48 89 d0             	mov    rax,rdx
//      d01:	48 c1 e0 02          	shl    rax,0x2
//      d05:	48 01 d0             	add    rax,rdx
//      d08:	48 c1 e0 05          	shl    rax,0x5
//      d0c:	48 8d 34 01          	lea    rsi,[rcx+rax*1]
//      d10:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
//      d14:	01 c0                	add    eax,eax
//      d16:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//      d1d:	00 
//      d1e:	48 63 d0             	movsxd rdx,eax
//      d21:	48 89 d0             	mov    rax,rdx
//      d24:	48 c1 e0 02          	shl    rax,0x2
//      d28:	48 01 d0             	add    rax,rdx
//      d2b:	48 c1 e0 05          	shl    rax,0x5
//      d2f:	48 8d 3c 01          	lea    rdi,[rcx+rax*1]
//      d33:	48 8b 14 24          	mov    rdx,QWORD PTR [rsp]
//      d37:	48 8b 44 24 10       	mov    rax,QWORD PTR [rsp+0x10]
//      d3c:	48 89 d1             	mov    rcx,rdx
//      d3f:	48 89 c2             	mov    rdx,rax
//      d42:	e8 00 00 00 00       	call   d47 <k1_ifma256_exp52x20+0x187>  // ASG - AMS
  lea(rcx, Address(rsp, 0x2c0));
  movl(rax, Address(rsp, 0x48));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x63);
  emit_int8((unsigned char) 0xd0);
  //movswl(rdx, rax);
  movq(rax, rdx);
  shlq(rax, 0x2);
  addq(rax, rdx);
  shlq(rax, 0x5);
  lea(rsi, Address(rcx, rax, Address::times_1));
  movl(rax, Address(rsp, 0x48));
  addl(rax, rax);
  lea(rcx, Address(rsp, 0x2c0));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x63);
  emit_int8((unsigned char) 0xd0);
  //movswl(rdx, rax);
  movq(rax, rdx);
  shlq(rax, 2);
  addq(rax, rdx);
  shlq(rax, 0x5);
  lea(rdi, Address(rcx, rax, Address::times_1));
  movq(rdx, Address(rsp, 0));
  movq(rax, Address(rsp, 0x10));
  movq(rcx, rdx);
  movq(rdx, rax);
  montgomerySquare52x20(rdi, rsi, rdx, rcx);

//       AMM(red_table[2*idx+1], red_table[2*idx],red_table[1], modulus, k0);
//      d47:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
//      d4b:	01 c0                	add    eax,eax
//      d4d:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//      d54:	00 
//      d55:	48 63 d0             	movsxd rdx,eax
//      d58:	48 89 d0             	mov    rax,rdx
//      d5b:	48 c1 e0 02          	shl    rax,0x2
//      d5f:	48 01 d0             	add    rax,rdx
//      d62:	48 c1 e0 05          	shl    rax,0x5
//      d66:	48 8d 34 01          	lea    rsi,[rcx+rax*1]
//      d6a:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
//      d6e:	01 c0                	add    eax,eax
//      d70:	ff c0                	inc    eax
//      d72:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//      d79:	00 
//      d7a:	48 63 d0             	movsxd rdx,eax
//      d7d:	48 89 d0             	mov    rax,rdx
//      d80:	48 c1 e0 02          	shl    rax,0x2
//      d84:	48 01 d0             	add    rax,rdx
//      d87:	48 c1 e0 05          	shl    rax,0x5
//      d8b:	48 8d 3c 01          	lea    rdi,[rcx+rax*1]
//      d8f:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      d93:	48 8b 44 24 10       	mov    rax,QWORD PTR [rsp+0x10]
//      d98:	48 8d 94 24 c0 02 00 	lea    rdx,[rsp+0x2c0]
//      d9f:	00 
//      da0:	48 81 c2 a0 00 00 00 	add    rdx,0xa0
//      da7:	49 89 c8             	mov    r8,rcx
//      daa:	48 89 c1             	mov    rcx,rax
//      dad:	e8 00 00 00 00       	call   db2 <k1_ifma256_exp52x20+0x1f2>  // ASG AMM
//    for (idx = 1; idx < (1U << EXP_WIN_SIZE)/2; idx++) {
//      db2:	ff 44 24 48          	inc    DWORD PTR [rsp+0x48]
  movl(rax, Address(rsp, 0x48));
  addl(rax, rax);
  lea(rcx, Address(rsp, 0x2c0));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x63);
  emit_int8((unsigned char) 0xd0);
//  movswl(rdx, rax);
  movq(rax, rdx);
  shlq(rax, 2);
  addq(rax, rdx);
  shlq(rax, 0x5);
  lea(rsi, Address(rax, rax, Address::times_1));
  movl(rax, Address(rsp, 0x48));
  addq(rax, rax);
  incq(rax);
  lea(rcx, Address(rsp, 0x2c0));
  emit_int8((unsigned char) 0x48);
  emit_int8((unsigned char) 0x63);
  emit_int8((unsigned char) 0xd0);
//  movswl(rdx, rax);
  movq(rax, rdx);
  shlq(rax, 2);
  addq(rax, rdx);
  shlq(rax, 0x5);
  lea(rdi, Address(rcx, rax, Address::times_1));
  movq(rcx, Address(rsp, 0));
  movq(rax, Address(rsp, 0x10));
  lea(rdx, Address(rsp, 0x2c0));
  addq(rdx, 0xa0);
  movq(r8, rdx);
  movq(rcx, rax);
  montgomeryMultiply52x20(rdi, rsi, rdx, rcx, r8);

  incl(Address(rsp, 0x48));
  bind(L_db6);
//      db6:	8b 44 24 48          	mov    eax,DWORD PTR [rsp+0x48]
//      dba:	83 f8 0f             	cmp    eax,0xf
//      dbd:	0f 86 2c ff ff ff    	jbe    cef <k1_ifma256_exp52x20+0x12f>
  movl(rax, Address(rsp, 0x48));
  cmpl(rax, 0x0f);
  jcc(Assembler::below, L_cef);
//    }

//    /* copy and expand exponents */
//    ZEXPAND_COPY_BNU(expz, LEN64+1, exp, LEN64);
//      dc3:	c7 44 24 4c 00 00 00 	mov    DWORD PTR [rsp+0x4c],0x0
//      dca:	00 
//      dcb:	eb 2b                	jmp    df8 <k1_ifma256_exp52x20+0x238>
//      dcd:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
//      dd1:	48 98                	cdqe   
//      dd3:	48 8d 14 c5 00 00 00 	lea    rdx,[rax*8+0x0]
//      dda:	00 
//      ddb:	48 8b 44 24 18       	mov    rax,QWORD PTR [rsp+0x18]
//      de0:	48 01 d0             	add    rax,rdx
//      de3:	48 8b 10             	mov    rdx,QWORD PTR [rax]
//      de6:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
//      dea:	48 98                	cdqe   
//      dec:	48 89 94 c4 80 00 00 	mov    QWORD PTR [rsp+rax*8+0x80],rdx
//      df3:	00 
//      df4:	ff 44 24 4c          	inc    DWORD PTR [rsp+0x4c]
//      df8:	83 7c 24 4c 0f       	cmp    DWORD PTR [rsp+0x4c],0xf
//      dfd:	7e ce                	jle    dcd <k1_ifma256_exp52x20+0x20d>
//      dff:	eb 16                	jmp    e17 <k1_ifma256_exp52x20+0x257>
//      e01:	8b 44 24 4c          	mov    eax,DWORD PTR [rsp+0x4c]
//      e05:	48 98                	cdqe   
//      e07:	48 c7 84 c4 80 00 00 	mov    QWORD PTR [rsp+rax*8+0x80],0x0
//      e0e:	00 00 00 00 00 
//      e13:	ff 44 24 4c          	inc    DWORD PTR [rsp+0x4c]
//      e17:	83 7c 24 4c 10       	cmp    DWORD PTR [rsp+0x4c],0x10
//      e1c:	7e e3                	jle    e01 <k1_ifma256_exp52x20+0x241>

//    /* exponentiation */
//    {
//       int rem = BITSIZE_MODULUS % EXP_WIN_SIZE;
//      e1e:	c7 44 24 58 04 00 00 	mov    DWORD PTR [rsp+0x58],0x4
//      e25:	00 
//       int delta = rem ? rem : EXP_WIN_SIZE;
//      e26:	83 7c 24 58 00       	cmp    DWORD PTR [rsp+0x58],0x0
//      e2b:	74 06                	je     e33 <k1_ifma256_exp52x20+0x273>
//      e2d:	8b 44 24 58          	mov    eax,DWORD PTR [rsp+0x58]
//      e31:	eb 05                	jmp    e38 <k1_ifma256_exp52x20+0x278>
//      e33:	b8 05 00 00 00       	mov    eax,0x5
//      e38:	89 44 24 5c          	mov    DWORD PTR [rsp+0x5c],eax
//       Ipp64u table_idx_mask = EXP_WIN_MASK;
//      e3c:	48 c7 44 24 68 1f 00 	mov    QWORD PTR [rsp+0x68],0x1f
//      e43:	00 00 

//       int exp_bit_no = BITSIZE_MODULUS - delta;
//      e45:	b8 00 04 00 00       	mov    eax,0x400
//      e4a:	2b 44 24 5c          	sub    eax,DWORD PTR [rsp+0x5c]
//      e4e:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
//       int exp_chunk_no = exp_bit_no / 64;
//      e52:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//      e56:	8d 50 3f             	lea    edx,[rax+0x3f]
//      e59:	85 c0                	test   eax,eax
//      e5b:	0f 48 c2             	cmovs  eax,edx
//      e5e:	c1 f8 06             	sar    eax,0x6
//      e61:	89 44 24 60          	mov    DWORD PTR [rsp+0x60],eax
//       int exp_chunk_shift = exp_bit_no % 64;
//      e65:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//      e69:	99                   	cdq    
//      e6a:	c1 ea 1a             	shr    edx,0x1a
//      e6d:	01 d0                	add    eax,edx
//      e6f:	83 e0 3f             	and    eax,0x3f
//      e72:	29 d0                	sub    eax,edx
//      e74:	89 44 24 64          	mov    DWORD PTR [rsp+0x64],eax

//       /* process 1-st exp window - just init result */
//       Ipp64u red_table_idx = expz[exp_chunk_no];
//      e78:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
//      e7c:	48 98                	cdqe   
//      e7e:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
//      e85:	00 
//      e86:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax
//       red_table_idx = red_table_idx >> exp_chunk_shift;
//      e8b:	8b 44 24 64          	mov    eax,DWORD PTR [rsp+0x64]
//      e8f:	89 c1                	mov    ecx,eax
//      e91:	48 d3 6c 24 70       	shr    QWORD PTR [rsp+0x70],cl

//       extract_multiplier(red_Y, (const Ipp64u(*)[LEN52])red_table, (int)red_table_idx);
//      e96:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
//      e9b:	89 c2                	mov    edx,eax
//      e9d:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//      ea4:	00 
//      ea5:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      eac:	00 
//      ead:	48 89 ce             	mov    rsi,rcx
//      eb0:	48 89 c7             	mov    rdi,rax
//      eb3:	e8 88 f7 ff ff       	call   640 <extract_multiplier>

//       /* process other exp windows */
//       for (exp_bit_no -= EXP_WIN_SIZE; exp_bit_no >= 0; exp_bit_no -= EXP_WIN_SIZE) {
//      eb8:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//      ebc:	83 e8 05             	sub    eax,0x5
//      ebf:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
//      ec3:	e9 91 01 00 00       	jmp    1059 <k1_ifma256_exp52x20+0x499>
//          /* series of squaring */
//          AMS(red_Y, red_Y, modulus, k0);
//      ec8:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      ecc:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
//      ed1:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//      ed8:	00 
//      ed9:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      ee0:	00 
//      ee1:	48 89 c7             	mov    rdi,rax
//      ee4:	e8 00 00 00 00       	call   ee9 <k1_ifma256_exp52x20+0x329>  // ASG - AMS
//          AMS(red_Y, red_Y, modulus, k0);
//      ee9:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      eed:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
//      ef2:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//      ef9:	00 
//      efa:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      f01:	00 
//      f02:	48 89 c7             	mov    rdi,rax
//      f05:	e8 00 00 00 00       	call   f0a <k1_ifma256_exp52x20+0x34a>  // ASG - AMS
//          AMS(red_Y, red_Y, modulus, k0);
//      f0a:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      f0e:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
//      f13:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//      f1a:	00 
//      f1b:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      f22:	00 
//      f23:	48 89 c7             	mov    rdi,rax
//      f26:	e8 00 00 00 00       	call   f2b <k1_ifma256_exp52x20+0x36b>  // ASG - AMS
//          AMS(red_Y, red_Y, modulus, k0);
//      f2b:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      f2f:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
//      f34:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//      f3b:	00 
//      f3c:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      f43:	00 
//      f44:	48 89 c7             	mov    rdi,rax
//      f47:	e8 00 00 00 00       	call   f4c <k1_ifma256_exp52x20+0x38c>  // ASG - AMS
//          AMS(red_Y, red_Y, modulus, k0);
//      f4c:	48 8b 0c 24          	mov    rcx,QWORD PTR [rsp]
//      f50:	48 8b 54 24 10       	mov    rdx,QWORD PTR [rsp+0x10]
//      f55:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//      f5c:	00 
//      f5d:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//      f64:	00 
//      f65:	48 89 c7             	mov    rdi,rax
//      f68:	e8 00 00 00 00       	call   f6d <k1_ifma256_exp52x20+0x3ad>  // ASG - AMS

//          /* extract pre-computed multiplier from the table */
//          {
//             Ipp64u T;
//             exp_chunk_no = exp_bit_no / 64;
//      f6d:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//      f71:	8d 50 3f             	lea    edx,[rax+0x3f]
//      f74:	85 c0                	test   eax,eax
//      f76:	0f 48 c2             	cmovs  eax,edx
//      f79:	c1 f8 06             	sar    eax,0x6
//      f7c:	89 44 24 60          	mov    DWORD PTR [rsp+0x60],eax
//             exp_chunk_shift = exp_bit_no % 64;
//      f80:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//      f84:	99                   	cdq    
//      f85:	c1 ea 1a             	shr    edx,0x1a
//      f88:	01 d0                	add    eax,edx
//      f8a:	83 e0 3f             	and    eax,0x3f
//      f8d:	29 d0                	sub    eax,edx
//      f8f:	89 44 24 64          	mov    DWORD PTR [rsp+0x64],eax

//             red_table_idx = expz[exp_chunk_no];
//      f93:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
//      f97:	48 98                	cdqe   
//      f99:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
//      fa0:	00 
//      fa1:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax
//             T = expz[exp_chunk_no+1];
//      fa6:	8b 44 24 60          	mov    eax,DWORD PTR [rsp+0x60]
//      faa:	ff c0                	inc    eax
//      fac:	48 98                	cdqe   
//      fae:	48 8b 84 c4 80 00 00 	mov    rax,QWORD PTR [rsp+rax*8+0x80]
//      fb5:	00 
//      fb6:	48 89 44 24 78       	mov    QWORD PTR [rsp+0x78],rax

//             red_table_idx = red_table_idx >> exp_chunk_shift;
//      fbb:	8b 44 24 64          	mov    eax,DWORD PTR [rsp+0x64]
//      fbf:	89 c1                	mov    ecx,eax
//      fc1:	48 d3 6c 24 70       	shr    QWORD PTR [rsp+0x70],cl
//             T = exp_chunk_shift == 0 ? 0 : T << (64 - exp_chunk_shift);
//      fc6:	83 7c 24 64 00       	cmp    DWORD PTR [rsp+0x64],0x0
//      fcb:	74 15                	je     fe2 <k1_ifma256_exp52x20+0x422>
//      fcd:	b8 40 00 00 00       	mov    eax,0x40
//      fd2:	2b 44 24 64          	sub    eax,DWORD PTR [rsp+0x64]
//      fd6:	48 8b 54 24 78       	mov    rdx,QWORD PTR [rsp+0x78]
//      fdb:	c4 e2 f9 f7 c2       	shlx   rax,rdx,rax
//      fe0:	eb 05                	jmp    fe7 <k1_ifma256_exp52x20+0x427>
//      fe2:	b8 00 00 00 00       	mov    eax,0x0
//      fe7:	48 89 44 24 78       	mov    QWORD PTR [rsp+0x78],rax
//             red_table_idx = (red_table_idx ^ T) & table_idx_mask;
//      fec:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
//      ff1:	48 33 44 24 78       	xor    rax,QWORD PTR [rsp+0x78]
//      ff6:	48 23 44 24 68       	and    rax,QWORD PTR [rsp+0x68]
//      ffb:	48 89 44 24 70       	mov    QWORD PTR [rsp+0x70],rax

//             extract_multiplier(red_X, (const Ipp64u(*)[LEN52])red_table, (int)red_table_idx);
//     1000:	48 8b 44 24 70       	mov    rax,QWORD PTR [rsp+0x70]
//     1005:	89 c2                	mov    edx,eax
//     1007:	48 8d 8c 24 c0 02 00 	lea    rcx,[rsp+0x2c0]
//     100e:	00 
//     100f:	48 8d 84 24 00 02 00 	lea    rax,[rsp+0x200]
//     1016:	00 
//     1017:	48 89 ce             	mov    rsi,rcx
//     101a:	48 89 c7             	mov    rdi,rax
//     101d:	e8 1e f6 ff ff       	call   640 <extract_multiplier>
//             AMM(red_Y, red_Y, red_X, modulus, k0);
//     1022:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
//     1026:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
//     102b:	48 8d 94 24 00 02 00 	lea    rdx,[rsp+0x200]
//     1032:	00 
//     1033:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//     103a:	00 
//     103b:	48 8d 84 24 40 01 00 	lea    rax,[rsp+0x140]
//     1042:	00 
//     1043:	49 89 f8             	mov    r8,rdi
//     1046:	48 89 c7             	mov    rdi,rax
//     1049:	e8 00 00 00 00       	call   104e <k1_ifma256_exp52x20+0x48e>  // ASG extract_multiplier
//       for (exp_bit_no -= EXP_WIN_SIZE; exp_bit_no >= 0; exp_bit_no -= EXP_WIN_SIZE) {
//     104e:	8b 44 24 50          	mov    eax,DWORD PTR [rsp+0x50]
//     1052:	83 e8 05             	sub    eax,0x5
//     1055:	89 44 24 50          	mov    DWORD PTR [rsp+0x50],eax
//     1059:	83 7c 24 50 00       	cmp    DWORD PTR [rsp+0x50],0x0
//     105e:	0f 89 64 fe ff ff    	jns    ec8 <k1_ifma256_exp52x20+0x308>
//          }
//       }
//    }

//    /* clear exponents */
//    PurgeBlock((Ipp64u*)expz, (LEN64+1)*(int)sizeof(Ipp64u));
//     1064:	48 8d 84 24 80 00 00 	lea    rax,[rsp+0x80]
//     106b:	00 
//     106c:	be 88 00 00 00       	mov    esi,0x88
//     1071:	48 89 c7             	mov    rdi,rax
//     1074:	e8 00 00 00 00       	call   1079 <k1_ifma256_exp52x20+0x4b9> // ASG PurgeBlock

//    /* convert result back in regular 2^52 domain */
//    ZEXPAND_BNU(red_X, 0, LEN52);
//     1079:	c7 44 24 54 00 00 00 	mov    DWORD PTR [rsp+0x54],0x0
//     1080:	00 
//     1081:	eb 16                	jmp    1099 <k1_ifma256_exp52x20+0x4d9>
//     1083:	8b 44 24 54          	mov    eax,DWORD PTR [rsp+0x54]
//     1087:	48 98                	cdqe   
//     1089:	48 c7 84 c4 00 02 00 	mov    QWORD PTR [rsp+rax*8+0x200],0x0
//     1090:	00 00 00 00 00 
//     1095:	ff 44 24 54          	inc    DWORD PTR [rsp+0x54]
//     1099:	83 7c 24 54 13       	cmp    DWORD PTR [rsp+0x54],0x13
//     109e:	7e e3                	jle    1083 <k1_ifma256_exp52x20+0x4c3>
//    red_X[0] = 1ULL;
//     10a0:	48 c7 84 24 00 02 00 	mov    QWORD PTR [rsp+0x200],0x1
//     10a7:	00 01 00 00 00 
//    AMM(out, red_Y, red_X, modulus, k0);
//     10ac:	48 8b 3c 24          	mov    rdi,QWORD PTR [rsp]
//     10b0:	48 8b 4c 24 10       	mov    rcx,QWORD PTR [rsp+0x10]
//     10b5:	48 8d 94 24 00 02 00 	lea    rdx,[rsp+0x200]
//     10bc:	00 
//     10bd:	48 8d b4 24 40 01 00 	lea    rsi,[rsp+0x140]
//     10c4:	00 
//     10c5:	48 8b 44 24 28       	mov    rax,QWORD PTR [rsp+0x28]
//     10ca:	49 89 f8             	mov    r8,rdi
//     10cd:	48 89 c7             	mov    rdi,rax
//     10d0:	e8 00 00 00 00       	call   10d5 <k1_ifma256_exp52x20+0x515>  // ASG AMM
// }
//     10d5:	90                   	nop
//     10d6:	c9                   	leave  
//     10d7:	c3                   	ret    
}
#endif

