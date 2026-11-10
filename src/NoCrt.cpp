// NoCrt.cpp - the few CRT symbols the compiler may call on its own.
// Compile this file ONLY for a build without the CRT (/NODEFAULTLIB).
#include <stddef.h>

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset, memcpy)
#endif

extern "C"
{
	void* __cdecl memset(void* dst, int c, size_t n)
	{
		unsigned char* d = static_cast<unsigned char*>(dst);
		while (n--) *d++ = static_cast<unsigned char>(c);
		return dst;
	}

	void* __cdecl memcpy(void* dst, const void* src, size_t n)
	{
		unsigned char* d = static_cast<unsigned char*>(dst);
		const unsigned char* s = static_cast<const unsigned char*>(src);
		while (n--) *d++ = *s++;
		return dst;
	}

	// Referenced by the vtables of abstract classes; never called.
	int __cdecl _purecall() { return 0; }
}

// Tells the linker that floating point is used (no CRT to define it).
extern "C" int _fltused = 0;
