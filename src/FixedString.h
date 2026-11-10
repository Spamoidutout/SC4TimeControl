// FixedString.h - a fixed-capacity cIGZString with no heap, no CRT, no STL.
//
// The game's window and text functions take a cIGZString (SetCaption,
// GZMsgBox...). This one can live in static storage: declare it
// `constinit` (its constructor is constexpr) so no global constructor is
// needed. Usable in any DLL; in a normal (CRT/STL) project you may use the
// SDK's string classes instead.
#pragma once
#include "cIGZString.h"

namespace FixedStringDetail
{
	constexpr uint32_t kMaxChars = 1024;

	inline uint32_t StrLen(const char* s)
	{
		uint32_t n = 0;
		while (s[n]) ++n;
		return n;
	}

	inline char ToLowerAscii(char c)
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
	}
}

class FixedString final : public cIGZString
{
public:
	constexpr FixedString() : refCount(0), length(0), buffer{} {}

	bool QueryInterface(uint32_t riid, void** ppvObj) override
	{
		if (riid == GZIID_cIGZString || riid == GZIID_cIGZUnknown)
		{
			*ppvObj = this;
			AddRef();
			return true;
		}
		return false;
	}
	uint32_t AddRef() override { return ++refCount; }
	uint32_t Release() override { if (refCount > 0) --refCount; return refCount; }

	void FromChar(char const* s) override { Set(s, s ? FixedStringDetail::StrLen(s) : 0); }
	void FromChar(char const* s, uint32_t n) override { Set(s, n); }
	char const* ToChar() const override { return buffer; }
	char const* Data() const override { return buffer; }
	uint32_t Strlen() const override { return length; }

	bool IsEqual(cIGZString const* other, bool cs) const override { return other && IsEqual(other->ToChar(), other->Strlen(), cs); }
	bool IsEqual(cIGZString const& other, bool cs) const override { return IsEqual(other.ToChar(), other.Strlen(), cs); }
	bool IsEqual(char const* s, uint32_t n, bool cs) const override { return CompareTo(s, n, cs) == 0; }

	int32_t CompareTo(cIGZString const& other, bool cs) const override { return CompareTo(other.ToChar(), other.Strlen(), cs); }
	int32_t CompareTo(char const* s, uint32_t n, bool cs) const override
	{
		const uint32_t common = n < length ? n : length;
		for (uint32_t i = 0; i < common; ++i)
		{
			char a = buffer[i], b = s[i];
			if (!cs) { a = FixedStringDetail::ToLowerAscii(a); b = FixedStringDetail::ToLowerAscii(b); }
			if (a != b) return static_cast<unsigned char>(a) < static_cast<unsigned char>(b) ? -1 : 1;
		}
		return length == n ? 0 : (length < n ? -1 : 1);
	}

	cIGZString& operator=(cIGZString const& other) override { Copy(other); return *this; }
	void Copy(cIGZString const& other) override { if (&other != this) Set(other.ToChar(), other.Strlen()); }

	void Resize(uint32_t n) override
	{
		if (n > FixedStringDetail::kMaxChars - 1) n = FixedStringDetail::kMaxChars - 1;
		for (uint32_t i = length; i < n; ++i) buffer[i] = ' ';
		length = n;
		buffer[length] = 0;
	}

	cIGZString* Append(char const* s, uint32_t n) override { return Insert(length, s, n); }
	cIGZString* Append(cIGZString const& other) override { return Append(other.ToChar(), other.Strlen()); }

	cIGZString* Insert(uint32_t pos, char const* s, uint32_t n) override
	{
		if (pos > length) pos = length;
		if (length + n > FixedStringDetail::kMaxChars - 1) n = FixedStringDetail::kMaxChars - 1 - length;
		for (uint32_t i = length; i > pos; --i) buffer[i - 1 + n] = buffer[i - 1];
		for (uint32_t i = 0; i < n; ++i) buffer[pos + i] = s[i];
		length += n;
		buffer[length] = 0;
		return this;
	}
	cIGZString* Insert(uint32_t pos, cIGZString const& other) override { return Insert(pos, other.ToChar(), other.Strlen()); }

	cIGZString* Replace(uint32_t pos, char const* s, uint32_t n) override
	{
		if (pos > length) pos = length;
		for (uint32_t i = 0; i < n && pos + i < FixedStringDetail::kMaxChars - 1; ++i)
		{
			buffer[pos + i] = s[i];
			if (pos + i >= length) length = pos + i + 1;
		}
		buffer[length] = 0;
		return this;
	}
	cIGZString* Replace(uint32_t pos, cIGZString const& other) override { return Replace(pos, other.ToChar(), other.Strlen()); }

	cIGZString* Erase(uint32_t start, uint32_t end) override
	{
		if (end > length) end = length;
		if (start >= end) return this;
		const uint32_t n = end - start;
		for (uint32_t i = end; i <= length; ++i) buffer[i - n] = buffer[i];
		length -= n;
		return this;
	}

	int32_t Find(char const* s, uint32_t pos, bool cs) const override
	{
		const uint32_t n = FixedStringDetail::StrLen(s);
		for (uint32_t i = pos; i + n <= length; ++i) if (Matches(i, s, n, cs)) return static_cast<int32_t>(i);
		return -1;
	}
	int32_t Find(cIGZString const& other, uint32_t pos, bool cs) const override { return Find(other.ToChar(), pos, cs); }
	int32_t RFind(char const* s, uint32_t pos, bool cs) const override
	{
		const uint32_t n = FixedStringDetail::StrLen(s);
		if (n > length) return -1;
		uint32_t i = length - n;
		if (pos < i) i = pos;
		for (;; --i)
		{
			if (Matches(i, s, n, cs)) return static_cast<int32_t>(i);
			if (i == 0) break;
		}
		return -1;
	}
	int32_t RFind(cIGZString const& other, uint32_t pos, bool cs) const override { return RFind(other.ToChar(), pos, cs); }

	// No CRT formatting available; keep the text so the object stays usable.
	cIGZString* Sprintf(char const* format, ...) override { FromChar(format); return this; }

private:
	void Set(const char* s, uint32_t n)
	{
		if (!s) n = 0;
		if (n > FixedStringDetail::kMaxChars - 1) n = FixedStringDetail::kMaxChars - 1;
		for (uint32_t i = 0; i < n; ++i) buffer[i] = s[i];
		length = n;
		buffer[length] = 0;
	}

	bool Matches(uint32_t at, const char* s, uint32_t n, bool cs) const
	{
		for (uint32_t j = 0; j < n; ++j)
		{
			char a = buffer[at + j], b = s[j];
			if (!cs) { a = FixedStringDetail::ToLowerAscii(a); b = FixedStringDetail::ToLowerAscii(b); }
			if (a != b) return false;
		}
		return true;
	}

	uint32_t refCount;
	uint32_t length;
	char buffer[FixedStringDetail::kMaxChars];
};
