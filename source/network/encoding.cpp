#include "encoding.hpp"
#include <cstdint>

static std::string lowerAscii(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
	return out;
}

static void appendUtf8(std::string& out, uint32_t cp)
{
	if (cp <= 0x7F) out += (char)cp;
	else if (cp <= 0x7FF)
	{
		out += (char)(0xC0 | (cp >> 6));
		out += (char)(0x80 | (cp & 0x3F));
	}
	else if (cp <= 0xFFFF)
	{
		out += (char)(0xE0 | (cp >> 12));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
	else
	{
		out += (char)(0xF0 | (cp >> 18));
		out += (char)(0x80 | ((cp >> 12) & 0x3F));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

static uint32_t cp1252ToUnicode(uint8_t b)
{
	if (b < 0x80) return b;
	if (b >= 0xA0) return b;
	static const uint16_t table[32] = {
		0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x003F, 0x017D, 0x003F,
		0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178
	};
	return table[b - 0x80];
}

static bool isValidUtf8(const std::string& s)
{
	size_t i = 0;
	while (i < s.size())
	{
		uint8_t c = (uint8_t)s[i++];
		if (c < 0x80) continue;
		int extra = (c & 0xE0) == 0xC0 ? 1 :
			((c & 0xF0) == 0xE0 ? 2 : ((c & 0xF8) == 0xF0 ? 3 : -1));
		if (extra < 0 || i + extra > s.size()) return false;
		for (int k = 0; k < extra; ++k)
			if (((uint8_t)s[i++] & 0xC0) != 0x80) return false;
	}
	return true;
}

std::string convertEncodingToUtf8(const std::string& input,
	const std::string& contentType, bool convertText)
{
	if (input.empty() || !convertText) return input;
	const std::string ct = lowerAscii(contentType);
	bool explicitLatin = ct.find("iso-8859") != std::string::npos ||
		ct.find("windows-1252") != std::string::npos ||
		ct.find("latin1") != std::string::npos || ct.find("cp1252") != std::string::npos;
	if (!explicitLatin)
	{
		const size_t headLen = input.size() < 2048 ? input.size() : 2048;
		const std::string head = lowerAscii(input.substr(0, headLen));
		explicitLatin = head.find("iso-8859") != std::string::npos ||
			head.find("windows-1252") != std::string::npos || head.find("latin1") != std::string::npos;
	}
	if (!explicitLatin && isValidUtf8(input)) return input;
	std::string out;
	out.reserve(input.size() + input.size() / 4);
	for (unsigned char b : input)
	{
		if (b < 0x80) out += (char)b;
		else appendUtf8(out, cp1252ToUnicode(b));
	}
	return out;
}
