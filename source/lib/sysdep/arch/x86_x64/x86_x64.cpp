/* Copyright (c) 2010 Wildfire Games
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * CPU-specific routines common to 32 and 64-bit x86
 */

#include "precompiled.h"
#include "lib/sysdep/arch/x86_x64/x86_x64.h"

#include <string.h>
#include <stdio.h>
#include <vector>
#include <set>
#include <algorithm>

#include "lib/posix/posix.h"	// pthread
#include "lib/bits.h"
#include "lib/timer.h"
#include "lib/module_init.h"
#include "lib/sysdep/cpu.h"
#include "lib/sysdep/os_cpu.h"

#if ARCH_AMD64
# include "lib/sysdep/arch/amd64/amd64_asm.h"
#else
# include "lib/sysdep/arch/ia32/ia32_asm.h"
#endif

#if MSC_VERSION
# include <intrin.h>	// __rdtsc
#elif GCC_VERSION
// (no includes needed)
#else
# error compiler not supported
#endif


// some of this module's functions are frequently called but require
// non-trivial initialization, so caching is helpful. isInitialized
// flags aren't thread-safe, so we use ModuleInit. calling it from
// every function is a bit wasteful, but it is convenient to avoid
// requiring users to pass around a global state object.
// one big Init() would be prone to deadlock if its subroutines also
// call a public function (that re-enters ModuleInit), so each
// function gets its own initState.


//-----------------------------------------------------------------------------
// CPUID

// note: unfortunately the MSC __cpuid intrinsic does not allow passing
// additional inputs (e.g. ecx = count), so we need to implement this
// in assembly for both IA-32 and AMD64.
static void cpuid_impl(x86_x64_CpuidRegs* regs)
{
#if ARCH_AMD64
	amd64_asm_cpuid(regs);
#else
	ia32_asm_cpuid(regs);
#endif
}

static u32 cpuid_maxFunction;
static u32 cpuid_maxExtendedFunction;

static LibError InitCpuid()
{
	x86_x64_CpuidRegs regs = { 0 };

	regs.eax = 0;
	cpuid_impl(&regs);
	cpuid_maxFunction = regs.eax;

	regs.eax = 0x80000000;
	cpuid_impl(&regs);
	cpuid_maxExtendedFunction = regs.eax;

	return INFO::OK;
}

bool x86_x64_cpuid(x86_x64_CpuidRegs* regs)
{
	static ModuleInitState initState;
	ModuleInit(&initState, InitCpuid);

	const u32 function = regs->eax;
	if(function > cpuid_maxExtendedFunction)
		return false;
	if(function < 0x80000000 && function > cpuid_maxFunction)
		return false;

	cpuid_impl(regs);
	return true;
}


//-----------------------------------------------------------------------------
// capability bits

// treated as 128 bit field; order: std ecx, std edx, ext ecx, ext edx
// keep in sync with enum x86_x64_Cap!
static u32 caps[4];

static LibError InitCaps()
{
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 1;
	if(x86_x64_cpuid(&regs))
	{
		caps[0] = regs.ecx;
		caps[1] = regs.edx;
	}
	regs.eax = 0x80000001;
	if(x86_x64_cpuid(&regs))
	{
		caps[2] = regs.ecx;
		caps[3] = regs.edx;
	}

	return INFO::OK;
}

bool x86_x64_cap(x86_x64_Cap cap)
{
	static ModuleInitState initState;
	ModuleInit(&initState, InitCaps);

	const size_t index = cap >> 5;
	const size_t bit = cap & 0x1F;
	if(index >= ARRAY_SIZE(caps))
	{
		DEBUG_WARN_ERR(ERR::INVALID_PARAM);
		return false;
	}
	return IsBitSet(caps[index], bit);
}


//-----------------------------------------------------------------------------
// CPU identification

static x86_x64_Vendors vendor;

static LibError InitVendor()
{
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 0;
	if(!x86_x64_cpuid(&regs))
		DEBUG_WARN_ERR(ERR::CPU_FEATURE_MISSING);

	// copy regs to string
	// note: 'strange' ebx,edx,ecx reg order is due to ModR/M encoding order.
	char vendorString[13];
	memcpy(&vendorString[0], &regs.ebx, 4);
	memcpy(&vendorString[4], &regs.edx, 4);
	memcpy(&vendorString[8], &regs.ecx, 4);
	vendorString[12] = '\0';	// 0-terminate

	if(!strcmp(vendorString, "AuthenticAMD"))
		vendor = X86_X64_VENDOR_AMD;
	else if(!strcmp(vendorString, "GenuineIntel"))
		vendor = X86_X64_VENDOR_INTEL;
	else
	{
		DEBUG_WARN_ERR(ERR::CPU_UNKNOWN_VENDOR);
		vendor = X86_X64_VENDOR_UNKNOWN;
	}

	return INFO::OK;
}

x86_x64_Vendors x86_x64_Vendor()
{
	static ModuleInitState initState;
	ModuleInit(&initState, InitVendor);
	return vendor;
}


static size_t model;
static size_t family;

static void InitModelAndFamily()
{
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 1;
	if(!x86_x64_cpuid(&regs))
		DEBUG_WARN_ERR(ERR::CPU_FEATURE_MISSING);
	model = bits(regs.eax, 4, 7);
	family = bits(regs.eax, 8, 11);
	const size_t extendedModel = bits(regs.eax, 16, 19);
	const size_t extendedFamily = bits(regs.eax, 20, 27);
	if(family == 0xF)
		family += extendedFamily;
	if(family == 0xF || (x86_x64_Vendor() == X86_X64_VENDOR_INTEL && family == 6))
		model += extendedModel << 4;
}


static size_t generation;

static LibError InitGeneration()
{
	InitModelAndFamily();

	switch(x86_x64_Vendor())
	{
	case X86_X64_VENDOR_AMD:
		switch(family)
		{
		case 5:
			if(model < 6)
				generation = 5;	// K5
			else
				generation = 6;	// K6
			break;

		case 6:
			generation = 7;	// K7 (Athlon)
			break;

		case 0xF:
		case 0x10:
			generation = 8;	// K8 (Opteron)
			break;
		}
		break;

	case X86_X64_VENDOR_INTEL:
		switch(family)
		{
		case 5:
			generation = 5;	// Pentium
			break;

		case 6:
			if(model < 0xF)
				generation = 6;	// Pentium Pro/II/III/M
			else
				generation = 8;	// Core2Duo
			break;

		case 0xF:
			if(model <= 6)
				generation = 7;	// Pentium 4/D
			break;
		}
		if(family >= 0x10)
			generation = 9;
		break;
	}

	debug_assert(generation != 0);
	return INFO::OK;
}

size_t x86_x64_Generation()
{
	static ModuleInitState initState;
	ModuleInit(&initState, InitGeneration);
	return generation;
}


//-----------------------------------------------------------------------------
// cache

static const size_t maxCacheParams = 3;
static x86_x64_CacheParameters cacheParametersStorage[maxCacheParams*2];
static x86_x64_Cache dcache = { 0, cacheParametersStorage };
static x86_x64_Cache icache = { 0, cacheParametersStorage+maxCacheParams };

static const size_t maxTLBParams = 15;
static x86_x64_TLBParameters tlbParametersStorage[maxTLBParams*2];
static x86_x64_TLB dtlb = { 0, tlbParametersStorage };
static x86_x64_TLB itlb = { 0, tlbParametersStorage+maxTLBParams };

static void AddTLBParameters(const x86_x64_TLBParameters& params)
{
	if(params.type == X86_X64_CACHE_TYPE_INSTRUCTION || params.type == X86_X64_CACHE_TYPE_UNIFIED)
	{
		if(itlb.numParameters < maxTLBParams)
			itlb.parameters[itlb.numParameters++] = params;
		else
			debug_assert(0);
	}
	if(params.type == X86_X64_CACHE_TYPE_DATA || params.type == X86_X64_CACHE_TYPE_UNIFIED)
	{
		if(dtlb.numParameters < maxTLBParams)
			dtlb.parameters[dtlb.numParameters++] = params;
		else
			debug_assert(0);
	}

	// large page TLBs have N 2M entries or N/2 4M entries; we generate a
	// second set of parameters for the latter from the former.
	if(params.pageSize == 2*MiB)
	{
		x86_x64_TLBParameters params4M = params;
		params4M.pageSize = 4*MiB;
		params4M.entries  = params.entries/2;
		AddTLBParameters(params4M);
	}
}

namespace AMD
{

static x86_x64_CacheParameters L1Parameters(u32 reg, x86_x64_CacheType type)
{
	x86_x64_CacheParameters params;
	params.type          = type;
	params.level         = 1;
	params.associativity = bits(reg, 16, 23);
	params.lineSize      = bits(reg,  0,  7);
	params.sharedBy      = 1;
	params.totalSize     = bits(reg, 24, 31)*KiB;
	return params;
}

// applies to L2, L3 and TLB2
static const size_t associativities[16] =
{
	0, 1, 2, 0, 4, 0, 8, 0,
	16, 0, 32, 48, 64, 96, 128, x86_x64_fullyAssociative
};

static x86_x64_CacheParameters L2Parameters(u32 reg, x86_x64_CacheType type)
{
	x86_x64_CacheParameters params;
	const size_t associativityIndex = bits(reg, 12, 15);
	if(associativityIndex == 0)	// disabled
	{
		params.type = X86_X64_CACHE_TYPE_NULL;
		params.associativity = 0;
	}
	else
	{
		params.type = type;
		params.associativity = associativities[associativityIndex];
		debug_assert(params.associativity != 0);	// else: encoding is "reserved"
	}
	params.level = 2;
	params.lineSize  = bits(reg,  0,  7);
	params.sharedBy  = 1;
	params.totalSize = bits(reg, 16, 31)*KiB;
	return params;
}

// (same as L2 except for the totalSize encoding)
static x86_x64_CacheParameters L3Parameters(u32 reg, x86_x64_CacheType type)
{
	x86_x64_CacheParameters params = L2Parameters(reg, type);
	params.level = 3;
	params.totalSize = bits(reg, 18, 31)*512*KiB;	// (rounded down)
	return params;
}

static x86_x64_TLBParameters TLB1Parameters(u32 reg, size_t bitOffset, size_t pageSize, x86_x64_CacheType type)
{
	x86_x64_TLBParameters params;
	params.type          = type;
	params.level         = 1;
	params.associativity = bits(reg, bitOffset+8, bitOffset+15);
	params.pageSize      = pageSize;
	params.entries       = bits(reg, bitOffset, bitOffset+7);
	return params;
}

static void AddTLB1Parameters(const x86_x64_CpuidRegs& regs)
{
	AddTLBParameters(TLB1Parameters(regs.eax,  0, 2*MiB, X86_X64_CACHE_TYPE_INSTRUCTION));
	AddTLBParameters(TLB1Parameters(regs.eax, 16, 2*MiB, X86_X64_CACHE_TYPE_DATA));
	AddTLBParameters(TLB1Parameters(regs.ebx,  0, 4*KiB, X86_X64_CACHE_TYPE_INSTRUCTION));
	AddTLBParameters(TLB1Parameters(regs.ebx, 16, 4*KiB, X86_X64_CACHE_TYPE_DATA));
}

static x86_x64_TLBParameters TLB2Parameters(u32 reg, size_t bitOffset, size_t pageSize, x86_x64_CacheType type)
{
	x86_x64_TLBParameters params;
	const size_t associativityIndex = bits(reg, bitOffset+12, bitOffset+15);
	if(associativityIndex == 0)	// disabled
	{
		params.type = X86_X64_CACHE_TYPE_NULL;
		params.associativity = 0;
	}
	else
	{
		params.type = type;
		params.associativity = associativities[associativityIndex];
	}
	params.level    = 2;
	params.pageSize = pageSize;
	params.entries  = bits(reg, bitOffset, bitOffset+11);
	return params;
}

static void AddTLB2ParameterPair(u32 reg, size_t pageSize)
{
	x86_x64_CacheType type = X86_X64_CACHE_TYPE_UNIFIED;
	if(bits(reg, 16, 31) != 0)	// not unified
	{
		AddTLBParameters(TLB2Parameters(reg, 16, pageSize, X86_X64_CACHE_TYPE_DATA));
		type = X86_X64_CACHE_TYPE_INSTRUCTION;
	}
	AddTLBParameters(TLB2Parameters(reg, 0, pageSize, type));
}

// AMD reports maxCpuidIdFunction > 4 but consider functions 2..4 to be
// "reserved". cache characteristics are returned via ext. functions.
static void DetectCacheAndTLB()
{
	x86_x64_CpuidRegs regs = { 0 };

	regs.eax = 0x80000005;
	if(x86_x64_cpuid(&regs))
	{
		AddTLB1Parameters(regs);

		dcache.levels = icache.levels = 1;
		dcache.parameters[0] = L1Parameters(regs.ecx, X86_X64_CACHE_TYPE_DATA);
		icache.parameters[0] = L1Parameters(regs.edx, X86_X64_CACHE_TYPE_INSTRUCTION);
	}

	regs.eax = 0x80000006;
	if(x86_x64_cpuid(&regs))
	{
		AddTLB2ParameterPair(regs.eax, 2*MiB);
		AddTLB2ParameterPair(regs.ebx, 4*KiB);

		icache.levels = dcache.levels = 2;
		icache.parameters[1] = dcache.parameters[1] = L2Parameters(regs.ecx, X86_X64_CACHE_TYPE_UNIFIED);

		icache.levels = dcache.levels = 3;
		icache.parameters[2] = dcache.parameters[2] = L3Parameters(regs.edx, X86_X64_CACHE_TYPE_UNIFIED);
	}
}

}	// namespace AMD

// note: CPUID 8000_0006 also returns L2 size, associativity, and
// line size, but I don't see any advantages vs. CPUID 4.

static void DetectCache_CPUID4()
{
	// note: ordering is undefined (see Intel AP-485)
	for(u32 count = 0; ; count++)
	{
		x86_x64_CpuidRegs regs = { 0 };
		regs.eax = 4;
		regs.ecx = count;
		if(!x86_x64_cpuid(&regs))
		{
			debug_assert(0);
			break;
		}

		const x86_x64_CacheType type = (x86_x64_CacheType)bits(regs.eax, 0, 4);
		const size_t level = (size_t)bits(regs.eax, 5, 7);
		if(type == X86_X64_CACHE_TYPE_NULL)	// no more remaining
			break;

		x86_x64_CacheParameters params;
		params.type          = type;
		params.level         = level;
		params.associativity = (size_t)bits(regs.ebx, 22, 31)+1;
		params.lineSize      = (size_t)bits(regs.ebx,  0, 11)+1;	// (yes, this also uses +1 encoding)
		params.sharedBy      = (size_t)bits(regs.eax, 14, 25)+1;
		{
			const size_t partitions = (size_t)bits(regs.ebx, 12, 21)+1;
			const size_t sets = (size_t)bits(regs.ecx, 0, 31)+1;
			params.totalSize = params.associativity * partitions * params.lineSize * sets;
		}

		if(type == X86_X64_CACHE_TYPE_INSTRUCTION || type == X86_X64_CACHE_TYPE_UNIFIED)
		{
			icache.levels = std::max(icache.levels, level);
			icache.parameters[level-1] = params;
		}
		if(type == X86_X64_CACHE_TYPE_DATA || type == X86_X64_CACHE_TYPE_UNIFIED)
		{
			dcache.levels = std::max(dcache.levels, level);
			dcache.parameters[level-1] = params;
		}
	}
}

static void ExtractDescriptors(u32 reg, std::vector<u8>& descriptors)
{
	if(IsBitSet(reg, 31))	// register contents are reserved
		return;
	for(int pos = 24; pos >= 0; pos -= 8)
	{
		const u8 descriptor = (u8)bits(reg, pos, pos+7);
		if(descriptor != 0)
			descriptors.push_back(descriptor);
	}
}

// note: the following cannot reside in DecodeDescriptor because
// ARRAY_SIZE's template argument must not reference a local type.

enum Flags
{
	// type (unified := neither bit set)
	I   = 0x01,
	D   = 0x02,

	// level
	L2  = 0x04,

	// size
	S4K = 0x08,
	S4M = 0x10,
	S2M = 0x20
};

struct Properties
{
	int flags;	// pageSize, type, level
	u8 descriptor;
	u8 associativity;
	u16 entries;
};

static const u8 F = x86_x64_fullyAssociative;

#define PROPERTIES(descriptor, flags, assoc, entries) { flags, descriptor, assoc, entries }

// references: [accessed 2009-01-05]
// AP485 http://download.intel.com/design/processor/applnots/241618033.pdf
// sandp http://www.sandpile.org/ia32/cpuid.htm
// opsol http://src.opensolaris.org/source/xref/onnv/onnv-gate/usr/src/uts/i86pc/os/cpuid.c
static const Properties propertyTable[] =
{
	PROPERTIES(0x01, I|S4K, 4,  32),
	PROPERTIES(0x02, I|S4M, F,   2),
	PROPERTIES(0x03, D|S4K, 4,  64),
	PROPERTIES(0x04, D|S4M, 4,   8),
	PROPERTIES(0x05, D|S4M, 4,  32),
	PROPERTIES(0x0B, I|S4M, 4,   4),
	PROPERTIES(0x4F, I|S4K, F,  32),	// sandp: unknown assoc, opsol: full, AP485: unmentioned
	PROPERTIES(0x50, I|S4K, F,  64),
	PROPERTIES(0x50, I|S4M, F,  64),
	PROPERTIES(0x50, I|S2M, F,  64),
	PROPERTIES(0x51, I|S4K, F, 128),
	PROPERTIES(0x51, I|S4M, F, 128),
	PROPERTIES(0x51, I|S2M, F, 128),
	PROPERTIES(0x52, I|S4K, F, 256),
	PROPERTIES(0x52, I|S4M, F, 256),
	PROPERTIES(0x52, I|S2M, F, 256),
	PROPERTIES(0x55, I|S4M, F,   7),
	PROPERTIES(0x55, I|S2M, F,   7),
	PROPERTIES(0x56, D|S4M, 4,  16),
	PROPERTIES(0x57, D|S4K, 4,  16),
	PROPERTIES(0x59, D|S4K, F,  16),
	PROPERTIES(0x5A, D|S4M, 4,  32),
	PROPERTIES(0x5A, D|S2M, 4,  32),
	PROPERTIES(0x5B, D|S4K, F,  64),
	PROPERTIES(0x5B, D|S4M, F,  64),
	PROPERTIES(0x5C, D|S4K, F, 128),
	PROPERTIES(0x5C, D|S4M, F, 128),
	PROPERTIES(0x5D, D|S4K, F, 256),
	PROPERTIES(0x5D, D|S4M, F, 256),
	PROPERTIES(0xB0, I|S4K, 4, 128),
	PROPERTIES(0xB1, I|S2M, 4,   8),
	PROPERTIES(0xB1, I|S4M, 4,   4),
	PROPERTIES(0xB2, I|S4K, 4,  64),
	PROPERTIES(0xB3, D|S4K, 4, 128),
	PROPERTIES(0xB3, D|S4M, 4, 128),
	PROPERTIES(0xB4, D|S4K, 4, 256),
	PROPERTIES(0xB4, D|S4M, 4, 256),
	PROPERTIES(0xBA, D|S4K, 4,  64),
	PROPERTIES(0xC0, D|S4K, 4,   8),
	PROPERTIES(0xC0, D|S4M, 4,   8),
	PROPERTIES(0xCA,   S4K|L2, 4, 512),
};
#undef PROPERTIES

static void DecodeDescriptor(u8 descriptor)
{
	// note: we can't use bsearch because propertyTable may contain multiple
	// entries with the same descriptor key.
	for(size_t i = 0; i < ARRAY_SIZE(propertyTable); i++)
	{
		const Properties& properties = propertyTable[i];
		if(properties.descriptor != descriptor)
			continue;

		const size_t flags = properties.flags;
		x86_x64_CacheType type = X86_X64_CACHE_TYPE_UNIFIED;
		if(flags & D)
			type = X86_X64_CACHE_TYPE_DATA;
		else if(flags & I)
			type = X86_X64_CACHE_TYPE_INSTRUCTION;
		const size_t level = (flags & L2)? 2 : 1;
		size_t pageSize = 0;
		if(flags & S4K)
			pageSize = 4*KiB;
		else if(flags & S4M)
			pageSize = 4*MiB;
		else if(flags & S2M)
			pageSize = 2*MiB;
		else
			debug_assert(0);

		x86_x64_TLBParameters params;
		params.type          = type;
		params.level         = level;
		params.associativity = properties.associativity;
		params.pageSize      = pageSize;
		params.entries       = properties.entries;

		if(type == X86_X64_CACHE_TYPE_INSTRUCTION || type == X86_X64_CACHE_TYPE_UNIFIED)
		{
			if(itlb.numParameters < maxTLBParams)
				itlb.parameters[itlb.numParameters++] = params;
			else
				debug_assert(0);
		}
		if(type == X86_X64_CACHE_TYPE_DATA || type == X86_X64_CACHE_TYPE_UNIFIED)
		{
			if(dtlb.numParameters < maxTLBParams)
				dtlb.parameters[dtlb.numParameters++] = params;
			else
				debug_assert(0);
		}

		return;	// success
	}
}

static void DetectTLB_CPUID2()
{
	// TODO: ensure we are pinned to the same CPU

	// extract descriptors
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 2;
	if(!x86_x64_cpuid(&regs))
		return;
	size_t iterations = bits(regs.eax, 0, 7);
	std::vector<u8> descriptors;
	for(;;)
	{
		ExtractDescriptors(bits(regs.eax, 8, 31), descriptors);
		ExtractDescriptors(regs.ebx, descriptors);
		ExtractDescriptors(regs.ecx, descriptors);
		ExtractDescriptors(regs.edx, descriptors);
		if(--iterations == 0)
			break;
		regs.eax = 2;
		const bool ok = x86_x64_cpuid(&regs);
		debug_assert(ok);
	}

	for(std::vector<u8>::const_iterator it = descriptors.begin(); it != descriptors.end(); ++it)
	{
		const u8 descriptor = *it;
		DecodeDescriptor(descriptor);
	}
}

static ModuleInitState cacheInitState;

static LibError DetectCacheAndTLB()
{
	if(x86_x64_Vendor() == X86_X64_VENDOR_AMD)
		AMD::DetectCacheAndTLB();
	else
	{
		DetectCache_CPUID4();
		DetectTLB_CPUID2();
	}

	// sanity check: cache type must match that of the data structure
	for(size_t i = 0; i < dcache.levels; i++)
		debug_assert(dcache.parameters[i].type != X86_X64_CACHE_TYPE_INSTRUCTION);
	for(size_t i = 0; i < icache.levels; i++)
		debug_assert(icache.parameters[i].type != X86_X64_CACHE_TYPE_DATA);
	for(size_t i = 0; i < dtlb.numParameters; i++)
		debug_assert(dtlb.parameters[i].type != X86_X64_CACHE_TYPE_INSTRUCTION);
	for(size_t i = 0; i < itlb.numParameters; i++)
		debug_assert(itlb.parameters[i].type != X86_X64_CACHE_TYPE_DATA);

	// ensure x86_x64_L1CacheLineSize and x86_x64_L2CacheLineSize will work
	debug_assert(dcache.levels >= 2);
	debug_assert(dcache.parameters[0].lineSize != 0);
	debug_assert(dcache.parameters[1].lineSize != 0);

	return INFO::OK;
}

const x86_x64_Cache* x86_x64_ICache()
{
	ModuleInit(&cacheInitState, DetectCacheAndTLB);
	return &icache;
}

const x86_x64_Cache* x86_x64_DCache()
{
	ModuleInit(&cacheInitState, DetectCacheAndTLB);
	return &dcache;
}

size_t x86_x64_L1CacheLineSize()
{
	return x86_x64_DCache()->parameters[0].lineSize;
}

size_t x86_x64_L2CacheLineSize()
{
	return x86_x64_DCache()->parameters[1].lineSize;
}

const x86_x64_TLB* x86_x64_ITLB()
{
	ModuleInit(&cacheInitState, DetectCacheAndTLB);
	return &itlb;
}

const x86_x64_TLB* x86_x64_DTLB()
{
	ModuleInit(&cacheInitState, DetectCacheAndTLB);
	return &dtlb;
}

size_t x86_x64_TLBCoverage(const x86_x64_TLB* tlb)
{
	// note: receiving a TLB pointer means DetectCacheAndTLB was called.

	const u64 pageSize = 4*KiB;
	const u64 largePageSize = 4*MiB;	// TODO: find out if we're using 2MB or 4MB
	u64 totalSize = 0;	// [bytes]
	for(size_t i = 0; i < tlb->numParameters; i++)
	{
		const x86_x64_TLBParameters& params = tlb->parameters[i];
		if(params.pageSize == pageSize)
			totalSize += pageSize * params.entries;
		if(params.pageSize == largePageSize)
			totalSize += largePageSize * params.entries;
	}

	return size_t(totalSize / MiB);
}


//-----------------------------------------------------------------------------
// identifier string

/// functor to remove substrings from the CPU identifier string
class StringStripper
{
public:
	StringStripper(char* string, size_t max_chars)
		: m_string(string), m_max_chars(max_chars)
	{
	}

	// remove all instances of substring from m_string
	void operator()(const char* substring)
	{
		const size_t substring_length = strlen(substring);
		for(;;)
		{
			char* substring_pos = strstr(m_string, substring);
			if(!substring_pos)
				break;
			const size_t substring_ofs = substring_pos - m_string;
			const size_t num_chars = m_max_chars - substring_ofs - substring_length;
			memmove(substring_pos, substring_pos+substring_length, num_chars);
		}
	}

private:
	char* m_string;
	size_t m_max_chars;
};

// 3 calls x 4 registers x 4 bytes = 48 + 0-terminator
static char identifierString[48+1];

static LibError InitIdentifierString()
{
	// get brand string (if available)
	char* pos = identifierString;
	bool gotBrandString = true;
	for(u32 function = 0x80000002; function <= 0x80000004; function++)
	{
		x86_x64_CpuidRegs regs = { 0 };
		regs.eax = function;
		gotBrandString &= x86_x64_cpuid(&regs);
		memcpy(pos, &regs, 16);
		pos += 16;
	}

	// fall back to manual detect of CPU type because either:
	// - CPU doesn't support brand string (we use a flag to indicate this
	//   rather than comparing against a default value because it is safer);
	// - the brand string is useless, e.g. "Unknown". this happens on
	//   some older boards whose BIOS reprograms the string for CPUs it
	//   doesn't recognize.
	if(!gotBrandString || strncmp(identifierString, "Unknow", 6) == 0)
	{
		InitModelAndFamily();
		switch(x86_x64_Vendor())
		{
		case X86_X64_VENDOR_AMD:
			// everything else is either too old, or should have a brand string.
			if(family == 6)
			{
				if(model == 3 || model == 7)
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "AMD Duron");
				else if(model <= 5)
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "AMD Athlon");
				else
				{
					if(x86_x64_cap(X86_X64_CAP_AMD_MP))
						strcpy_s(identifierString, ARRAY_SIZE(identifierString), "AMD Athlon MP");
					else
						strcpy_s(identifierString, ARRAY_SIZE(identifierString), "AMD Athlon XP");
				}
			}
			break;

		case X86_X64_VENDOR_INTEL:
			// everything else is either too old, or should have a brand string.
			if(family == 6)
			{
				if(model == 1)
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "Intel Pentium Pro");
				else if(model == 3 || model == 5)
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "Intel Pentium II");
				else if(model == 6)
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "Intel Celeron");	
				else
					strcpy_s(identifierString, ARRAY_SIZE(identifierString), "Intel Pentium III");
			}
			break;
		}
	}
	// identifierString already holds a valid brand string; pretty it up.
	else
	{
		const char* const undesiredStrings[] = { "(tm)", "(TM)", "(R)", "CPU ", "          " };
		std::for_each(undesiredStrings, undesiredStrings+ARRAY_SIZE(undesiredStrings),
			StringStripper(identifierString, strlen(identifierString)+1));

		// note: Intel brand strings include a frequency, but we can't rely
		// on it because the CPU may be overclocked. we'll leave it in the
		// string to show measurement accuracy and if SpeedStep is active.
	}

	return INFO::OK;
}

const char* cpu_IdentifierString()
{
	static ModuleInitState initState;
	ModuleInit(&initState, InitIdentifierString);
	return identifierString;
}


//-----------------------------------------------------------------------------
// miscellaneous stateless functions

// these routines do not call ModuleInit (because some of them are
// time-critical, e.g. cpu_Serialize) and should also avoid the
// other x86_x64* functions and their global state.
// in particular, use cpuid_impl instead of x86_x64_cpuid.

u8 x86_x64_ApicId()
{
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 1;
	// note: CPUID function 1 is always supported, but only processors with
	// an xAPIC (e.g. P4/Athlon XP) will return a nonzero ID.
	cpuid_impl(&regs);
	const u8 apicId = (u8)bits(regs.ebx, 24, 31);
	return apicId;
}


#if !MSC_VERSION	// replaced by macro
u64 x86_x64_rdtsc()
{
#if GCC_VERSION
	// GCC supports "portable" assembly for both x86 and x64
	volatile u32 lo, hi;
	asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	return u64_from_u32(hi, lo);
#endif
}
#endif


void x86_x64_DebugBreak()
{
#if MSC_VERSION
	__debugbreak();
#elif GCC_VERSION
	// note: this probably isn't necessary, since unix_debug_break
	// (SIGTRAP) is most probably available if GCC_VERSION.
	// we include it for completeness, though.
	__asm__ __volatile__ ("int $3");
#endif
}


void cpu_Serialize()
{
	x86_x64_CpuidRegs regs = { 0 };
	regs.eax = 1;
	cpuid_impl(&regs);	// CPUID serializes execution.
}


//-----------------------------------------------------------------------------
// CPU frequency

// set scheduling priority and restore when going out of scope.
class ScopedSetPriority
{
public:
	ScopedSetPriority(int newPriority)
	{
		// get current scheduling policy and priority
		pthread_getschedparam(pthread_self(), &m_oldPolicy, &m_oldParam);

		// set new priority
		sched_param newParam = {0};
		newParam.sched_priority = newPriority;
		pthread_setschedparam(pthread_self(), SCHED_FIFO, &newParam);
	}

	~ScopedSetPriority()
	{
		// restore previous policy and priority.
		pthread_setschedparam(pthread_self(), m_oldPolicy, &m_oldParam);
	}

private:
	int m_oldPolicy;
	sched_param m_oldParam;
};

// note: this function uses timer.cpp!timer_Time, which is implemented via
// whrt.cpp on Windows.
double x86_x64_ClockFrequency()
{
	// if the TSC isn't available, there's really no good way to count the
	// actual CPU clocks per known time interval, so bail.
	// note: loop iterations ("bogomips") are not a reliable measure due
	// to differing IPC and compiler optimizations.
	if(!x86_x64_cap(X86_X64_CAP_TSC))
		return -1.0;	// impossible value

	// increase priority to reduce interference while measuring.
	const int priority = sched_get_priority_max(SCHED_FIFO)-1;
	ScopedSetPriority ssp(priority);

	// note: no need to "warm up" cpuid - it will already have been
	// called several times by the time this code is reached.
	// (background: it's used in x86_x64_rdtsc() to serialize instruction flow;
	// the first call is documented to be slower on Intel CPUs)

	size_t numSamples = 16;
	// if clock is low-res, do less samples so it doesn't take too long.
	// balance measuring time (~ 10 ms) and accuracy (< 1 0/00 error -
	// ok for using the TSC as a time reference)
	if(timer_Resolution() >= 1e-3)
		numSamples = 8;
	std::vector<double> samples(numSamples);

	for(size_t i = 0; i < numSamples; i++)
	{
		double dt;
		i64 dc;	// (i64 instead of u64 for faster conversion to double)

		// count # of clocks in max{1 tick, 1 ms}:
		// .. wait for start of tick.
		const double t0 = timer_Time();
		u64 c1; double t1;
		do
		{
			// note: timer_Time effectively has a long delay (up to 5 us)
			// before returning the time. we call it before x86_x64_rdtsc to
			// minimize the delay between actually sampling time / TSC,
			// thus decreasing the chance for interference.
			// (if unavoidable background activity, e.g. interrupts,
			// delays the second reading, inaccuracy is introduced).
			t1 = timer_Time();
			c1 = x86_x64_rdtsc();
		}
		while(t1 == t0);
		// .. wait until start of next tick and at least 1 ms elapsed.
		do
		{
			const double t2 = timer_Time();
			const u64 c2 = x86_x64_rdtsc();
			dc = (i64)(c2 - c1);
			dt = t2 - t1;
		}
		while(dt < 1e-3);

		// .. freq = (delta_clocks) / (delta_seconds);
		//    x86_x64_rdtsc/timer overhead is negligible.
		const double freq = dc / dt;
		samples[i] = freq;
	}

	std::sort(samples.begin(), samples.end());

	// median filter (remove upper and lower 25% and average the rest).
	// note: don't just take the lowest value! it could conceivably be
	// too low, if background processing delays reading c1 (see above).
	double sum = 0.0;
	const int lo = numSamples/4, hi = 3*numSamples/4;
	for(int i = lo; i < hi; i++)
		sum += samples[i];

	const double clockFrequency = sum / (hi-lo);
	return clockFrequency;
}
