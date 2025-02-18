//----------------------------------------------------------------------------------------------------------------------
//
// mz::spin_mutex
// https://github.com/marzer/spin_mutex
// SPDX-License-Identifier: MIT
//
//----------------------------------------------------------------------------------------------------------------------
//         THIS FILE WAS ASSEMBLED FROM MULTIPLE HEADER FILES BY A SCRIPT - PLEASE DON'T EDIT IT DIRECTLY
//----------------------------------------------------------------------------------------------------------------------
//
// MIT License
//
// Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//----------------------------------------------------------------------------------------------------------------------
#ifndef MZ_SPIN_MUTEX_HPP
#define MZ_SPIN_MUTEX_HPP

#define MZ_SPIN_MUTEX_VERSION_MAJOR 1
#define MZ_SPIN_MUTEX_VERSION_MINOR 0
#define MZ_SPIN_MUTEX_VERSION_PATCH 0

#ifndef MZ_MAKE_VERSION
#define MZ_MAKE_VERSION(major, minor, patch) (((major)*10000) + ((minor)*100) + ((patch)))
#endif

#ifndef MZ_INTELLISENSE
#ifdef __INTELLISENSE__
#define MZ_INTELLISENSE 1
#else
#define MZ_INTELLISENSE 0
#endif
#endif

#ifndef MZ_DOXYGEN
#if defined(DOXYGEN) || defined(__DOXYGEN) || defined(__DOXYGEN__) || defined(__doxygen__) || defined(__POXY__)    \
		|| defined(__poxy__)
#define MZ_DOXYGEN 1
#else
#define MZ_DOXYGEN 0
#endif
#endif

#ifndef MZ_CLANG
#ifdef __clang__
#define MZ_CLANG __clang_major__
#else
#define MZ_CLANG 0
#endif


// special handling for apple clang; see:
// - https://github.com/marzer/tomlplusplus/issues/189
// - https://en.wikipedia.org/wiki/Xcode
// -
// https://stackoverflow.com/questions/19387043/how-can-i-reliably-detect-the-version-of-clang-at-preprocessing-time
#if MZ_CLANG && defined(__apple_build_version__)
#undef MZ_CLANG
		#define MZ_CLANG_VERSION MZ_MAKE_VERSION(__clang_major__, __clang_minor__, __clang_patchlevel__)
		#if MZ_CLANG_VERSION >= MZ_MAKE_VERSION(15, 0, 0)
			#define MZ_CLANG 16
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(14, 3, 0)
			#define MZ_CLANG 15
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(14, 0, 0)
			#define MZ_CLANG 14
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(13, 1, 6)
			#define MZ_CLANG 13
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(13, 0, 0)
			#define MZ_CLANG 12
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(12, 0, 5)
			#define MZ_CLANG 11
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(12, 0, 0)
			#define MZ_CLANG 10
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(11, 0, 3)
			#define MZ_CLANG 9
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(11, 0, 0)
			#define MZ_CLANG 8
		#elif MZ_CLANG_VERSION >= MZ_MAKE_VERSION(10, 0, 1)
			#define MZ_CLANG 7
		#else
			#define MZ_CLANG 6 // not strictly correct but doesn't matter below this
		#endif
		#undef MZ_CLANG_VERSION
#endif
#endif

#ifndef MZ_ICC
#ifdef __INTEL_COMPILER
#define MZ_ICC __INTEL_COMPILER
		#ifdef __ICL
			#define MZ_ICC_CL MZ_ICC
		#else
			#define MZ_ICC_CL 0
		#endif
#else
#define MZ_ICC	  0
#define MZ_ICC_CL 0
#endif
#endif

#ifndef MZ_MSVC_LIKE
#ifdef _MSC_VER
#define MZ_MSVC_LIKE _MSC_VER
#else
#define MZ_MSVC_LIKE 0
#endif
#endif

#ifndef MZ_MSVC
#if MZ_MSVC_LIKE && !MZ_CLANG && !MZ_ICC
#define MZ_MSVC MZ_MSVC_LIKE
#else
#define MZ_MSVC 0
#endif
#endif

#ifndef MZ_GCC_LIKE
#ifdef __GNUC__
#define MZ_GCC_LIKE __GNUC__
#else
#define MZ_GCC_LIKE 0
#endif
#endif

#ifndef MZ_GCC
#if MZ_GCC_LIKE && !MZ_CLANG && !MZ_ICC
#define MZ_GCC MZ_GCC_LIKE
#else
#define MZ_GCC 0
#endif
#endif

#ifndef MZ_CUDA
#if defined(__CUDACC__) || defined(__CUDA_ARCH__) || defined(__CUDA_LIBDEVICE__)
#define MZ_CUDA 1
#else
#define MZ_CUDA 0
#endif
#endif

#ifndef MZ_ARCH_ITANIUM
#if defined(__ia64__) || defined(__ia64) || defined(_IA64) || defined(__IA64__) || defined(_M_IA64)
#define MZ_ARCH_ITANIUM 1
		#define MZ_ARCH_BITNESS 64
#else
#define MZ_ARCH_ITANIUM 0
#endif
#endif

#ifndef MZ_ARCH_AMD64
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_AMD64)
#define MZ_ARCH_AMD64	1
		#define MZ_ARCH_BITNESS 64
#else
#define MZ_ARCH_AMD64 0
#endif
#endif

#ifndef MZ_ARCH_X86
#if defined(__i386__) || defined(_M_IX86)
#define MZ_ARCH_X86		1
#define MZ_ARCH_BITNESS 32
#else
#define MZ_ARCH_X86 0
#endif
#endif

#ifndef MZ_ARCH_ARM
#if defined(__aarch64__) || defined(__ARM_ARCH_ISA_A64) || defined(_M_ARM64) || defined(__ARM_64BIT_STATE)         \
		|| defined(_M_ARM64EC)
#define MZ_ARCH_ARM32	0
		#define MZ_ARCH_ARM64	1
		#define MZ_ARCH_ARM		1
		#define MZ_ARCH_BITNESS 64
#elif defined(__arm__) || defined(_M_ARM) || defined(__ARM_32BIT_STATE)
#define MZ_ARCH_ARM32	1
		#define MZ_ARCH_ARM64	0
		#define MZ_ARCH_ARM		1
		#define MZ_ARCH_BITNESS 32
#else
#define MZ_ARCH_ARM32 0
#define MZ_ARCH_ARM64 0
#define MZ_ARCH_ARM	  0
#endif
#endif

#ifndef MZ_ARCH_BITNESS
#define MZ_ARCH_BITNESS 0
#endif

#ifndef MZ_ARCH_X64
#if MZ_ARCH_BITNESS == 64
#define MZ_ARCH_X64 1
#else
#define MZ_ARCH_X64 0
#endif
#endif

#ifndef MZ_HAS_BUILTIN
#ifdef __has_builtin
#define MZ_HAS_BUILTIN(name) __has_builtin(name)
#else
#define MZ_HAS_BUILTIN(name) 0
#endif
#endif

#ifndef MZ_HAS_ATTR
#ifdef __has_attribute
#define MZ_HAS_ATTR(attr) __has_attribute(attr)
#else
#define MZ_HAS_ATTR(attr) 0
#endif
#endif

#ifndef MZ_HAS_CPP_ATTR
#ifdef __has_cpp_attribute
#define MZ_HAS_CPP_ATTR(attr) __has_cpp_attribute(attr)
#else
#define MZ_HAS_CPP_ATTR(attr) 0
#endif
#endif

#ifndef MZ_ATTR
#if MZ_CLANG || MZ_GCC_LIKE
#define MZ_ATTR(...) __attribute__((__VA_ARGS__))
#else
#define MZ_ATTR(...)
#endif
#endif

#ifndef MZ_NODISCARD
#if MZ_HAS_CPP_ATTR(nodiscard) >= 201603
#define MZ_NODISCARD	   [[nodiscard]]
		#define MZ_NODISCARD_CLASS [[nodiscard]]
#elif MZ_CLANG || MZ_GCC_LIKE || MZ_HAS_ATTR(__warn_unused_result__)
#define MZ_NODISCARD MZ_ATTR(__warn_unused_result__)
#else
#define MZ_NODISCARD
#endif
#ifndef MZ_NODISCARD_CLASS
#define MZ_NODISCARD_CLASS
#endif
#if MZ_HAS_CPP_ATTR(nodiscard) >= 201907
#define MZ_NODISCARD_CTOR [[nodiscard]]
#else
#define MZ_NODISCARD_CTOR
#endif
#endif

// msvc-specific
#if !defined(MZ_PAUSE) && MZ_MSVC
#if MZ_ARCH_X86 || MZ_ARCH_AMD64
		#define MZ_PAUSE() _mm_pause()
	#elif MZ_ARCH_ARM
		#define MZ_PAUSE() __yield()
	#endif
#endif

// __builtin_ia32_pause on GCC+clang
#if !defined(MZ_PAUSE) && (MZ_ARCH_X86 || MZ_ARCH_AMD64) && (MZ_CLANG || MZ_GCC || MZ_HAS_BUILTIN(__builtin_ia32_pause))
#define MZ_PAUSE() __builtin_ia32_pause()
#endif

// YieldProcessor() on windows if available
#if !defined(MZ_PAUSE) && MZ_WINDOWS && defined(YieldProcessor)
#define MZ_PAUSE() YieldProcessor()
#endif

// x86 fallback
#if !defined(MZ_PAUSE) && (MZ_ARCH_X86 || MZ_ARCH_AMD64)
#include <immintrin.h>
	#define MZ_PAUSE() _mm_pause()
#endif

// ARM fallback
//#if !defined(MZ_PAUSE) && MZ_ARCH_ARM
//#define MZ_PAUSE() __yield()
//#endif

// no-op
#if !defined(MZ_PAUSE)
#define MZ_PAUSE() static_cast<void>(0)
#endif

#include <atomic>

namespace mz
{
    class MZ_NODISCARD_CLASS spin_mutex
    {
        // implementation is based on this article:
        // https://rigtorp.se/spinlock/
        //
        // increasing spin-wait backoff based on "Intel 64 and IA-32 Architectures Optimization Reference Manual":
        // https://software.intel.com/sites/default/files/managed/9e/bc/64-ia-32-architectures-optimization-manual.pdf

    private:
        std::atomic_bool held_;

    public:
        MZ_NODISCARD_CTOR
        spin_mutex() noexcept //
                : held_{ false }
        {}

        spin_mutex(const spin_mutex&)			 = delete;
        spin_mutex& operator=(const spin_mutex&) = delete;
        spin_mutex(spin_mutex&&)				 = delete;
        spin_mutex& operator=(spin_mutex&&)		 = delete;

        void lock() noexcept
        {
            int mask		  = 1;
            constexpr int max = 64;
            while (held_.exchange(true, std::memory_order_acquire))
            {
                while (held_.load(std::memory_order_relaxed))
                {
                    for (int i = mask; i; --i)
                        MZ_PAUSE();
                    mask = mask < max ? mask << 1 : max;
                }
            }
        }

        MZ_NODISCARD
        bool try_lock() noexcept
        {
            return !held_.load(std::memory_order_relaxed) //
                   && !held_.exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept
        {
            held_.store(false, std::memory_order_release);
        }
    };

}

#endif // MZ_SPIN_MUTEX_HPP