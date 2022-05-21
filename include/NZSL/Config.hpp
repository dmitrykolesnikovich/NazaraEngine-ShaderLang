/*
	Nazara Shading Language (NZSL)

	Copyright (C) 2015 Jérôme "Lynix" Leclercq (Lynix680@gmail.com)

	Permission is hereby granted, free of charge, to any person obtaining a copy of
	this software and associated documentation files (the "Software"), to deal in
	the Software without restriction, including without limitation the rights to
	use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
	of the Software, and to permit persons to whom the Software is furnished to do
	so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#ifndef NZSL_PREREQUISITES_HPP
#define NZSL_PREREQUISITES_HPP

#include <Nazara/Prerequisites.hpp>

// Nazara version macro
#define NZSL_VERSION_MAJOR 0
#define NZSL_VERSION_MINOR 1
#define NZSL_VERSION_PATCH 0

// Try to identify target platform via defines
#if defined(_WIN32)
	#define NZSL_PLATFORM_DESKTOP
	#define NZSL_PLATFORM_WINDOWS

	#define NZSL_EXPORT __declspec(dllexport)
	#define NZSL_IMPORT __declspec(dllimport)
#elif defined(__linux__) || defined(__unix__)
	#define NZSL_PLATFORM_DESKTOP
	#define NZSL_PLATFORM_LINUX
	#define NZSL_PLATFORM_POSIX

	#define NZSL_EXPORT __attribute__((visibility ("default")))
	#define NZSL_IMPORT __attribute__((visibility ("default")))
#elif defined(__APPLE__)
	#include <TargetConditionals.h>
	#if TARGET_OS_IPHONE
		#define NZSL_PLATFORM_IOS
	#else
		#define NZSL_PLATFORM_DESKTOP
		#define NZSL_PLATFORM_MACOS
	#endif
	#define NZSL_PLATFORM_POSIX

	#define NZSL_EXPORT __attribute__((visibility ("default")))
	#define NZSL_IMPORT __attribute__((visibility ("default")))
#else
	#error This operating system is not fully supported by the Nazara Shading Language

	#define NZSL_PLATFORM_UNKNOWN
#endif

#if !defined(NZSL_STATIC)
	#ifdef NZSL_BUILD
		#define NZSL_API NAZARA_EXPORT
	#else
		#define NZSL_API NAZARA_IMPORT
	#endif
#else
	#define NZSL_API
#endif

#include <cstdint>

#endif // NZSL_PREREQUISITES_HPP