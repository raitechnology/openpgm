/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * 32-bit atomic operations.  A complex mix of inline assembler and compiler
 * intrinsics.  Native x86 code uses fetch-and-add instruction which is proven
 * faster than Solaris intrinsics that all use compare-and-swap (CAS):
 * https://blogs.oracle.com/dave/entry/atomic_fetch_and_add_vs
 *
 * AMD Opteron revision E memory-barrier bug is ignored as obsolete hardware.
 * https://bugzilla.kernel.org/show_bug.cgi?id=11305
 *
 * NB: XADD requires 80486 microprocessor.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_ATOMIC_H__
#define __PGM_ATOMIC_H__

#include <pgm/types.h>


PGM_BEGIN_DECLS

/* 32-bit word addition returning original atomic value.
 *
 * 	uint32_t tmp = *atomic;
 * 	*atomic += val;
 * 	return tmp;
 */

uint32_t
pgm_atomic_exchange_and_add32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	);

/* 32-bit word addition.
 *
 * 	*atomic += val;
 */

void
pgm_atomic_add32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	);

/* 32-bit word increment.
 *
 * 	*atomic++;
 *
 * Per Intel's rule #33: 
 * http://www.intel.com/content/dam/doc/manual/64-ia-32-architectures-optimization-manual.pdf
 *
 * INC and DEC instructions should be replaced with ADD and SUB because false dependencies
 * are created on earlier instructions that set flags.
 */

void
pgm_atomic_inc32 (
	volatile uint32_t*	atomic
	);

/* 32-bit word decrement.
 *
 * 	*atomic--;
 */

void
pgm_atomic_dec32 (
	volatile uint32_t*	atomic
	);

/* 32-bit word load 
 */

uint32_t
pgm_atomic_read32 (
	const volatile uint32_t* atomic
	);

/* 32-bit word store
 */

void
pgm_atomic_write32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	);

PGM_END_DECLS

#endif /* __PGM_ATOMIC_H__ */
