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
void MacroAssembler::montgomeryMultiply52x20(Register out, Register kk0)
{
	const int LIMIT = 5552;
	const int BASE = 65521;
	const int CHUNKSIZE = 16;
	const int CHUNKSIZE_M1 = CHUNKSIZE - 1;

	const Register s = r11;
	const Register a_d = r12; // r12d
	const Register b_d = r8;  // r8d
	const Register end = r13;

	const XMMRegister ya = xmm0;
	const XMMRegister yb = xmm1;
	const XMMRegister ydata0 = xmm2;
	const XMMRegister ydata1 = xmm3;
	const XMMRegister ysa = xmm4;
	const XMMRegister ydata = ysa;
	const XMMRegister ytmp0 = ydata0;
	const XMMRegister ytmp1 = ydata1;
	const XMMRegister ytmp2 = xmm5;
	const XMMRegister xa = xmm0;
	const XMMRegister xb = xmm1;
	const XMMRegister xtmp0 = xmm2;
	const XMMRegister xtmp1 = xmm3;
	const XMMRegister xsa = xmm4;
	const XMMRegister xtmp2 = xmm5;

#if 0
   U64 R0 = get_zero64();
   U64 R1 = get_zero64();
   U64 R2 = get_zero64();

   /* High part of 512bit result in 256bit mode */
   U64 R0h = get_zero64();
   U64 R1h = get_zero64();
   U64 R2h = get_zero64();

   U64 Bi, Yi;

   Ipp64u m0 = m[0];
   Ipp64u a0 = a[0];
   Ipp64u acc0 = 0;

   int i;
   for (i=0; i<20; i++) {
      Ipp64u t0, t1, t2, yi;

      Bi = set64((long long)b[i]);               /* broadcast(b[i]) */
      /* compute yi */
      t0 = _mulx_u64(a0, b[i], &t2);             /* (t2:t0) = acc0 + a[0]*b[i] */
      ADD104(t2, acc0, 0, t0)
      yi = (acc0 * k0)  & EXP_DIGIT_MASK_AVX512; /* yi = acc0*k0 */
      Yi = set64((long long)yi);

      t0 = _mulx_u64(m0, yi, &t1);               /* (t1:t0)   = m0*yi     */
      ADD104(t2, acc0, t1, t0)                   /* (t2:acc0) += (t1:t0)  */
      acc0 = SHRD52(t2, acc0);

      fma52x8lo_mem(R0, R0, Bi, a,      64*0)
      fma52x8lo_mem(R1, R1, Bi, a,      64*1)
      fma52x8lo_mem_len(R2, R2, Bi, a,  64*2, 4)
      fma52x8lo_mem(R0, R0, Yi, m,      64*0)
      fma52x8lo_mem(R1, R1, Yi, m,      64*1)
      fma52x8lo_mem_len(R2, R2, Yi, m,  64*2, 4)

      shift64_imm(R0, R0h, 1)
      shift64_imm(R0h, R1, 1)
      shift64_imm(R1, R1h, 1)
      shift64_imm(R1h, R2, 1)
      shift64_imm(R2, get_zero64(), 1)

      /* "shift" R */
      t0 = get64(R0, 0);
      acc0 += t0;

      /* U = A*Bi (hi) */
      fma52x8hi_mem(R0, R0, Bi, a, 64*0)
      fma52x8hi_mem(R1, R1, Bi, a, 64*1)
      fma52x8hi_mem_len(R2, R2, Bi, a, 64*2, 4)
      /* R += M*Yi (hi) */
      fma52x8hi_mem(R0, R0, Yi, m, 64*0)
      fma52x8hi_mem(R1, R1, Yi, m, 64*1)
      fma52x8hi_mem_len(R2, R2, Yi, m, 64*2, 4)
   }

   /* Set R0[0] == acc0 */
   Bi = set64((long long)acc0);
   R0 = blend64(R0, Bi, 1);

   NORMALIZE_52x20(R0, R1, R2)

   storeu64(out + 0*4, R0);
   storeu64(out + 1*4, R0h);
   storeu64(out + 2*4, R1);
   storeu64(out + 3*4, R1h);
   storeu64(out + 4*4, R2);
#endif

   // On entry:
   //  out points to where the result should be stored (20 qwords)
   //  out + 40 qwords is a
   //  out + 80 qwords is b
   //  out + 120 qwords is m
   //  kk0 is the inverse (-(1/p) mod b)
   //  out + 160 is space for a qword (loop index)
   //
   // This routine uses all GP registers, and ymm0 - ymm12
   // Need to save r8, r14 and r13


  //  4:	55                   	push   rbp
  //  5:	c5 d1 ef ed          	vpxor  xmm5,xmm5,xmm5   // R1h
  //  9:	31 c0                	xor    eax,eax
  //  b:	48 89 e5             	mov    rbp,rsp
  //  e:	41 57                	push   r15
  // 10:	62 f1 fd 28 6f fd    	vmovdqa64 ymm7,ymm5     // R0h
  // 16:	62 f1 fd 28 6f e5    	vmovdqa64 ymm4,ymm5
  // 1c:	41 56                	push   r14
  // 1e:	62 f1 fd 28 6f f5    	vmovdqa64 ymm6,ymm5     // R1
  // 24:	62 f1 fd 28 6f c5    	vmovdqa64 ymm0,ymm5     // R0
  // 2a:	41 55                	push   r13
  // 2c:	49 b9 ff ff ff ff ff 	movabs r9,0xfffffffffffff
  // 33:	ff 0f 00 
  // 36:	62 71 fd 28 6f c5    	vmovdqa64 ymm8,ymm5
  // 3c:	41 54                	push   r12
  // 3e:	53                   	push   rbx
  // 3f:	48 8d 9a a0 00 00 00 	lea    rbx,[rdx+0xa0]
  // 46:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
  // 4a:	48 89 54 24 f8       	mov    QWORD PTR [rsp-0x8],rdx
  // 4f:	4c 8b 29             	mov    r13,QWORD PTR [rcx]
  // 52:	4c 8b 26             	mov    r12,QWORD PTR [rsi]
  push(r8);
  push(r14);
  push(r13);
  movq(r8, kk0);    // r8 gets inv
  vpxor(xmm5, xmm5, xmm5, Assembler::AVX_256bit);
  vmovdqu(xmm7, xmm5);
  vmovdqu(xmm4, xmm5);
  vmovdqu(xmm6, xmm5);
  vmovdqu(xmm0, xmm5);
  vmovdqu(xmm8, xmm5);
  mov64(r9, 0xfffffffffffff);
  lea(rdx, Address(out, 2 * 40 * wordSize));    // Points to b[0]
  lea(rbx, Address(out, 0xa0 + 2 * 40 * wordSize));    // Points to b[20] - loop terminator
#define LOOP_TERM Address(out, 4 * 40 * wordSize)
  movq(LOOP_TERM, rdx);
  lea(r13, Address(out, 3 * 40 * wordSize));    // Points to m[0]
  lea(r12, Address(out, 1 * 40 * wordSize));    // Points to a[0]
  xorq(rax, rax);

  align32();
  Label L_loop, L_mask;
  bind(L_loop);

  // 55:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
  // 5c:	00 00 00 00 
  // 60:	48 8b 54 24 f8       	mov    rdx,QWORD PTR [rsp-0x8]
  // 65:	62 f1 fd 28 6f cc    	vmovdqa64 ymm1,ymm4     // R2
  // 6b:	4c 8b 12             	mov    r10,QWORD PTR [rdx]
  // 6e:	4c 89 d2             	mov    rdx,r10
  // 71:	62 d2 fd 28 7c da    	vpbroadcastq ymm3,r10   // Bi
  // 77:	c4 42 ab f6 dc       	mulx   r11,r10,r12    // r11:r10 = rdx(b[i]) * r12(a[0])
  // 7c:	4c 01 d0             	add    rax,r10
  // 7f:	4d 89 de             	mov    r14,r11
  // 82:	49 89 c2             	mov    r10,rax
  // 85:	49 83 d6 00          	adc    r14,0x0
  // 89:	4d 0f af d0          	imul   r10,r8       // r8 is k0
  // 8d:	45 31 ff             	xor    r15d,r15d
  // 90:	4d 21 ca             	and    r10,r9
  // 93:	4c 89 d2             	mov    rdx,r10
  // 96:	62 d2 fd 28 7c d2    	vpbroadcastq ymm2,r10   // Yi
  // 9c:	c4 42 ab f6 dd       	mulx   r11,r10,r13
  // a1:	4c 01 d0             	add    rax,r10
  // a4:	41 0f 92 c7          	setb   r15b
  movq(rdx, LOOP_TERM);
  vmovdqu(xmm1, xmm4);
  movq(r10, Address(rdx, 0));
  movq(rdx, r10);
  evpbroadcastq(xmm3, r10, Assembler::AVX_256bit);
  mulxq(r11, r10, r12);
  addq(rax, r10);
  movq(r14, r11);
  movq(r10, rax);
  adcq(r14, 0);
  imulq(r10, r8);
  xorl(r15, r15);
  andq(r10, r9);
  movq(rdx, r10);
  evpbroadcastq(xmm2, r10, Assembler::AVX_256bit);
  mulxq(r11, r10, r13);
  addq(rax, r10);
  setb(Assembler::below, r15);

  // a8:	62 f2 e5 28 b4 06    	vpmadd52luq ymm0,ymm3,YMMWORD PTR [rsi]
  // ae:	62 f2 e5 28 b4 7e 01 	vpmadd52luq ymm7,ymm3,YMMWORD PTR [rsi+0x20]
  // b5:	62 f2 ed 28 b4 01    	vpmadd52luq ymm0,ymm2,YMMWORD PTR [rcx]
  // bb:	62 f2 ed 28 b4 79 01 	vpmadd52luq ymm7,ymm2,YMMWORD PTR [rcx+0x20]
  // c2:	62 f3 c5 28 03 c0 01 	valignq ymm0,ymm7,ymm0,0x1
  // c9:	48 83 44 24 f8 08    	add    QWORD PTR [rsp-0x8],0x8
  // cf:	62 f2 e5 28 b4 4e 04 	vpmadd52luq ymm1,ymm3,YMMWORD PTR [rsi+0x80]
  // d6:	62 f2 ed 28 b4 49 04 	vpmadd52luq ymm1,ymm2,YMMWORD PTR [rcx+0x80]
  // dd:	62 f3 bd 28 03 e1 01 	valignq ymm4,ymm8,ymm1,0x1
  vpmadd52luq(xmm0, xmm3, Address(rsi, 0), Assembler::AVX_256bit);
  vpmadd52luq(xmm7, xmm3, Address(rsi, 0x20), Assembler::AVX_256bit);
  vpmadd52luq(xmm0, xmm2, Address(rcx, 0), Assembler::AVX_256bit);
  vpmadd52luq(xmm7, xmm2, Address(rcx, 0x20), Assembler::AVX_256bit);
  evalignq(xmm0, xmm7, xmm0, 1, Assembler::AVX_256bit);
  addq(LOOP_TERM, 8);
  vpmadd52luq(xmm1, xmm3, Address(rsi, 0x80), Assembler::AVX_256bit);
  vpmadd52luq(xmm1, xmm2, Address(rcx, 0x80), Assembler::AVX_256bit);
  evalignq(xmm4, xmm8, xmm1, 1, Assembler::AVX_256bit);

  // e4:	4d 89 da             	mov    r10,r11
  // e7:	4d 01 f2             	add    r10,r14
  // ea:	4d 01 fa             	add    r10,r15
  // ed:	48 c1 e8 34          	shr    rax,0x34
  // f1:	49 c1 e2 0c          	shl    r10,0xc
  // f5:	48 8b 54 24 f8       	mov    rdx,QWORD PTR [rsp-0x8]
  // fa:	49 09 c2             	or     r10,rax
  // fd:	c4 e1 f9 7e c0       	vmovq  rax,xmm0
  movq(r10, r11);
  addq(r10, r14);
  addq(r10, r15);
  shrq(rax, 0x34);
  shlq(r10, 0xc);
  movq(rdx, LOOP_TERM);
  orq(r10, rax);
  movq(rax, xmm0);

//  102:	62 f2 e5 28 b4 76 02 	vpmadd52luq ymm6,ymm3,YMMWORD PTR [rsi+0x40]
//  109:	62 f2 e5 28 b4 6e 03 	vpmadd52luq ymm5,ymm3,YMMWORD PTR [rsi+0x60]
//  110:	62 f2 ed 28 b4 71 02 	vpmadd52luq ymm6,ymm2,YMMWORD PTR [rcx+0x40]
//  117:	62 f2 ed 28 b4 69 03 	vpmadd52luq ymm5,ymm2,YMMWORD PTR [rcx+0x60]
//  11e:	62 f3 cd 28 03 ff 01 	valignq ymm7,ymm6,ymm7,0x1
  vpmadd52luq(xmm6, xmm3, Address(rsi, 0x40), Assembler::AVX_256bit);
  vpmadd52luq(xmm5, xmm3, Address(rsi, 0x60), Assembler::AVX_256bit);
  vpmadd52luq(xmm6, xmm2, Address(rcx, 0x40), Assembler::AVX_256bit);
  vpmadd52luq(xmm5, xmm2, Address(rcx, 0x60), Assembler::AVX_256bit);
  evalignq(xmm7, xmm6, xmm7, 1, Assembler::AVX_256bit);

//  125:	62 f2 e5 28 b5 06    	vpmadd52huq ymm0,ymm3,YMMWORD PTR [rsi]
//  12b:	62 f3 d5 28 03 f6 01 	valignq ymm6,ymm5,ymm6,0x1
//  132:	62 f2 e5 28 b5 7e 01 	vpmadd52huq ymm7,ymm3,YMMWORD PTR [rsi+0x20]
//  139:	62 f3 f5 28 03 ed 01 	valignq ymm5,ymm1,ymm5,0x1
//  140:	62 f2 e5 28 b5 76 02 	vpmadd52huq ymm6,ymm3,YMMWORD PTR [rsi+0x40]
//  147:	62 f1 fd 28 6f cc    	vmovdqa64 ymm1,ymm4
//  14d:	62 f2 e5 28 b5 6e 03 	vpmadd52huq ymm5,ymm3,YMMWORD PTR [rsi+0x60]
//  154:	62 f2 e5 28 b5 4e 04 	vpmadd52huq ymm1,ymm3,YMMWORD PTR [rsi+0x80]
//  15b:	4c 01 d0             	add    rax,r10
//  15e:	62 f2 ed 28 b5 49 04 	vpmadd52huq ymm1,ymm2,YMMWORD PTR [rcx+0x80]
//  165:	62 f2 ed 28 b5 01    	vpmadd52huq ymm0,ymm2,YMMWORD PTR [rcx]
//  16b:	62 f1 fd 28 6f e1    	vmovdqa64 ymm4,ymm1
//  171:	62 f2 ed 28 b5 79 01 	vpmadd52huq ymm7,ymm2,YMMWORD PTR [rcx+0x20]
//  178:	62 f2 ed 28 b5 71 02 	vpmadd52huq ymm6,ymm2,YMMWORD PTR [rcx+0x40]
//  17f:	62 f2 ed 28 b5 69 03 	vpmadd52huq ymm5,ymm2,YMMWORD PTR [rcx+0x60]
  vpmadd52huq(xmm0, xmm3, Address(rsi, 0x00), Assembler::AVX_256bit);
  evalignq(xmm6, xmm5, xmm6, 1, Assembler::AVX_256bit);
  vpmadd52huq(xmm7, xmm3, Address(rsi, 0x20), Assembler::AVX_256bit);
  evalignq(xmm5, xmm1, xmm5, 1, Assembler::AVX_256bit);
  vpmadd52huq(xmm6, xmm3, Address(rsi, 0x40), Assembler::AVX_256bit);
  vmovdqu(xmm1, xmm4);
  vpmadd52huq(xmm5, xmm3, Address(rsi, 0x60), Assembler::AVX_256bit);
  vpmadd52huq(xmm1, xmm3, Address(rsi, 0x80), Assembler::AVX_256bit);
  addq(rax, r10);
  vpmadd52huq(xmm1, xmm2, Address(rcx, 0x80), Assembler::AVX_256bit);
  vpmadd52huq(xmm0, xmm2, Address(rcx, 0x00), Assembler::AVX_256bit);
  vmovdqu(xmm4, xmm1);
  vpmadd52huq(xmm7, xmm2, Address(rcx, 0x20), Assembler::AVX_256bit);
  vpmadd52huq(xmm6, xmm2, Address(rcx, 0x40), Assembler::AVX_256bit);
  vpmadd52huq(xmm5, xmm2, Address(rcx, 0x60), Assembler::AVX_256bit);

//  186:	48 39 d3             	cmp    rbx,rdx
//  189:	0f 85 d1 fe ff ff    	jne    60 <k1_ifma256_amm52x20+0x60>
  cmpq(rbx, rdx);
  jcc(Assembler::notEqual, L_loop);

// NORMALIZATION
//  18f:	c5 ed 73 d5 34       	vpsrlq ymm2,ymm5,0x34
//  194:	c5 b5 73 d7 34       	vpsrlq ymm9,ymm7,0x34
//  199:	c5 ad 73 d6 34       	vpsrlq ymm10,ymm6,0x34
//  19e:	62 d3 ed 28 03 da 03 	valignq ymm3,ymm2,ymm10,0x3
//  1a5:	62 f2 fd 28 7c c8    	vpbroadcastq ymm1,rax         // ymm1 = Bi, rax = acc0
//  1ab:	62 53 ad 28 03 d1 03 	valignq ymm10,ymm10,ymm9,0x3
//  1b2:	c4 e3 7d 02 c1 03    	vpblendd ymm0,ymm0,ymm1,0x3
//  1b8:	c5 a5 73 d0 34       	vpsrlq ymm11,ymm0,0x34
//  1bd:	c5 f5 73 d4 34       	vpsrlq ymm1,ymm4,0x34
//  1c2:	62 73 f5 28 03 e2 03 	valignq ymm12,ymm1,ymm2,0x3
//  1c9:	62 53 a5 28 03 c0 03 	valignq ymm8,ymm11,ymm8,0x3
  vpsrlq(xmm2, xmm5, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm9, xmm7, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm10, xmm6, 0x34, Assembler::AVX_256bit);
  evalignq(xmm3, xmm2, xmm10, 3, Assembler::AVX_256bit);
  evpbroadcastq(xmm1, rax, Assembler::AVX_256bit);
  evalignq(xmm10, xmm10, xmm9, 3, Assembler::AVX_256bit);
  vpblendd(xmm0, xmm0, xmm1, 0x3, Assembler::AVX_256bit),
  vpsrlq(xmm11, xmm0, 0x34, Assembler::AVX_256bit);
  vpsrlq(xmm1, xmm4, 0x34, Assembler::AVX_256bit);
  evalignq(xmm12, xmm1, xmm2, 3, Assembler::AVX_256bit);
  evalignq(xmm8, xmm11, xmm8, 3, Assembler::AVX_256bit);

//  1d0:	62 f1 fd 28 6f 15 00 	vmovdqa64 ymm2,YMMWORD PTR [rip+0x0]        # 1da <k1_ifma256_amm52x20+0x1da> // mask - low-order 52 bits set
//  1d7:	00 00 00 
//  1da:	62 53 b5 28 03 cb 03 	valignq ymm9,ymm9,ymm11,0x3
//  1e1:	c5 cd db f2          	vpand  ymm6,ymm6,ymm2
//  1e5:	c4 c1 4d d4 f2       	vpaddq ymm6,ymm6,ymm10
//  1ea:	62 f3 ed 28 1e f6 01 	vpcmpltuq k6,ymm2,ymm6   // 0x7fffe1042206
//  1f1:	c5 fd db c2          	vpand  ymm0,ymm0,ymm2
//  1f5:	c5 dd db ca          	vpand  ymm1,ymm4,ymm2
//  1f9:	c5 d5 db ea          	vpand  ymm5,ymm5,ymm2
//  1fd:	c4 c1 7d d4 c0       	vpaddq ymm0,ymm0,ymm8
//  202:	c4 c1 75 d4 cc       	vpaddq ymm1,ymm1,ymm12
//  207:	c5 d5 d4 eb          	vpaddq ymm5,ymm5,ymm3
//  20b:	62 f3 ed 28 1e c0 01 	vpcmpltuq k0,ymm2,ymm0
//  212:	62 f3 ed 28 1e cd 01 	vpcmpltuq k1,ymm2,ymm5
//  219:	62 f3 ed 28 1e e1 00 	vpcmpequq k4,ymm2,ymm1
//  220:	62 f3 ed 28 1e f9 01 	vpcmpltuq k7,ymm2,ymm1
  // vmovdqu(xmm2, ExternalAddress((address) L_mask));
  evpbroadcastq(xmm2, r9, Assembler::AVX_256bit);
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

//  227:	c5 f9 93 f6          	kmovb  esi,k6
//  22b:	62 f3 ed 28 1e f5 00 	vpcmpequq k6,ymm2,ymm5
//  232:	c5 c5 db fa          	vpand  ymm7,ymm7,ymm2
//  236:	62 f3 ed 28 1e d0 00 	vpcmpequq k2,ymm2,ymm0
//  23d:	62 f3 ed 28 1e de 00 	vpcmpequq k3,ymm2,ymm6
//  244:	c4 c1 45 d4 f9       	vpaddq ymm7,ymm7,ymm9
//  249:	62 f3 ed 28 1e ef 00 	vpcmpequq k5,ymm2,ymm7
//  250:	c5 79 93 c0          	kmovb  r8d,k0
//  254:	c5 f9 93 c7          	kmovb  eax,k7
//  258:	62 f3 ed 28 1e c7 01 	vpcmpltuq k0,ymm2,ymm7
  kmovbl(rsi, k6);
  evpcmpq(k6, knoreg, xmm2, xmm5, Assembler::overflow, false, Assembler::AVX_256bit);
  vpand(xmm7, xmm7, xmm2, Assembler::AVX_256bit);
  evpcmpq(k2, knoreg, xmm2, xmm0, Assembler::overflow, false, Assembler::AVX_256bit);
  evpcmpq(k3, knoreg, xmm2, xmm6, Assembler::overflow, false, Assembler::AVX_256bit);
  vpaddq(xmm7, xmm7, xmm9, Assembler::AVX_256bit);
  evpcmpq(k5, knoreg, xmm2, xmm7, Assembler::overflow, false, Assembler::AVX_256bit);
  kmovbl(r8, k0);
  kmovbl(rax, k7);
  evpcmpq(k0, knoreg, xmm2, xmm7, Assembler::noOverflow, false, Assembler::AVX_256bit);

//  25f:	c1 e0 10             	shl    eax,0x10
//  262:	c5 79 93 c9          	kmovb  r9d,k1
//  266:	c5 f9 93 d4          	kmovb  edx,k4
//  26a:	41 c1 e1 0c          	shl    r9d,0xc
//  26e:	c1 e2 10             	shl    edx,0x10
//  271:	c5 f9 93 de          	kmovb  ebx,k6
//  275:	c1 e3 0c             	shl    ebx,0xc
//  278:	09 da                	or     edx,ebx
//  27a:	44 09 c8             	or     eax,r9d
//  27d:	c5 79 93 e2          	kmovb  r12d,k2
//  281:	44 09 c0             	or     eax,r8d
//  284:	44 09 e2             	or     edx,r12d
//  287:	c1 e6 08             	shl    esi,0x8
//  28a:	c5 79 93 db          	kmovb  r11d,k3
//  28e:	41 c1 e3 08          	shl    r11d,0x8
//  292:	44 09 da             	or     edx,r11d
//  295:	09 f0                	or     eax,esi
//  297:	c5 f9 93 c8          	kmovb  ecx,k0
//  29b:	c5 79 93 d5          	kmovb  r10d,k5
//  29f:	c1 e1 04             	shl    ecx,0x4
//  2a2:	41 c1 e2 04          	shl    r10d,0x4
//  2a6:	44 09 d2             	or     edx,r10d
//  2a9:	09 c8                	or     eax,ecx
  shll(rax, 0x10);
  kmovbl(r9, k1);
  kmovbl(rdx, k4);
  shll(r9, 0xc);
  shll(rdx, 0x10);
  kmovbl(rbx, k6);
  shll(rbx, 0xc);
  orl(rdx, rbx);
  orl(rax, r9);
  kmovbl(r12, k2);
  orl(rax, r8);
  orl(rdx, r12);
  shll(rsi, 8);
  kmovbl(r11, k3);
  shll(r11, 8);
  orl(rdx, r11);
  orl(rax, rsi);
  kmovbl(rcx, k0);
  kmovbl(r10, k5);
  shll(rcx, 4);
  shll(r10, 4);
  orl(rdx, r10);
  orl(rax, rcx);

//  2ab:	8d 04 42             	lea    eax,[rdx+rax*2]  // compiles into lea eax,[edx+eax*2]
//  2ae:	31 c2                	xor    edx,eax
//  2b0:	0f b6 c6             	movzx  eax,dh
//  2b3:	c5 f9 92 d0          	kmovb  k2,eax
//  2b7:	89 d0                	mov    eax,edx
//  2b9:	c1 e8 10             	shr    eax,0x10
//  2bc:	c5 f9 92 d8          	kmovb  k3,eax
//  2c0:	89 d0                	mov    eax,edx
//  2c2:	c5 f9 92 ca          	kmovb  k1,edx
//  2c6:	c1 e8 04             	shr    eax,0x4
//  2c9:	c1 ea 0c             	shr    edx,0xc
  //  leal(rax, Address(rdx, rax, Address::times_2));
  emit_int8((unsigned char) 0x8d);
  emit_int8((unsigned char) 0x04);
  emit_int8((unsigned char) 0x42);
  xorl(rdx, rax);
  //movzwl(rax, rdx);
  //shrl(rax, 8);
  //andl(rax, 0xff);
  emit_int8((unsigned char) 0x0f);
  emit_int8((unsigned char) 0xb6);
  emit_int8((unsigned char) 0xc6);
  kmovbl(k2, rax);
  movl(rax, rdx);
  shrl(rax, 0x10);
  kmovbl(k3, rax);
  movl(rax, rdx);
  kmovbl(k1, rdx);
  shrl(rax, 4);
  shrl(rdx, 0xc);

//  2cc:	62 f1 fd 29 fb c2    	vpsubq ymm0{k1},ymm0,ymm2  // ??????????
//  2d2:	62 f1 cd 2a fb f2    	vpsubq ymm6{k2},ymm6,ymm2
//  2d8:	62 f1 f5 2b fb ca    	vpsubq ymm1{k3},ymm1,ymm2
//  2de:	c5 f9 92 e0          	kmovb  k4,eax
//  2e2:	c5 f9 92 ea          	kmovb  k5,edx
//  2e6:	62 f1 c5 2c fb fa    	vpsubq ymm7{k4},ymm7,ymm2
//  2ec:	62 f1 d5 2d fb ea    	vpsubq ymm5{k5},ymm5,ymm2
//  2f2:	c5 fd db c2          	vpand  ymm0,ymm0,ymm2
//  2f6:	c5 c5 db fa          	vpand  ymm7,ymm7,ymm2
//  2fa:	c5 cd db f2          	vpand  ymm6,ymm6,ymm2
//  2fe:	c5 d5 db ea          	vpand  ymm5,ymm5,ymm2
//  302:	c5 f5 db ca          	vpand  ymm1,ymm1,ymm2
//  306:	62 f1 fe 28 7f 07    	vmovdqu64 YMMWORD PTR [rdi],ymm0
//  30c:	62 f1 fe 28 7f 7f 01 	vmovdqu64 YMMWORD PTR [rdi+0x20],ymm7
//  313:	62 f1 fe 28 7f 77 02 	vmovdqu64 YMMWORD PTR [rdi+0x40],ymm6
//  31a:	62 f1 fe 28 7f 6f 03 	vmovdqu64 YMMWORD PTR [rdi+0x60],ymm5
//  321:	62 f1 fe 28 7f 4f 04 	vmovdqu64 YMMWORD PTR [rdi+0x80],ymm1
//  328:	c5 f8 77             	vzeroupper 
  evpsubq(xmm0, k1, xmm0, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm6, k2, xmm6, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm1, k3, xmm1, xmm2, true, Assembler::AVX_256bit);
  kmovbl(k4, rax);
  kmovbl(k5, rdx);
  evpsubq(xmm7, k4, xmm7, xmm2, true, Assembler::AVX_256bit);
  evpsubq(xmm5, k5, xmm5, xmm2, true, Assembler::AVX_256bit);
  vpand(xmm0, xmm0, xmm2, Assembler::AVX_256bit);
  vpand(xmm7, xmm7, xmm2, Assembler::AVX_256bit);
  vpand(xmm6, xmm6, xmm2, Assembler::AVX_256bit);
  vpand(xmm5, xmm5, xmm2, Assembler::AVX_256bit);
  vpand(xmm1, xmm1, xmm2, Assembler::AVX_256bit);
  evmovdquq(Address(rdi, 0x00), xmm0, Assembler::AVX_256bit);
  evmovdquq(Address(rdi, 0x20), xmm7, Assembler::AVX_256bit);
  evmovdquq(Address(rdi, 0x40), xmm6, Assembler::AVX_256bit);
  evmovdquq(Address(rdi, 0x60), xmm5, Assembler::AVX_256bit);
  evmovdquq(Address(rdi, 0x80), xmm1, Assembler::AVX_256bit);
  vzeroupper();

  pop(r13);
  pop(r14);
  pop(r8);

#if 0
 32b:	48 8d 65 d8          	lea    rsp,[rbp-0x28]
 32f:	5b                   	pop    rbx
 330:	41 5c                	pop    r12
 332:	41 5d                	pop    r13
 334:	41 5e                	pop    r14
 336:	41 5f                	pop    r15
 338:	5d                   	pop    rbp
 339:	c3                   	ret    
 33a:	66 0f 1f 44 00 00    	nop    WORD PTR [rax+rax*1+0x0]

0000000000000340 <k1_ifma256_ams52x20>:
 340:	f3 0f 1e fa          	endbr64 
 344:	49 89 c8             	mov    r8,rcx
 347:	48 89 d1             	mov    rcx,rdx
 34a:	48 89 f2             	mov    rdx,rsi
 34d:	e9 00 00 00 00       	jmp    352 <k1_ifma256_ams52x20+0x12>
#endif
  }


void MacroAssembler::montgomeryMultiply52x30(Register out, Register kk0)
{
	const int LIMIT = 5552;
	const int BASE = 65521;
	const int CHUNKSIZE = 16;
	const int CHUNKSIZE_M1 = CHUNKSIZE - 1;

	const Register s = r11;
	const Register a_d = r12; // r12d
	const Register b_d = r8;  // r8d
	const Register end = r13;

	const XMMRegister ya = xmm0;
	const XMMRegister yb = xmm1;
	const XMMRegister ydata0 = xmm2;
	const XMMRegister ydata1 = xmm3;
	const XMMRegister ysa = xmm4;
	const XMMRegister ydata = ysa;
	const XMMRegister ytmp0 = ydata0;
	const XMMRegister ytmp1 = ydata1;
	const XMMRegister ytmp2 = xmm5;
	const XMMRegister xa = xmm0;
	const XMMRegister xb = xmm1;
	const XMMRegister xtmp0 = xmm2;
	const XMMRegister xtmp1 = xmm3;
	const XMMRegister xsa = xmm4;
	const XMMRegister xtmp2 = xmm5;

#if 0
   U64 R0 = get_zero64();
   U64 R1 = get_zero64();
   U64 R2 = get_zero64();
   U64 R3 = get_zero64();

   U64 R0h = get_zero64();
   U64 R1h = get_zero64();
   U64 R2h = get_zero64();
   U64 R3h = get_zero64();

   U64 Bi, Yi;

   Ipp64u m0 = m[0];
   Ipp64u a0 = a[0];
   Ipp64u acc0 = 0;

   int i;
   for (i=0; i<30; i++) {
      Ipp64u t0, t1, t2, yi;

      Bi = set64((long long)b[i]);               /* broadcast(b[i]) */
      /* compute yi */
      t0 = _mulx_u64(a0, b[i], &t2);             /* (t2:t0) = acc0 + a[0]*b[i] */
      ADD104(t2, acc0, 0, t0)
      yi = (acc0 * k0)  & EXP_DIGIT_MASK_AVX512; /* yi = acc0*k0 */
      Yi = set64((long long)yi);

      t0 = _mulx_u64(m0, yi, &t1);               /* (t1:t0)   = m0*yi     */
      ADD104(t2, acc0, t1, t0)                   /* (t2:acc0) += (t1:t0)  */
      acc0 = SHRD52(t2, acc0);

      fma52x8lo_mem(R0, R0, Bi, a, 64*0)
      fma52x8lo_mem(R1, R1, Bi, a, 64*1)
      fma52x8lo_mem(R2, R2, Bi, a, 64*2)
      fma52x8lo_mem(R3, R3, Bi, a, 64*3)

      fma52x8lo_mem(R0, R0, Yi, m, 64*0)
      fma52x8lo_mem(R1, R1, Yi, m, 64*1)
      fma52x8lo_mem(R2, R2, Yi, m, 64*2)
      fma52x8lo_mem(R3, R3, Yi, m, 64*3)

      shift64_imm(R0, R0h, 1)
      shift64_imm(R0h, R1, 1)
      shift64_imm(R1, R1h, 1)
      shift64_imm(R1h, R2, 1)
      shift64_imm(R2, R2h, 1)
      shift64_imm(R2h, R3, 1)
      shift64_imm(R3, R3h, 1)
      shift64_imm(R3h, get_zero64(), 1)

      /* "shift" R */
      t0 = get64(R0, 0);
      acc0 += t0;

      /* U = A*Bi (hi) */
      fma52x8hi_mem(R0, R0, Bi, a, 64*0)
      fma52x8hi_mem(R1, R1, Bi, a, 64*1)
      fma52x8hi_mem(R2, R2, Bi, a, 64*2)
      fma52x8hi_mem(R3, R3, Bi, a, 64*3)
      /* R += M*Yi (hi) */
      fma52x8hi_mem(R0, R0, Yi, m, 64*0)
      fma52x8hi_mem(R1, R1, Yi, m, 64*1)
      fma52x8hi_mem(R2, R2, Yi, m, 64*2)
      fma52x8hi_mem(R3, R3, Yi, m, 64*3)
   }

   /* Set R0[0] == acc0 */
   Bi = set64((long long)acc0);
   R0 = blend64(R0, Bi, 1);

   NORMALIZE_52x30(R0, R1, R2, R3)

   storeu64(out + 0*4, R0);
   storeu64(out + 1*4, R0h);
   storeu64(out + 2*4, R1);
   storeu64(out + 3*4, R1h);
   storeu64(out + 4*4, R2);
   storeu64(out + 5*4, R2h);
   storeu64(out + 6*4, R3);
   storeu64(out + 7*4, R3h);


   4:	55                   	push   rbp
   5:	c5 e9 ef d2          	vpxor  xmm2,xmm2,xmm2
   9:	31 c0                	xor    eax,eax
   b:	48 89 e5             	mov    rbp,rsp
   e:	41 57                	push   r15
  10:	62 f1 fd 28 6f e2    	vmovdqa64 ymm4,ymm2
  16:	62 f1 fd 28 6f f2    	vmovdqa64 ymm6,ymm2
  1c:	41 56                	push   r14
  1e:	62 71 fd 28 6f c2    	vmovdqa64 ymm8,ymm2
  24:	62 f1 fd 28 6f da    	vmovdqa64 ymm3,ymm2
  2a:	41 55                	push   r13
  2c:	62 f1 fd 28 6f ea    	vmovdqa64 ymm5,ymm2
  32:	62 f1 fd 28 6f fa    	vmovdqa64 ymm7,ymm2
  38:	41 54                	push   r12
  3a:	62 f1 fd 28 6f c2    	vmovdqa64 ymm0,ymm2
  40:	49 b9 ff ff ff ff ff 	movabs r9,0xfffffffffffff
  47:	ff 0f 00 
  4a:	53                   	push   rbx
  4b:	62 71 fd 28 6f ca    	vmovdqa64 ymm9,ymm2
  51:	48 8d 9a f0 00 00 00 	lea    rbx,[rdx+0xf0]
  58:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
  5c:	49 89 d7             	mov    r15,rdx
  5f:	4c 8b 29             	mov    r13,QWORD PTR [rcx]
  62:	4c 8b 26             	mov    r12,QWORD PTR [rsi]
  65:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
  6c:	00 00 00 00 
  70:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
  77:	00 00 00 00 
  7b:	0f 1f 44 00 00       	nop    DWORD PTR [rax+rax*1+0x0]
  80:	4d 8b 17             	mov    r10,QWORD PTR [r15]
  83:	62 f1 fd 28 6f ca    	vmovdqa64 ymm1,ymm2
  89:	4c 89 d2             	mov    rdx,r10
  8c:	62 52 fd 28 7c da    	vpbroadcastq ymm11,r10
  92:	c4 42 ab f6 dc       	mulx   r11,r10,r12
  97:	4c 01 d0             	add    rax,r10
  9a:	4d 89 de             	mov    r14,r11
  9d:	49 89 c2             	mov    r10,rax
  a0:	49 83 d6 00          	adc    r14,0x0
  a4:	4d 0f af d0          	imul   r10,r8
  a8:	4d 21 ca             	and    r10,r9
  ab:	4c 89 d2             	mov    rdx,r10
  ae:	62 52 fd 28 7c d2    	vpbroadcastq ymm10,r10
  b4:	c4 42 ab f6 dd       	mulx   r11,r10,r13
  b9:	4c 89 54 24 f0       	mov    QWORD PTR [rsp-0x10],r10
  be:	4c 89 5c 24 f8       	mov    QWORD PTR [rsp-0x8],r11
  c3:	45 31 d2             	xor    r10d,r10d
  c6:	48 03 44 24 f0       	add    rax,QWORD PTR [rsp-0x10]
  cb:	41 0f 92 c2          	setb   r10b
  cf:	62 f2 a5 28 b4 06    	vpmadd52luq ymm0,ymm11,YMMWORD PTR [rsi]
  d5:	62 72 a5 28 b4 46 01 	vpmadd52luq ymm8,ymm11,YMMWORD PTR [rsi+0x20]
  dc:	62 f2 ad 28 b4 01    	vpmadd52luq ymm0,ymm10,YMMWORD PTR [rcx]
  e2:	62 72 ad 28 b4 41 01 	vpmadd52luq ymm8,ymm10,YMMWORD PTR [rcx+0x20]
  e9:	62 f3 bd 28 03 c0 01 	valignq ymm0,ymm8,ymm0,0x1
  f0:	62 f2 a5 28 b4 4e 07 	vpmadd52luq ymm1,ymm11,YMMWORD PTR [rsi+0xe0]
  f7:	4c 8b 5c 24 f8       	mov    r11,QWORD PTR [rsp-0x8]
  fc:	62 f2 ad 28 b4 49 07 	vpmadd52luq ymm1,ymm10,YMMWORD PTR [rcx+0xe0]
 103:	62 f3 b5 28 03 d1 01 	valignq ymm2,ymm9,ymm1,0x1
 10a:	4d 01 de             	add    r14,r11
 10d:	4d 01 d6             	add    r14,r10
 110:	48 c1 e8 34          	shr    rax,0x34
 114:	49 c1 e6 0c          	shl    r14,0xc
 118:	49 09 c6             	or     r14,rax
 11b:	c4 e1 f9 7e c0       	vmovq  rax,xmm0
 120:	62 f2 a5 28 b4 7e 02 	vpmadd52luq ymm7,ymm11,YMMWORD PTR [rsi+0x40]
 127:	62 f2 a5 28 b4 76 03 	vpmadd52luq ymm6,ymm11,YMMWORD PTR [rsi+0x60]
 12e:	62 f2 ad 28 b4 79 02 	vpmadd52luq ymm7,ymm10,YMMWORD PTR [rcx+0x40]
 135:	62 f2 ad 28 b4 71 03 	vpmadd52luq ymm6,ymm10,YMMWORD PTR [rcx+0x60]
 13c:	62 53 c5 28 03 c0 01 	valignq ymm8,ymm7,ymm8,0x1
 143:	62 f2 a5 28 b4 6e 04 	vpmadd52luq ymm5,ymm11,YMMWORD PTR [rsi+0x80]
 14a:	62 f3 cd 28 03 ff 01 	valignq ymm7,ymm6,ymm7,0x1
 151:	62 f2 ad 28 b4 69 04 	vpmadd52luq ymm5,ymm10,YMMWORD PTR [rcx+0x80]
 158:	62 f2 a5 28 b4 66 05 	vpmadd52luq ymm4,ymm11,YMMWORD PTR [rsi+0xa0]
 15f:	62 f3 d5 28 03 f6 01 	valignq ymm6,ymm5,ymm6,0x1
 166:	62 f2 ad 28 b4 61 05 	vpmadd52luq ymm4,ymm10,YMMWORD PTR [rcx+0xa0]
 16d:	62 f2 a5 28 b4 5e 06 	vpmadd52luq ymm3,ymm11,YMMWORD PTR [rsi+0xc0]
 174:	62 f3 dd 28 03 ed 01 	valignq ymm5,ymm4,ymm5,0x1
 17b:	62 f2 ad 28 b4 59 06 	vpmadd52luq ymm3,ymm10,YMMWORD PTR [rcx+0xc0]
 182:	62 f2 a5 28 b5 06    	vpmadd52huq ymm0,ymm11,YMMWORD PTR [rsi]
 188:	62 f3 e5 28 03 e4 01 	valignq ymm4,ymm3,ymm4,0x1
 18f:	62 72 a5 28 b5 46 01 	vpmadd52huq ymm8,ymm11,YMMWORD PTR [rsi+0x20]
 196:	62 f3 f5 28 03 db 01 	valignq ymm3,ymm1,ymm3,0x1
 19d:	62 f2 a5 28 b5 76 03 	vpmadd52huq ymm6,ymm11,YMMWORD PTR [rsi+0x60]
 1a4:	62 f1 fd 28 6f ca    	vmovdqa64 ymm1,ymm2
 1aa:	62 f2 a5 28 b5 6e 04 	vpmadd52huq ymm5,ymm11,YMMWORD PTR [rsi+0x80]
 1b1:	62 f2 a5 28 b5 66 05 	vpmadd52huq ymm4,ymm11,YMMWORD PTR [rsi+0xa0]
 1b8:	62 f2 a5 28 b5 5e 06 	vpmadd52huq ymm3,ymm11,YMMWORD PTR [rsi+0xc0]
 1bf:	62 f2 a5 28 b5 4e 07 	vpmadd52huq ymm1,ymm11,YMMWORD PTR [rsi+0xe0]
 1c6:	4c 01 f0             	add    rax,r14
 1c9:	62 f2 a5 28 b5 7e 02 	vpmadd52huq ymm7,ymm11,YMMWORD PTR [rsi+0x40]
 1d0:	62 f2 ad 28 b5 01    	vpmadd52huq ymm0,ymm10,YMMWORD PTR [rcx]
 1d6:	62 72 ad 28 b5 41 01 	vpmadd52huq ymm8,ymm10,YMMWORD PTR [rcx+0x20]
 1dd:	62 f2 ad 28 b5 79 02 	vpmadd52huq ymm7,ymm10,YMMWORD PTR [rcx+0x40]
 1e4:	49 83 c7 08          	add    r15,0x8
 1e8:	62 f2 ad 28 b5 49 07 	vpmadd52huq ymm1,ymm10,YMMWORD PTR [rcx+0xe0]
 1ef:	62 f2 ad 28 b5 71 03 	vpmadd52huq ymm6,ymm10,YMMWORD PTR [rcx+0x60]
 1f6:	62 f1 fd 28 6f d1    	vmovdqa64 ymm2,ymm1
 1fc:	62 f2 ad 28 b5 69 04 	vpmadd52huq ymm5,ymm10,YMMWORD PTR [rcx+0x80]
 203:	62 f2 ad 28 b5 61 05 	vpmadd52huq ymm4,ymm10,YMMWORD PTR [rcx+0xa0]
 20a:	62 f2 ad 28 b5 59 06 	vpmadd52huq ymm3,ymm10,YMMWORD PTR [rcx+0xc0]
 211:	4c 39 fb             	cmp    rbx,r15
 214:	0f 85 66 fe ff ff    	jne    80 <k1_ifma256_amm52x30+0x80>
 21a:	62 f2 fd 28 7c c8    	vpbroadcastq ymm1,rax
 220:	c4 e3 7d 02 c1 03    	vpblendd ymm0,ymm0,ymm1,0x3
 226:	62 f1 f5 20 73 d0 34 	vpsrlq ymm17,ymm0,0x34
 22d:	62 53 f5 20 03 c9 03 	valignq ymm9,ymm17,ymm9,0x3
 234:	c5 a5 73 d3 34       	vpsrlq ymm11,ymm3,0x34
 239:	c5 ad 73 d4 34       	vpsrlq ymm10,ymm4,0x34
 23e:	c5 95 73 d2 34       	vpsrlq ymm13,ymm2,0x34
 243:	62 53 a5 28 03 f2 03 	valignq ymm14,ymm11,ymm10,0x3
 24a:	c5 f5 73 d6 34       	vpsrlq ymm1,ymm6,0x34
 24f:	62 53 95 28 03 eb 03 	valignq ymm13,ymm13,ymm11,0x3
 256:	c4 c1 1d 73 d0 34    	vpsrlq ymm12,ymm8,0x34
 25c:	62 f1 fd 20 73 d7 34 	vpsrlq ymm16,ymm7,0x34
 263:	c5 85 73 d5 34       	vpsrlq ymm15,ymm5,0x34
 268:	62 33 f5 28 03 d8 03 	valignq ymm11,ymm1,ymm16,0x3
 26f:	62 53 ad 28 03 d7 03 	valignq ymm10,ymm10,ymm15,0x3
 276:	62 c3 fd 20 03 c4 03 	valignq ymm16,ymm16,ymm12,0x3
 27d:	62 73 85 28 03 f9 03 	valignq ymm15,ymm15,ymm1,0x3
 284:	62 f1 fd 28 6f 0d 00 	vmovdqa64 ymm1,YMMWORD PTR [rip+0x0]        # 28e <k1_ifma256_amm52x30+0x28e>
 28b:	00 00 00 
 28e:	62 33 9d 28 03 e1 03 	valignq ymm12,ymm12,ymm17,0x3
 295:	c5 fd db c1          	vpand  ymm0,ymm0,ymm1
 299:	c4 41 7d d4 c9       	vpaddq ymm9,ymm0,ymm9
 29e:	62 d3 f5 28 1e c1 01 	vpcmpltuq k0,ymm1,ymm9
 2a5:	c5 e5 db d9          	vpand  ymm3,ymm3,ymm1
 2a9:	c5 ed db d1          	vpand  ymm2,ymm2,ymm1
 2ad:	c4 c1 65 d4 de       	vpaddq ymm3,ymm3,ymm14
 2b2:	c4 c1 6d d4 d5       	vpaddq ymm2,ymm2,ymm13
 2b7:	c5 c5 db f9          	vpand  ymm7,ymm7,ymm1
 2bb:	c5 dd db e1          	vpand  ymm4,ymm4,ymm1
 2bf:	62 f3 f5 28 1e db 01 	vpcmpltuq k3,ymm1,ymm3
 2c6:	62 f3 f5 28 1e fa 01 	vpcmpltuq k7,ymm1,ymm2
 2cd:	62 b1 c5 28 d4 f8    	vpaddq ymm7,ymm7,ymm16
 2d3:	c4 c1 5d d4 e2       	vpaddq ymm4,ymm4,ymm10
 2d8:	62 f3 f5 28 1e f4 01 	vpcmpltuq k6,ymm1,ymm4
 2df:	c5 d5 db e9          	vpand  ymm5,ymm5,ymm1
 2e3:	c5 79 93 f0          	kmovb  r14d,k0
 2e7:	62 f3 f5 28 1e c7 01 	vpcmpltuq k0,ymm1,ymm7
 2ee:	c4 c1 55 d4 ef       	vpaddq ymm5,ymm5,ymm15
 2f3:	c5 cd db f1          	vpand  ymm6,ymm6,ymm1
 2f7:	62 f3 f5 28 1e d5 01 	vpcmpltuq k2,ymm1,ymm5
 2fe:	c4 c1 4d d4 f3       	vpaddq ymm6,ymm6,ymm11
 303:	c5 f9 91 5c 24 f0    	kmovb  BYTE PTR [rsp-0x10],k3
 309:	c5 f9 91 7c 24 e8    	kmovb  BYTE PTR [rsp-0x18],k7
 30f:	62 f3 f5 28 1e db 00 	vpcmpequq k3,ymm1,ymm3
 316:	62 f3 f5 28 1e fa 00 	vpcmpequq k7,ymm1,ymm2
 31d:	62 f3 f5 28 1e ee 01 	vpcmpltuq k5,ymm1,ymm6
 324:	c5 3d db c1          	vpand  ymm8,ymm8,ymm1
 328:	c5 f9 93 d0          	kmovb  edx,k0
 32c:	c5 79 93 c6          	kmovb  r8d,k6
 330:	62 d3 f5 28 1e c1 00 	vpcmpequq k0,ymm1,ymm9
 337:	62 f3 f5 28 1e f4 00 	vpcmpequq k6,ymm1,ymm4
 33e:	c4 41 3d d4 c4       	vpaddq ymm8,ymm8,ymm12
 343:	62 d3 f5 28 1e e0 01 	vpcmpltuq k4,ymm1,ymm8
 34a:	c5 f9 93 f2          	kmovb  esi,k2
 34e:	c5 79 93 eb          	kmovb  r13d,k3
 352:	62 f3 f5 28 1e d5 00 	vpcmpequq k2,ymm1,ymm5
 359:	41 c1 e5 18          	shl    r13d,0x18
 35d:	c5 79 93 ff          	kmovb  r15d,k7
 361:	41 c1 e7 1c          	shl    r15d,0x1c
 365:	45 09 fd             	or     r13d,r15d
 368:	c5 f9 93 cd          	kmovb  ecx,k5
 36c:	c5 79 93 e0          	kmovb  r12d,k0
 370:	62 f3 f5 28 1e ee 00 	vpcmpequq k5,ymm1,ymm6
 377:	45 0f b6 fc          	movzx  r15d,r12b
 37b:	c5 f9 93 c6          	kmovb  eax,k6
 37f:	c1 e0 14             	shl    eax,0x14
 382:	62 f3 f5 28 1e cf 00 	vpcmpequq k1,ymm1,ymm7
 389:	45 09 fd             	or     r13d,r15d
 38c:	41 89 c7             	mov    r15d,eax
 38f:	45 09 ef             	or     r15d,r13d
 392:	c5 f9 91 64 24 ef    	kmovb  BYTE PTR [rsp-0x11],k4
 398:	c5 f9 93 da          	kmovb  ebx,k2
 39c:	62 d3 f5 28 1e e0 00 	vpcmpequq k4,ymm1,ymm8
 3a3:	c1 e3 10             	shl    ebx,0x10
 3a6:	41 09 df             	or     r15d,ebx
 3a9:	c5 79 93 dd          	kmovb  r11d,k5
 3ad:	41 c1 e3 0c          	shl    r11d,0xc
 3b1:	45 09 df             	or     r15d,r11d
 3b4:	c5 79 93 d1          	kmovb  r10d,k1
 3b8:	41 c1 e2 08          	shl    r10d,0x8
 3bc:	45 09 fa             	or     r10d,r15d
 3bf:	c5 79 93 cc          	kmovb  r9d,k4
 3c3:	41 0f b6 c1          	movzx  eax,r9b
 3c7:	c1 e0 04             	shl    eax,0x4
 3ca:	45 89 d1             	mov    r9d,r10d
 3cd:	41 09 c1             	or     r9d,eax
 3d0:	44 8b 54 24 f0       	mov    r10d,DWORD PTR [rsp-0x10]
 3d5:	8b 44 24 e8          	mov    eax,DWORD PTR [rsp-0x18]
 3d9:	41 c1 e2 18          	shl    r10d,0x18
 3dd:	c1 e0 1c             	shl    eax,0x1c
 3e0:	44 09 d0             	or     eax,r10d
 3e3:	41 09 c6             	or     r14d,eax
 3e6:	41 c1 e0 14          	shl    r8d,0x14
 3ea:	45 09 f0             	or     r8d,r14d
 3ed:	c1 e6 10             	shl    esi,0x10
 3f0:	44 09 c6             	or     esi,r8d
 3f3:	0f b6 44 24 ef       	movzx  eax,BYTE PTR [rsp-0x11]
 3f8:	c1 e1 0c             	shl    ecx,0xc
 3fb:	09 f1                	or     ecx,esi
 3fd:	c1 e2 08             	shl    edx,0x8
 400:	09 ca                	or     edx,ecx
 402:	c1 e0 04             	shl    eax,0x4
 405:	09 c2                	or     edx,eax
 407:	41 8d 04 51          	lea    eax,[r9+rdx*2]
 40b:	41 31 c1             	xor    r9d,eax
 40e:	44 89 c8             	mov    eax,r9d
 411:	0f b6 c4             	movzx  eax,ah
 414:	c5 f9 92 d0          	kmovb  k2,eax
 418:	44 89 c8             	mov    eax,r9d
 41b:	c1 e8 10             	shr    eax,0x10
 41e:	c5 f9 92 d8          	kmovb  k3,eax
 422:	44 89 c8             	mov    eax,r9d
 425:	c1 e8 18             	shr    eax,0x18
 428:	c5 f9 92 e0          	kmovb  k4,eax
 42c:	44 89 c8             	mov    eax,r9d
 42f:	c1 e8 04             	shr    eax,0x4
 432:	c5 f9 92 e8          	kmovb  k5,eax
 436:	44 89 c8             	mov    eax,r9d
 439:	c1 e8 0c             	shr    eax,0xc
 43c:	c5 f9 92 f0          	kmovb  k6,eax
 440:	44 89 c8             	mov    eax,r9d
 443:	c4 c1 79 92 c9       	kmovb  k1,r9d
 448:	c1 e8 14             	shr    eax,0x14
 44b:	41 c1 e9 1c          	shr    r9d,0x1c
 44f:	62 71 b5 29 fb c9    	vpsubq ymm9{k1},ymm9,ymm1
 455:	62 f1 c5 2a fb f9    	vpsubq ymm7{k2},ymm7,ymm1
 45b:	62 f1 d5 2b fb e9    	vpsubq ymm5{k3},ymm5,ymm1
 461:	62 f1 e5 2c fb d9    	vpsubq ymm3{k4},ymm3,ymm1
 467:	62 71 bd 2d fb c1    	vpsubq ymm8{k5},ymm8,ymm1
 46d:	62 f1 cd 2e fb f1    	vpsubq ymm6{k6},ymm6,ymm1
 473:	c5 f9 92 f8          	kmovb  k7,eax
 477:	c4 c1 79 92 c9       	kmovb  k1,r9d
 47c:	62 f1 dd 2f fb e1    	vpsubq ymm4{k7},ymm4,ymm1
 482:	62 f1 ed 29 fb d1    	vpsubq ymm2{k1},ymm2,ymm1
 488:	c5 35 db c9          	vpand  ymm9,ymm9,ymm1
 48c:	c5 3d db c1          	vpand  ymm8,ymm8,ymm1
 490:	c5 c5 db f9          	vpand  ymm7,ymm7,ymm1
 494:	c5 cd db f1          	vpand  ymm6,ymm6,ymm1
 498:	c5 d5 db e9          	vpand  ymm5,ymm5,ymm1
 49c:	c5 dd db e1          	vpand  ymm4,ymm4,ymm1
 4a0:	c5 e5 db d9          	vpand  ymm3,ymm3,ymm1
 4a4:	c5 ed db d1          	vpand  ymm2,ymm2,ymm1
 4a8:	62 71 fe 28 7f 0f    	vmovdqu64 YMMWORD PTR [rdi],ymm9
 4ae:	62 71 fe 28 7f 47 01 	vmovdqu64 YMMWORD PTR [rdi+0x20],ymm8
 4b5:	62 f1 fe 28 7f 7f 02 	vmovdqu64 YMMWORD PTR [rdi+0x40],ymm7
 4bc:	62 f1 fe 28 7f 77 03 	vmovdqu64 YMMWORD PTR [rdi+0x60],ymm6
 4c3:	62 f1 fe 28 7f 6f 04 	vmovdqu64 YMMWORD PTR [rdi+0x80],ymm5
 4ca:	62 f1 fe 28 7f 67 05 	vmovdqu64 YMMWORD PTR [rdi+0xa0],ymm4
 4d1:	62 f1 fe 28 7f 5f 06 	vmovdqu64 YMMWORD PTR [rdi+0xc0],ymm3
 4d8:	62 f1 fe 28 7f 57 07 	vmovdqu64 YMMWORD PTR [rdi+0xe0],ymm2
 4df:	c5 f8 77             	vzeroupper 
 4e2:	48 8d 65 d8          	lea    rsp,[rbp-0x28]
 4e6:	5b                   	pop    rbx
 4e7:	41 5c                	pop    r12
 4e9:	41 5d                	pop    r13
 4eb:	41 5e                	pop    r14
 4ed:	41 5f                	pop    r15
 4ef:	5d                   	pop    rbp
 4f0:	c3                   	ret    
 4f1:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
 4f8:	00 00 00 00 
 4fc:	0f 1f 40 00          	nop    DWORD PTR [rax+0x0]

0000000000000500 <k1_ifma256_ams52x30>:
 500:	f3 0f 1e fa          	endbr64 
 504:	49 89 c8             	mov    r8,rcx
 507:	48 89 d1             	mov    rcx,rdx
 50a:	48 89 f2             	mov    rdx,rsi
 50d:	e9 00 00 00 00       	jmp    512 <k1_ifma256_ams52x30+0x12>
#endif
}


void MacroAssembler::montgomeryMultiply52x40(Register out, Register kk0)
{
	const int LIMIT = 5552;
	const int BASE = 65521;
	const int CHUNKSIZE = 16;
	const int CHUNKSIZE_M1 = CHUNKSIZE - 1;

	const Register s = r11;
	const Register a_d = r12; // r12d
	const Register b_d = r8;  // r8d
	const Register end = r13;

	const XMMRegister ya = xmm0;
	const XMMRegister yb = xmm1;
	const XMMRegister ydata0 = xmm2;
	const XMMRegister ydata1 = xmm3;
	const XMMRegister ysa = xmm4;
	const XMMRegister ydata = ysa;
	const XMMRegister ytmp0 = ydata0;
	const XMMRegister ytmp1 = ydata1;
	const XMMRegister ytmp2 = xmm5;
	const XMMRegister xa = xmm0;
	const XMMRegister xb = xmm1;
	const XMMRegister xtmp0 = xmm2;
	const XMMRegister xtmp1 = xmm3;
	const XMMRegister xsa = xmm4;
	const XMMRegister xtmp2 = xmm5;

#if 0
   U64 R0 = get_zero64();
   U64 R1 = get_zero64();
   U64 R2 = get_zero64();
   U64 R3 = get_zero64();
   U64 R4 = get_zero64();

   U64 R0h = get_zero64();
   U64 R1h = get_zero64();
   U64 R2h = get_zero64();
   U64 R3h = get_zero64();
   U64 R4h = get_zero64();

   U64 Bi, Yi;

   Ipp64u m0 = m[0];
   Ipp64u a0 = a[0];
   Ipp64u acc0 = 0;

   int i;
   for (i=0; i<40; i++) {
      Ipp64u t0, t1, t2, yi;

      Bi = set64((long long)b[i]);               /* broadcast(b[i]) */
      /* compute yi */
      t0 = _mulx_u64(a0, b[i], &t2);             /* (t2:t0) = acc0 + a[0]*b[i] */
      ADD104(t2, acc0, 0, t0)
      yi = (acc0 * k0)  & EXP_DIGIT_MASK_AVX512; /* yi = acc0*k0 */
      Yi = set64((long long)yi);

      t0 = _mulx_u64(m0, yi, &t1);               /* (t1:t0)   = m0*yi     */
      ADD104(t2, acc0, t1, t0)                   /* (t2:acc0) += (t1:t0)  */
      acc0 = SHRD52(t2, acc0);

      fma52x8lo_mem(R0, R0, Bi, a, 64*0)
      fma52x8lo_mem(R1, R1, Bi, a, 64*1)
      fma52x8lo_mem(R2, R2, Bi, a, 64*2)
      fma52x8lo_mem(R3, R3, Bi, a, 64*3)
      fma52x8lo_mem(R4, R4, Bi, a, 64*4)

      fma52x8lo_mem(R0, R0, Yi, m, 64*0)
      fma52x8lo_mem(R1, R1, Yi, m, 64*1)
      fma52x8lo_mem(R2, R2, Yi, m, 64*2)
      fma52x8lo_mem(R3, R3, Yi, m, 64*3)
      fma52x8lo_mem(R4, R4, Yi, m, 64*4)

      shift64_imm(R0, R0h, 1)
      shift64_imm(R0h, R1, 1)
      shift64_imm(R1, R1h, 1)
      shift64_imm(R1h, R2, 1)
      shift64_imm(R2, R2h, 1)
      shift64_imm(R2h, R3, 1)
      shift64_imm(R3, R3h, 1)
      shift64_imm(R3h, R4, 1)
      shift64_imm(R4, R4h, 1)
      shift64_imm(R4h, get_zero64(), 1)

      /* "shift" R */
      t0 = get64(R0, 0);
      acc0 += t0;

      /* U = A*Bi (hi) */
      fma52x8hi_mem(R0, R0, Bi, a, 64*0)
      fma52x8hi_mem(R1, R1, Bi, a, 64*1)
      fma52x8hi_mem(R2, R2, Bi, a, 64*2)
      fma52x8hi_mem(R3, R3, Bi, a, 64*3)
      fma52x8hi_mem(R4, R4, Bi, a, 64*4)
      /* R += M*Yi (hi) */
      fma52x8hi_mem(R0, R0, Yi, m, 64*0)
      fma52x8hi_mem(R1, R1, Yi, m, 64*1)
      fma52x8hi_mem(R2, R2, Yi, m, 64*2)
      fma52x8hi_mem(R3, R3, Yi, m, 64*3)
      fma52x8hi_mem(R4, R4, Yi, m, 64*4)
   }

   /* Set R0[0] == acc0 */
   Bi = set64((long long)acc0);
   R0 = blend64(R0, Bi, 1);

   NORMALIZE_52x40(R0, R1, R2, R3, R4)

   storeu64(out + 0*4, R0);
   storeu64(out + 1*4, R0h);
   storeu64(out + 2*4, R1);
   storeu64(out + 3*4, R1h);
   storeu64(out + 4*4, R2);
   storeu64(out + 5*4, R2h);
   storeu64(out + 6*4, R3);
   storeu64(out + 7*4, R3h);
   storeu64(out + 8*4, R4);
   storeu64(out + 9*4, R4h);



   4:	55                   	push   rbp
   5:	c5 e9 ef d2          	vpxor  xmm2,xmm2,xmm2
   9:	31 c0                	xor    eax,eax
   b:	48 89 e5             	mov    rbp,rsp
   e:	41 57                	push   r15
  10:	62 f1 fd 28 6f e2    	vmovdqa64 ymm4,ymm2
  16:	62 f1 fd 28 6f f2    	vmovdqa64 ymm6,ymm2
  1c:	41 56                	push   r14
  1e:	62 71 fd 28 6f c2    	vmovdqa64 ymm8,ymm2
  24:	62 71 fd 28 6f d2    	vmovdqa64 ymm10,ymm2
  2a:	41 55                	push   r13
  2c:	62 f1 fd 28 6f da    	vmovdqa64 ymm3,ymm2
  32:	62 f1 fd 28 6f ea    	vmovdqa64 ymm5,ymm2
  38:	41 54                	push   r12
  3a:	62 f1 fd 28 6f fa    	vmovdqa64 ymm7,ymm2
  40:	62 71 fd 28 6f ca    	vmovdqa64 ymm9,ymm2
  46:	53                   	push   rbx
  47:	62 f1 fd 28 6f c2    	vmovdqa64 ymm0,ymm2
  4d:	48 8d 9a 40 01 00 00 	lea    rbx,[rdx+0x140]
  54:	48 83 e4 e0          	and    rsp,0xffffffffffffffe0
  58:	49 b9 ff ff ff ff ff 	movabs r9,0xfffffffffffff
  5f:	ff 0f 00 
  62:	62 71 fd 28 6f da    	vmovdqa64 ymm11,ymm2
  68:	49 89 d7             	mov    r15,rdx
  6b:	4c 8b 29             	mov    r13,QWORD PTR [rcx]
  6e:	4c 8b 26             	mov    r12,QWORD PTR [rsi]
  71:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
  78:	00 00 00 00 
  7c:	0f 1f 40 00          	nop    DWORD PTR [rax+0x0]
  80:	4d 8b 17             	mov    r10,QWORD PTR [r15]
  83:	62 f1 fd 28 6f ca    	vmovdqa64 ymm1,ymm2
  89:	4c 89 d2             	mov    rdx,r10
  8c:	62 52 fd 28 7c ea    	vpbroadcastq ymm13,r10
  92:	c4 42 ab f6 dc       	mulx   r11,r10,r12
  97:	4c 01 d0             	add    rax,r10
  9a:	4d 89 de             	mov    r14,r11
  9d:	49 89 c2             	mov    r10,rax
  a0:	49 83 d6 00          	adc    r14,0x0
  a4:	4d 0f af d0          	imul   r10,r8
  a8:	4d 21 ca             	and    r10,r9
  ab:	4c 89 d2             	mov    rdx,r10
  ae:	62 52 fd 28 7c e2    	vpbroadcastq ymm12,r10
  b4:	c4 42 ab f6 dd       	mulx   r11,r10,r13
  b9:	4c 89 54 24 f0       	mov    QWORD PTR [rsp-0x10],r10
  be:	4c 89 5c 24 f8       	mov    QWORD PTR [rsp-0x8],r11
  c3:	45 31 d2             	xor    r10d,r10d
  c6:	48 03 44 24 f0       	add    rax,QWORD PTR [rsp-0x10]
  cb:	41 0f 92 c2          	setb   r10b
  cf:	62 f2 95 28 b4 06    	vpmadd52luq ymm0,ymm13,YMMWORD PTR [rsi]
  d5:	62 72 95 28 b4 56 01 	vpmadd52luq ymm10,ymm13,YMMWORD PTR [rsi+0x20]
  dc:	62 f2 9d 28 b4 01    	vpmadd52luq ymm0,ymm12,YMMWORD PTR [rcx]
  e2:	62 72 9d 28 b4 51 01 	vpmadd52luq ymm10,ymm12,YMMWORD PTR [rcx+0x20]
  e9:	62 f3 ad 28 03 c0 01 	valignq ymm0,ymm10,ymm0,0x1
  f0:	4c 8b 5c 24 f8       	mov    r11,QWORD PTR [rsp-0x8]
  f5:	62 f2 95 28 b4 4e 09 	vpmadd52luq ymm1,ymm13,YMMWORD PTR [rsi+0x120]
  fc:	62 f2 9d 28 b4 49 09 	vpmadd52luq ymm1,ymm12,YMMWORD PTR [rcx+0x120]
 103:	62 f3 a5 28 03 d1 01 	valignq ymm2,ymm11,ymm1,0x1
 10a:	4d 01 de             	add    r14,r11
 10d:	4d 01 d6             	add    r14,r10
 110:	48 c1 e8 34          	shr    rax,0x34
 114:	49 c1 e6 0c          	shl    r14,0xc
 118:	49 09 c6             	or     r14,rax
 11b:	c4 e1 f9 7e c0       	vmovq  rax,xmm0
 120:	62 72 95 28 b4 4e 02 	vpmadd52luq ymm9,ymm13,YMMWORD PTR [rsi+0x40]
 127:	62 72 95 28 b4 46 03 	vpmadd52luq ymm8,ymm13,YMMWORD PTR [rsi+0x60]
 12e:	62 72 9d 28 b4 49 02 	vpmadd52luq ymm9,ymm12,YMMWORD PTR [rcx+0x40]
 135:	62 72 9d 28 b4 41 03 	vpmadd52luq ymm8,ymm12,YMMWORD PTR [rcx+0x60]
 13c:	62 53 b5 28 03 d2 01 	valignq ymm10,ymm9,ymm10,0x1
 143:	62 f2 95 28 b4 7e 04 	vpmadd52luq ymm7,ymm13,YMMWORD PTR [rsi+0x80]
 14a:	62 53 bd 28 03 c9 01 	valignq ymm9,ymm8,ymm9,0x1
 151:	62 f2 9d 28 b4 79 04 	vpmadd52luq ymm7,ymm12,YMMWORD PTR [rcx+0x80]
 158:	62 f2 95 28 b4 76 05 	vpmadd52luq ymm6,ymm13,YMMWORD PTR [rsi+0xa0]
 15f:	62 53 c5 28 03 c0 01 	valignq ymm8,ymm7,ymm8,0x1
 166:	62 f2 9d 28 b4 71 05 	vpmadd52luq ymm6,ymm12,YMMWORD PTR [rcx+0xa0]
 16d:	62 f2 95 28 b4 6e 06 	vpmadd52luq ymm5,ymm13,YMMWORD PTR [rsi+0xc0]
 174:	62 f3 cd 28 03 ff 01 	valignq ymm7,ymm6,ymm7,0x1
 17b:	62 f2 9d 28 b4 69 06 	vpmadd52luq ymm5,ymm12,YMMWORD PTR [rcx+0xc0]
 182:	62 f2 95 28 b4 66 07 	vpmadd52luq ymm4,ymm13,YMMWORD PTR [rsi+0xe0]
 189:	62 f3 d5 28 03 f6 01 	valignq ymm6,ymm5,ymm6,0x1
 190:	62 f2 9d 28 b4 61 07 	vpmadd52luq ymm4,ymm12,YMMWORD PTR [rcx+0xe0]
 197:	62 f2 95 28 b4 5e 08 	vpmadd52luq ymm3,ymm13,YMMWORD PTR [rsi+0x100]
 19e:	62 f3 dd 28 03 ed 01 	valignq ymm5,ymm4,ymm5,0x1
 1a5:	62 f2 9d 28 b4 59 08 	vpmadd52luq ymm3,ymm12,YMMWORD PTR [rcx+0x100]
 1ac:	62 f2 95 28 b5 06    	vpmadd52huq ymm0,ymm13,YMMWORD PTR [rsi]
 1b2:	62 72 95 28 b5 56 01 	vpmadd52huq ymm10,ymm13,YMMWORD PTR [rsi+0x20]
 1b9:	62 72 95 28 b5 4e 02 	vpmadd52huq ymm9,ymm13,YMMWORD PTR [rsi+0x40]
 1c0:	62 72 95 28 b5 46 03 	vpmadd52huq ymm8,ymm13,YMMWORD PTR [rsi+0x60]
 1c7:	62 f2 95 28 b5 7e 04 	vpmadd52huq ymm7,ymm13,YMMWORD PTR [rsi+0x80]
 1ce:	62 f2 95 28 b5 76 05 	vpmadd52huq ymm6,ymm13,YMMWORD PTR [rsi+0xa0]
 1d5:	4c 01 f0             	add    rax,r14
 1d8:	62 f2 95 28 b5 6e 06 	vpmadd52huq ymm5,ymm13,YMMWORD PTR [rsi+0xc0]
 1df:	49 83 c7 08          	add    r15,0x8
 1e3:	62 f3 e5 28 03 e4 01 	valignq ymm4,ymm3,ymm4,0x1
 1ea:	62 f2 9d 28 b5 01    	vpmadd52huq ymm0,ymm12,YMMWORD PTR [rcx]
 1f0:	62 f3 f5 28 03 db 01 	valignq ymm3,ymm1,ymm3,0x1
 1f7:	62 f2 95 28 b5 66 07 	vpmadd52huq ymm4,ymm13,YMMWORD PTR [rsi+0xe0]
 1fe:	62 f1 fd 28 6f ca    	vmovdqa64 ymm1,ymm2
 204:	62 f2 95 28 b5 5e 08 	vpmadd52huq ymm3,ymm13,YMMWORD PTR [rsi+0x100]
 20b:	62 f2 95 28 b5 4e 09 	vpmadd52huq ymm1,ymm13,YMMWORD PTR [rsi+0x120]
 212:	62 72 9d 28 b5 51 01 	vpmadd52huq ymm10,ymm12,YMMWORD PTR [rcx+0x20]
 219:	62 f2 9d 28 b5 49 09 	vpmadd52huq ymm1,ymm12,YMMWORD PTR [rcx+0x120]
 220:	62 72 9d 28 b5 49 02 	vpmadd52huq ymm9,ymm12,YMMWORD PTR [rcx+0x40]
 227:	62 f1 fd 28 6f d1    	vmovdqa64 ymm2,ymm1
 22d:	62 72 9d 28 b5 41 03 	vpmadd52huq ymm8,ymm12,YMMWORD PTR [rcx+0x60]
 234:	62 f2 9d 28 b5 79 04 	vpmadd52huq ymm7,ymm12,YMMWORD PTR [rcx+0x80]
 23b:	62 f2 9d 28 b5 71 05 	vpmadd52huq ymm6,ymm12,YMMWORD PTR [rcx+0xa0]
 242:	62 f2 9d 28 b5 69 06 	vpmadd52huq ymm5,ymm12,YMMWORD PTR [rcx+0xc0]
 249:	62 f2 9d 28 b5 61 07 	vpmadd52huq ymm4,ymm12,YMMWORD PTR [rcx+0xe0]
 250:	62 f2 9d 28 b5 59 08 	vpmadd52huq ymm3,ymm12,YMMWORD PTR [rcx+0x100]
 257:	4c 39 fb             	cmp    rbx,r15
 25a:	0f 85 20 fe ff ff    	jne    80 <k1_ifma256_amm52x40+0x80>
 260:	62 f2 fd 28 7c c8    	vpbroadcastq ymm1,rax
 266:	c4 e3 7d 02 c1 03    	vpblendd ymm0,ymm0,ymm1,0x3
 26c:	62 f1 d5 20 73 d0 34 	vpsrlq ymm21,ymm0,0x34
 273:	62 53 d5 20 03 db 03 	valignq ymm11,ymm21,ymm11,0x3
 27a:	c5 f5 73 d7 34       	vpsrlq ymm1,ymm7,0x34
 27f:	62 d1 fd 20 73 d2 34 	vpsrlq ymm16,ymm10,0x34
 286:	62 d1 dd 20 73 d1 34 	vpsrlq ymm20,ymm9,0x34
 28d:	c4 c1 05 73 d0 34    	vpsrlq ymm15,ymm8,0x34
 293:	c5 8d 73 d6 34       	vpsrlq ymm14,ymm6,0x34
 298:	62 f1 ed 20 73 d5 34 	vpsrlq ymm18,ymm5,0x34
 29f:	c5 95 73 d4 34       	vpsrlq ymm13,ymm4,0x34
 2a4:	62 f1 f5 20 73 d3 34 	vpsrlq ymm17,ymm3,0x34
 2ab:	c5 9d 73 d2 34       	vpsrlq ymm12,ymm2,0x34
 2b0:	62 c3 f5 28 03 df 03 	valignq ymm19,ymm1,ymm15,0x3
 2b7:	62 33 9d 28 03 e1 03 	valignq ymm12,ymm12,ymm17,0x3
 2be:	62 33 85 28 03 fc 03 	valignq ymm15,ymm15,ymm20,0x3
 2c5:	62 c3 f5 20 03 cd 03 	valignq ymm17,ymm17,ymm13,0x3
 2cc:	62 a3 dd 20 03 e0 03 	valignq ymm20,ymm20,ymm16,0x3
 2d3:	62 33 95 28 03 ea 03 	valignq ymm13,ymm13,ymm18,0x3
 2da:	62 c3 ed 20 03 d6 03 	valignq ymm18,ymm18,ymm14,0x3
 2e1:	62 73 8d 28 03 f1 03 	valignq ymm14,ymm14,ymm1,0x3
 2e8:	62 f1 fd 28 6f 0d 00 	vmovdqa64 ymm1,YMMWORD PTR [rip+0x0]        # 2f2 <k1_ifma256_amm52x40+0x2f2>
 2ef:	00 00 00 
 2f2:	62 a3 fd 20 03 c5 03 	valignq ymm16,ymm16,ymm21,0x3
 2f9:	c5 fd db c1          	vpand  ymm0,ymm0,ymm1
 2fd:	c4 41 7d d4 db       	vpaddq ymm11,ymm0,ymm11
 302:	62 d3 f5 28 1e c3 01 	vpcmpltuq k0,ymm1,ymm11
 309:	c5 35 db c9          	vpand  ymm9,ymm9,ymm1
 30d:	c5 d5 db e9          	vpand  ymm5,ymm5,ymm1
 311:	62 31 b5 28 d4 cc    	vpaddq ymm9,ymm9,ymm20
 317:	62 b1 d5 28 d4 ea    	vpaddq ymm5,ymm5,ymm18
 31d:	62 f3 f5 28 1e ed 01 	vpcmpltuq k5,ymm1,ymm5
 324:	c5 79 93 f8          	kmovb  r15d,k0
 328:	62 d3 f5 28 1e c1 01 	vpcmpltuq k0,ymm1,ymm9
 32f:	c5 3d db c1          	vpand  ymm8,ymm8,ymm1
 333:	c4 41 3d d4 c7       	vpaddq ymm8,ymm8,ymm15
 338:	c5 c5 db f9          	vpand  ymm7,ymm7,ymm1
 33c:	c5 dd db e1          	vpand  ymm4,ymm4,ymm1
 340:	62 b1 c5 28 d4 fb    	vpaddq ymm7,ymm7,ymm19
 346:	c4 c1 5d d4 e5       	vpaddq ymm4,ymm4,ymm13
 34b:	c5 f9 91 44 24 f0    	kmovb  BYTE PTR [rsp-0x10],k0
 351:	c5 f9 93 d5          	kmovb  edx,k5
 355:	62 d3 f5 28 1e c0 01 	vpcmpltuq k0,ymm1,ymm8
 35c:	62 d3 f5 28 1e e9 00 	vpcmpequq k5,ymm1,ymm9
 363:	62 f3 f5 28 1e e7 01 	vpcmpltuq k4,ymm1,ymm7
 36a:	62 f3 f5 28 1e d4 01 	vpcmpltuq k2,ymm1,ymm4
 371:	c5 e5 db d9          	vpand  ymm3,ymm3,ymm1
 375:	c5 ed db d1          	vpand  ymm2,ymm2,ymm1
 379:	62 b1 e5 28 d4 d9    	vpaddq ymm3,ymm3,ymm17
 37f:	c4 c1 6d d4 d4       	vpaddq ymm2,ymm2,ymm12
 384:	c5 f9 91 44 24 ed    	kmovb  BYTE PTR [rsp-0x13],k0
 38a:	c5 79 93 e5          	kmovb  r12d,k5
 38e:	62 f3 f5 28 1e c3 00 	vpcmpequq k0,ymm1,ymm3
 395:	62 f3 f5 28 1e ea 00 	vpcmpequq k5,ymm1,ymm2
 39c:	c5 f9 91 64 24 ef    	kmovb  BYTE PTR [rsp-0x11],k4
 3a2:	c5 f9 91 54 24 eb    	kmovb  BYTE PTR [rsp-0x15],k2
 3a8:	62 d3 f5 28 1e e3 00 	vpcmpequq k4,ymm1,ymm11
 3af:	62 d3 f5 28 1e d0 00 	vpcmpequq k2,ymm1,ymm8
 3b6:	62 f3 f5 28 1e da 01 	vpcmpltuq k3,ymm1,ymm2
 3bd:	c5 79 93 d0          	kmovb  r10d,k0
 3c1:	c5 79 93 f5          	kmovb  r14d,k5
 3c5:	49 c1 e2 20          	shl    r10,0x20
 3c9:	49 c1 e6 24          	shl    r14,0x24
 3cd:	62 f3 f5 28 1e f3 01 	vpcmpltuq k6,ymm1,ymm3
 3d4:	4d 09 f2             	or     r10,r14
 3d7:	c5 79 93 ec          	kmovb  r13d,k4
 3db:	c5 f9 93 c2          	kmovb  eax,k2
 3df:	45 0f b6 f5          	movzx  r14d,r13b
 3e3:	48 c1 e0 0c          	shl    rax,0xc
 3e7:	4d 09 f2             	or     r10,r14
 3ea:	c5 f9 91 5c 24 ea    	kmovb  BYTE PTR [rsp-0x16],k3
 3f0:	49 89 c6             	mov    r14,rax
 3f3:	0f b6 44 24 ea       	movzx  eax,BYTE PTR [rsp-0x16]
 3f8:	c5 79 93 de          	kmovb  r11d,k6
 3fc:	48 c1 e0 24          	shl    rax,0x24
 400:	49 c1 e3 20          	shl    r11,0x20
 404:	4c 09 d8             	or     rax,r11
 407:	c5 cd db f1          	vpand  ymm6,ymm6,ymm1
 40b:	c4 c1 4d d4 f6       	vpaddq ymm6,ymm6,ymm14
 410:	4c 09 f8             	or     rax,r15
 413:	c5 2d db d1          	vpand  ymm10,ymm10,ymm1
 417:	44 0f b6 7c 24 eb    	movzx  r15d,BYTE PTR [rsp-0x15]
 41d:	62 f3 f5 28 1e ce 01 	vpcmpltuq k1,ymm1,ymm6
 424:	62 31 ad 28 d4 d0    	vpaddq ymm10,ymm10,ymm16
 42a:	62 d3 f5 28 1e fa 01 	vpcmpltuq k7,ymm1,ymm10
 431:	49 c1 e7 1c          	shl    r15,0x1c
 435:	4c 09 f8             	or     rax,r15
 438:	48 c1 e2 18          	shl    rdx,0x18
 43c:	62 f3 f5 28 1e e4 00 	vpcmpequq k4,ymm1,ymm4
 443:	48 09 d0             	or     rax,rdx
 446:	c5 f9 91 4c 24 ec    	kmovb  BYTE PTR [rsp-0x14],k1
 44c:	0f b6 54 24 ec       	movzx  edx,BYTE PTR [rsp-0x14]
 451:	c5 f9 91 7c 24 ee    	kmovb  BYTE PTR [rsp-0x12],k7
 457:	62 f3 f5 28 1e fd 00 	vpcmpequq k7,ymm1,ymm5
 45e:	62 f3 f5 28 1e de 00 	vpcmpequq k3,ymm1,ymm6
 465:	48 c1 e2 14          	shl    rdx,0x14
 469:	62 f3 f5 28 1e f7 00 	vpcmpequq k6,ymm1,ymm7
 470:	48 09 d0             	or     rax,rdx
 473:	c5 79 93 cc          	kmovb  r9d,k4
 477:	0f b6 54 24 ef       	movzx  edx,BYTE PTR [rsp-0x11]
 47c:	49 c1 e1 1c          	shl    r9,0x1c
 480:	4d 09 ca             	or     r10,r9
 483:	c5 79 93 c7          	kmovb  r8d,k7
 487:	49 c1 e0 18          	shl    r8,0x18
 48b:	4d 09 c2             	or     r10,r8
 48e:	48 c1 e2 10          	shl    rdx,0x10
 492:	c5 f9 93 f3          	kmovb  esi,k3
 496:	48 c1 e6 14          	shl    rsi,0x14
 49a:	49 09 f2             	or     r10,rsi
 49d:	48 09 d0             	or     rax,rdx
 4a0:	c5 f9 93 ce          	kmovb  ecx,k6
 4a4:	0f b6 54 24 ed       	movzx  edx,BYTE PTR [rsp-0x13]
 4a9:	48 c1 e1 10          	shl    rcx,0x10
 4ad:	62 d3 f5 28 1e ca 00 	vpcmpequq k1,ymm1,ymm10
 4b4:	49 09 ca             	or     r10,rcx
 4b7:	4d 09 d6             	or     r14,r10
 4ba:	48 c1 e2 0c          	shl    rdx,0xc
 4be:	4d 89 e2             	mov    r10,r12
 4c1:	49 c1 e2 08          	shl    r10,0x8
 4c5:	48 09 d0             	or     rax,rdx
 4c8:	0f b6 54 24 f0       	movzx  edx,BYTE PTR [rsp-0x10]
 4cd:	4d 09 d6             	or     r14,r10
 4d0:	c5 f9 93 d9          	kmovb  ebx,k1
 4d4:	48 c1 e3 04          	shl    rbx,0x4
 4d8:	49 09 de             	or     r14,rbx
 4db:	48 c1 e2 08          	shl    rdx,0x8
 4df:	48 09 d0             	or     rax,rdx
 4e2:	0f b6 54 24 ee       	movzx  edx,BYTE PTR [rsp-0x12]
 4e7:	48 c1 e2 04          	shl    rdx,0x4
 4eb:	48 09 d0             	or     rax,rdx
 4ee:	49 8d 1c 46          	lea    rbx,[r14+rax*2]
 4f2:	4c 31 f3             	xor    rbx,r14
 4f5:	0f b6 c7             	movzx  eax,bh
 4f8:	c5 f9 92 d0          	kmovb  k2,eax
 4fc:	48 89 d8             	mov    rax,rbx
 4ff:	48 c1 e8 10          	shr    rax,0x10
 503:	c5 f9 92 d8          	kmovb  k3,eax
 507:	89 d8                	mov    eax,ebx
 509:	c1 e8 18             	shr    eax,0x18
 50c:	c5 f9 92 e0          	kmovb  k4,eax
 510:	48 89 d8             	mov    rax,rbx
 513:	48 c1 e8 20          	shr    rax,0x20
 517:	c5 f9 92 e8          	kmovb  k5,eax
 51b:	48 89 d8             	mov    rax,rbx
 51e:	48 c1 e8 04          	shr    rax,0x4
 522:	c5 f9 92 f0          	kmovb  k6,eax
 526:	48 89 d8             	mov    rax,rbx
 529:	48 c1 e8 0c          	shr    rax,0xc
 52d:	c5 f9 92 f8          	kmovb  k7,eax
 531:	48 89 d8             	mov    rax,rbx
 534:	48 c1 e8 14          	shr    rax,0x14
 538:	c5 f9 92 cb          	kmovb  k1,ebx
 53c:	62 71 a5 29 fb d9    	vpsubq ymm11{k1},ymm11,ymm1
 542:	c5 f9 92 c8          	kmovb  k1,eax
 546:	48 89 d8             	mov    rax,rbx
 549:	48 c1 e8 1c          	shr    rax,0x1c
 54d:	48 c1 eb 24          	shr    rbx,0x24
 551:	62 71 b5 2a fb c9    	vpsubq ymm9{k2},ymm9,ymm1
 557:	62 f1 c5 2b fb f9    	vpsubq ymm7{k3},ymm7,ymm1
 55d:	62 f1 d5 2c fb e9    	vpsubq ymm5{k4},ymm5,ymm1
 563:	62 f1 e5 2d fb d9    	vpsubq ymm3{k5},ymm3,ymm1
 569:	62 71 ad 2e fb d1    	vpsubq ymm10{k6},ymm10,ymm1
 56f:	62 71 bd 2f fb c1    	vpsubq ymm8{k7},ymm8,ymm1
 575:	62 f1 cd 29 fb f1    	vpsubq ymm6{k1},ymm6,ymm1
 57b:	c5 f9 92 d0          	kmovb  k2,eax
 57f:	c5 f9 92 db          	kmovb  k3,ebx
 583:	62 f1 dd 2a fb e1    	vpsubq ymm4{k2},ymm4,ymm1
 589:	62 f1 ed 2b fb d1    	vpsubq ymm2{k3},ymm2,ymm1
 58f:	c5 25 db d9          	vpand  ymm11,ymm11,ymm1
 593:	c5 2d db d1          	vpand  ymm10,ymm10,ymm1
 597:	c5 35 db c9          	vpand  ymm9,ymm9,ymm1
 59b:	c5 3d db c1          	vpand  ymm8,ymm8,ymm1
 59f:	c5 c5 db f9          	vpand  ymm7,ymm7,ymm1
 5a3:	c5 cd db f1          	vpand  ymm6,ymm6,ymm1
 5a7:	c5 d5 db e9          	vpand  ymm5,ymm5,ymm1
 5ab:	c5 dd db e1          	vpand  ymm4,ymm4,ymm1
 5af:	c5 e5 db d9          	vpand  ymm3,ymm3,ymm1
 5b3:	c5 ed db d1          	vpand  ymm2,ymm2,ymm1
 5b7:	62 71 fe 28 7f 1f    	vmovdqu64 YMMWORD PTR [rdi],ymm11
 5bd:	62 71 fe 28 7f 57 01 	vmovdqu64 YMMWORD PTR [rdi+0x20],ymm10
 5c4:	62 71 fe 28 7f 4f 02 	vmovdqu64 YMMWORD PTR [rdi+0x40],ymm9
 5cb:	62 71 fe 28 7f 47 03 	vmovdqu64 YMMWORD PTR [rdi+0x60],ymm8
 5d2:	62 f1 fe 28 7f 7f 04 	vmovdqu64 YMMWORD PTR [rdi+0x80],ymm7
 5d9:	62 f1 fe 28 7f 77 05 	vmovdqu64 YMMWORD PTR [rdi+0xa0],ymm6
 5e0:	62 f1 fe 28 7f 6f 06 	vmovdqu64 YMMWORD PTR [rdi+0xc0],ymm5
 5e7:	62 f1 fe 28 7f 67 07 	vmovdqu64 YMMWORD PTR [rdi+0xe0],ymm4
 5ee:	62 f1 fe 28 7f 5f 08 	vmovdqu64 YMMWORD PTR [rdi+0x100],ymm3
 5f5:	62 f1 fe 28 7f 57 09 	vmovdqu64 YMMWORD PTR [rdi+0x120],ymm2
 5fc:	c5 f8 77             	vzeroupper 
 5ff:	48 8d 65 d8          	lea    rsp,[rbp-0x28]
 603:	5b                   	pop    rbx
 604:	41 5c                	pop    r12
 606:	41 5d                	pop    r13
 608:	41 5e                	pop    r14
 60a:	41 5f                	pop    r15
 60c:	5d                   	pop    rbp
 60d:	c3                   	ret    
 60e:	66 66 2e 0f 1f 84 00 	data16 nop WORD PTR cs:[rax+rax*1+0x0]
 615:	00 00 00 00 
 619:	0f 1f 80 00 00 00 00 	nop    DWORD PTR [rax+0x0]

0000000000000620 <k1_ifma256_ams52x40>:
 620:	f3 0f 1e fa          	endbr64 
 624:	49 89 c8             	mov    r8,rcx
 627:	48 89 d1             	mov    rcx,rdx
 62a:	48 89 f2             	mov    rdx,rsi
 62d:	e9 00 00 00 00       	jmp    632 <k1_ifma256_ams52x40+0x12>
#endif
}
#endif
