#include "pdf_reader.hpp"
#include "../storage/storage.hpp"
#include "../common.hpp"
#include "../image/stb_image.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

namespace
{
	const size_t kMaxPdfBytes = 8u * 1024u * 1024u;
	const size_t kMaxStreamBytes = 2u * 1024u * 1024u;
	const size_t kMaxPages = 128;
	using PdfUnicodeMap = std::map<std::string, std::string>;
	struct PdfFontInfo
	{
		PdfUnicodeMap unicode;
		PdfUnicodeMap simpleUnicode;
		bool characterIds = false;
		bool macRoman = false;
		bool bold = false;
		bool italic = false;
		bool monospace = false;
	};
	using PdfFontMaps = std::map<std::string, PdfFontInfo>;

	bool isSpace(char c)
	{
		return c == 0 || c == '\t' || c == '\n' || c == '\f' ||
			c == '\r' || c == ' ';
	}

	std::string pdfHtmlEscape(const std::string& value)
	{
		std::string out;
		for (char c : value)
		{
			switch (c)
			{
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default: out += c; break;
			}
		}
		return out;
	}

	void appendUtf8(std::string& out, unsigned int cp)
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
		else if (cp <= 0x10FFFF)
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	std::string decodePdfBytes(const std::string& bytes)
	{
		static const unsigned short winAnsi[32] =
		{
			0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
			0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
			0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
			0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
		};
		std::string out;
		size_t i = 0;
		bool utf16Be = bytes.size() >= 2 &&
			(unsigned char)bytes[0] == 0xFE &&
			(unsigned char)bytes[1] == 0xFF;
		if (!utf16Be && bytes.size() >= 4 && bytes.size() % 2 == 0)
		{
			size_t zeroHighBytes = 0;
			for (size_t byte = 0; byte < bytes.size(); byte += 2)
				if (bytes[byte] == 0) ++zeroHighBytes;
			utf16Be = zeroHighBytes >= bytes.size() / 4;
		}
		if (utf16Be)
		{
			i = (unsigned char)bytes[0] == 0xFE &&
				(unsigned char)bytes[1] == 0xFF ? 2 : 0;
			while (i + 1 < bytes.size())
			{
				unsigned int cp = ((unsigned char)bytes[i] << 8) |
					(unsigned char)bytes[i + 1];
				i += 2;
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < bytes.size())
				{
					const unsigned int low = ((unsigned char)bytes[i] << 8) |
						(unsigned char)bytes[i + 1];
					if (low >= 0xDC00 && low <= 0xDFFF)
					{
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
						i += 2;
					}
				}
				if (cp == '\n' || cp == '\r' || cp == '\t' ||
					(cp >= 0x20 && !(cp >= 0x7F && cp <= 0x9F)))
					appendUtf8(out, cp);
			}
			return out;
		}
		for (unsigned char c : bytes)
		{
			if (c >= 0x80 && c <= 0x9F)
				appendUtf8(out, winAnsi[c - 0x80]);
			else if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t')
				appendUtf8(out, c);
		}
		return out;
	}

	std::string decodeUnicodeHex(const std::string& bytes)
	{
		std::string out;
		for (size_t i = 0; i + 1 < bytes.size(); i += 2)
		{
			unsigned int cp = ((unsigned char)bytes[i] << 8) |
				(unsigned char)bytes[i + 1];
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < bytes.size())
			{
				const unsigned int low = ((unsigned char)bytes[i + 2] << 8) |
					(unsigned char)bytes[i + 3];
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					i += 2;
				}
			}
			appendUtf8(out, cp);
		}
		if (bytes.size() % 2 != 0)
			appendUtf8(out, (unsigned char)bytes.back());
		return out;
	}

	std::string decodePdfText(const std::string& bytes,
		const PdfFontInfo* font)
	{
		if (!font) return decodePdfBytes(bytes);
		if (font->unicode.empty() && font->characterIds)
		{
			std::string undecodable;
			for (size_t i = 0; i < bytes.size(); i += 2)
				appendUtf8(undecodable, 0xFFFD);
			return undecodable;
		}
		if (!font->characterIds)
		{
			if (font->simpleUnicode.empty() && !font->macRoman)
				return decodePdfBytes(bytes);
			static const unsigned short macRoman[128] =
			{
				0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
				0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
				0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
				0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
				0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
				0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
				0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
				0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
				0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
				0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
				0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
				0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
				0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
				0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
				0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
				0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
			};
			std::string out;
			for (unsigned char byte : bytes)
			{
				const std::string code(1, (char)byte);
				auto found = font->simpleUnicode.find(code);
				if (found != font->simpleUnicode.end())
					out += found->second;
				else if (font->macRoman && byte >= 0x80)
					appendUtf8(out, macRoman[byte - 0x80]);
				else
					out += decodePdfBytes(code);
			}
			return out;
		}
		std::string out;
		size_t pos = 0;
		while (pos < bytes.size())
		{
			size_t matched = 0;
			for (size_t length = std::min((size_t)4, bytes.size() - pos);
				length > 0; --length)
			{
				auto found = font->unicode.find(bytes.substr(pos, length));
				if (found == font->unicode.end()) continue;
				out += found->second;
				matched = length;
				break;
			}
			if (matched == 0)
				appendUtf8(out, 0xFFFD);
			else
				pos += matched;
			if (matched == 0) ++pos;
		}
		return out;
	}

	std::string glyphNameUnicode(const std::string& rawName)
	{
		const size_t suffix = rawName.find('.');
		const std::string name = rawName.substr(0, suffix);
		static const std::map<std::string, unsigned int> glyphs =
		{
			{ "space", 0x20 }, { "nonbreakingspace", 0xA0 },
			{ "Euro", 0x20AC }, { "bullet", 0x2022 },
			{ "ellipsis", 0x2026 }, { "endash", 0x2013 },
			{ "emdash", 0x2014 }, { "quotedblleft", 0x201C },
			{ "quotedblright", 0x201D }, { "quoteleft", 0x2018 },
			{ "quoteright", 0x2019 }, { "guillemotleft", 0xAB },
			{ "guillemotright", 0xBB }, { "fi", 0xFB01 },
			{ "fl", 0xFB02 }, { "dotlessi", 0x131 },
			{ "oe", 0x153 }, { "OE", 0x152 }, { "ae", 0xE6 },
			{ "AE", 0xC6 }, { "oslash", 0xF8 }, { "Oslash", 0xD8 },
			{ "germandbls", 0xDF }, { "copyright", 0xA9 },
			{ "registered", 0xAE }, { "trademark", 0x2122 },
			{ "degree", 0xB0 }, { "plusminus", 0xB1 },
			{ "multiply", 0xD7 }, { "divide", 0xF7 },
			{ "perthousand", 0x2030 }, { "sterling", 0xA3 },
			{ "yen", 0xA5 }, { "cent", 0xA2 },
			{ "section", 0xA7 }, { "paragraph", 0xB6 },
			{ "quotedblbase", 0x201E }, { "quotesinglbase", 0x201A },
			{ "Agrave", 0xC0 }, { "Aacute", 0xC1 }, { "Acircumflex", 0xC2 },
			{ "Atilde", 0xC3 }, { "Adieresis", 0xC4 }, { "Aring", 0xC5 },
			{ "Ccedilla", 0xC7 }, { "Egrave", 0xC8 }, { "Eacute", 0xC9 },
			{ "Ecircumflex", 0xCA }, { "Edieresis", 0xCB },
			{ "Igrave", 0xCC }, { "Iacute", 0xCD }, { "Icircumflex", 0xCE },
			{ "Idieresis", 0xCF }, { "Ntilde", 0xD1 }, { "Ograve", 0xD2 },
			{ "Oacute", 0xD3 }, { "Ocircumflex", 0xD4 }, { "Otilde", 0xD5 },
			{ "Odieresis", 0xD6 }, { "Ugrave", 0xD9 }, { "Uacute", 0xDA },
			{ "Ucircumflex", 0xDB }, { "Udieresis", 0xDC },
			{ "Yacute", 0xDD }, { "agrave", 0xE0 }, { "aacute", 0xE1 },
			{ "acircumflex", 0xE2 }, { "atilde", 0xE3 }, { "adieresis", 0xE4 },
			{ "aring", 0xE5 }, { "ccedilla", 0xE7 }, { "egrave", 0xE8 },
			{ "eacute", 0xE9 }, { "ecircumflex", 0xEA }, { "edieresis", 0xEB },
			{ "igrave", 0xEC }, { "iacute", 0xED }, { "icircumflex", 0xEE },
			{ "idieresis", 0xEF }, { "ntilde", 0xF1 }, { "ograve", 0xF2 },
			{ "oacute", 0xF3 }, { "ocircumflex", 0xF4 }, { "otilde", 0xF5 },
			{ "odieresis", 0xF6 }, { "ugrave", 0xF9 }, { "uacute", 0xFA },
			{ "ucircumflex", 0xFB }, { "udieresis", 0xFC }, { "yacute", 0xFD },
			{ "ydieresis", 0xFF }
		};
		auto known = glyphs.find(name);
		if (known != glyphs.end())
		{
			std::string out;
			appendUtf8(out, known->second);
			return out;
		}
		if (name.size() == 1 &&
			((name[0] >= 'A' && name[0] <= 'Z') ||
			 (name[0] >= 'a' && name[0] <= 'z') ||
			 (name[0] >= '0' && name[0] <= '9')))
			return name;
		if (name.rfind("uni", 0) == 0 && name.size() >= 7 &&
			(name.size() - 3) % 4 == 0)
		{
			std::string out;
			for (size_t i = 3; i + 3 < name.size(); i += 4)
			{
				char* end = nullptr;
				const std::string code = name.substr(i, 4);
				const unsigned long cp = strtoul(code.c_str(), &end, 16);
				if (!end || *end || cp > 0x10FFFF) return std::string();
				appendUtf8(out, (unsigned int)cp);
			}
			return out;
		}
		if (name.size() >= 5 && name[0] == 'u')
		{
			char* end = nullptr;
			const unsigned long cp = strtoul(name.c_str() + 1, &end, 16);
			if (end && !*end && cp <= 0x10FFFF)
			{
				std::string out;
				appendUtf8(out, (unsigned int)cp);
				return out;
			}
		}
		return std::string();
	}

	bool parsePdfString(const std::string& source, size_t& pos,
		std::string& value)
	{
		value.clear();
		if (pos >= source.size()) return false;
		if (source[pos] == '(')
		{
			++pos;
			int depth = 1;
			while (pos < source.size() && depth > 0)
			{
				char c = source[pos++];
				if (c == '\\')
				{
					if (pos >= source.size()) break;
					c = source[pos++];
					switch (c)
					{
					case 'n': value += '\n'; break;
					case 'r': value += '\r'; break;
					case 't': value += '\t'; break;
					case 'b': value += '\b'; break;
					case 'f': value += '\f'; break;
					case '\r':
						if (pos < source.size() && source[pos] == '\n') ++pos;
						break;
					case '\n': break;
					default:
						if (c >= '0' && c <= '7')
						{
							int octal = c - '0';
							for (int i = 0; i < 2 && pos < source.size() &&
								source[pos] >= '0' && source[pos] <= '7'; ++i)
								octal = (octal << 3) + source[pos++] - '0';
							value += (char)(octal & 0xFF);
						}
						else value += c;
						break;
					}
				}
				else if (c == '(')
				{
					++depth;
					value += c;
				}
				else if (c == ')')
				{
					if (--depth > 0) value += c;
				}
				else value += c;
			}
			return depth == 0;
		}
		if (source[pos] != '<' || pos + 1 >= source.size() ||
			source[pos + 1] == '<') return false;
		++pos;
		int high = -1;
		while (pos < source.size() && source[pos] != '>')
		{
			const unsigned char c = (unsigned char)source[pos++];
			if (isSpace((char)c)) continue;
			const int digit = c >= '0' && c <= '9' ? c - '0' :
				(c >= 'A' && c <= 'F' ? c - 'A' + 10 :
					(c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1));
			if (digit < 0) return false;
			if (high < 0) high = digit;
			else
			{
				value += (char)((high << 4) | digit);
				high = -1;
			}
		}
		if (pos >= source.size()) return false;
		++pos;
		if (high >= 0) value += (char)(high << 4);
		return true;
	}

	std::string extractText(const std::string& stream,
		const PdfFontMaps& fontMaps, bool& unsupportedCharacterMap,
		std::vector<PdfTextRun>& runs)
	{
		std::string text;
		bool inText = false;
		std::string activeFont;
		std::string pendingFont;
		std::string pendingText;
		int fontSize = 12;
		int pendingFontSize = fontSize;
		uint32_t textColor = 0xFF111111;
		std::vector<uint32_t> savedTextColors;
		std::vector<double> numericOperands;
		int arrayDepth = 0;
		auto appendText = [&](const std::string& value) {
			if (value.empty()) return;
			text += value;
			const auto font = fontMaps.find(activeFont);
			PdfTextRun run;
			run.text = value;
			run.fontSize = fontSize;
			run.color = textColor;
			if (font != fontMaps.end())
			{
				run.bold = font->second.bold;
				run.italic = font->second.italic;
				run.monospace = font->second.monospace;
			}
			if (!runs.empty() && runs.back().fontSize == run.fontSize &&
				runs.back().bold == run.bold &&
				runs.back().italic == run.italic &&
				runs.back().monospace == run.monospace &&
				runs.back().color == run.color)
				runs.back().text += run.text;
			else
				runs.push_back(std::move(run));
		};
		auto flushPendingText = [&]() {
			appendText(pendingText);
			pendingText.clear();
		};
		for (size_t pos = 0; pos < stream.size();)
		{
			if (stream.compare(pos, 2, "BT") == 0 &&
				(pos == 0 || isSpace(stream[pos - 1])))
			{
				inText = true;
				numericOperands.clear();
				pos += 2;
				continue;
			}
			if (stream.compare(pos, 2, "ET") == 0 &&
				(pos == 0 || isSpace(stream[pos - 1])))
			{
				flushPendingText();
				inText = false;
				numericOperands.clear();
				if (!text.empty() && text.back() != '\n')
					appendText("\n");
				pos += 2;
				continue;
			}
			if (stream[pos] == '(' ||
				(stream[pos] == '<' && pos + 1 < stream.size() &&
				 stream[pos + 1] != '<'))
			{
				std::string value;
				if (!parsePdfString(stream, pos, value))
				{
					if (inText) break;
					++pos;
					continue;
				}
				if (!inText) continue;
				auto font = fontMaps.find(activeFont);
				if (font != fontMaps.end() &&
					font->second.characterIds && font->second.unicode.empty())
					unsupportedCharacterMap = true;
				pendingText += decodePdfText(value, font == fontMaps.end()
					? nullptr : &font->second);
				continue;
			}
			if (stream[pos] == '[')
			{
				if (inText) ++arrayDepth;
				++pos;
				continue;
			}
			if (stream[pos] == ']')
			{
				if (inText && arrayDepth > 0) --arrayDepth;
				++pos;
				continue;
			}
			if (isSpace(stream[pos])) { ++pos; continue; }
			size_t end = pos;
			while (end < stream.size() && !isSpace(stream[end]) &&
				stream[end] != '[' && stream[end] != ']' &&
				stream[end] != '(' && stream[end] != '<') ++end;
			if (end == pos) { ++pos; continue; }
			const std::string token = stream.substr(pos, end - pos);
			if (inText && token.size() > 1 && token[0] == '/')
			{
				pendingFont = token.substr(1);
				pendingFontSize = fontSize;
			}
			else
			{
				char* numberEnd = nullptr;
				const double number = strtod(token.c_str(), &numberEnd);
				if (numberEnd != token.c_str() && *numberEnd == '\0')
				{
					numericOperands.push_back(number);
					if (!pendingFont.empty())
						pendingFontSize = clamp((int)std::lround(std::fabs(number)),
							6, 72);
					if (inText && arrayDepth > 0 && number < -120.0 &&
						!pendingText.empty() && pendingText.back() != ' ')
						pendingText += number < -500.0 ? '\t' : ' ';
					pos = end;
					continue;
				}
			}
			if (token == "Tf")
			{
				activeFont = pendingFont;
				fontSize = pendingFontSize;
				pendingFont.clear();
			}
			else if (token == "rg" && numericOperands.size() >= 3)
			{
				flushPendingText();
				const size_t n = numericOperands.size();
				const unsigned int r = (unsigned int)std::lround(
					fclamp((float)numericOperands[n - 3], 0.0f, 1.0f) * 255.0f);
				const unsigned int g = (unsigned int)std::lround(
					fclamp((float)numericOperands[n - 2], 0.0f, 1.0f) * 255.0f);
				const unsigned int b = (unsigned int)std::lround(
					fclamp((float)numericOperands[n - 1], 0.0f, 1.0f) * 255.0f);
				textColor = 0xFF000000u | (b << 16) | (g << 8) | r;
			}
			else if (token == "g" && !numericOperands.empty())
			{
				flushPendingText();
				const unsigned int gray = (unsigned int)std::lround(
					fclamp((float)numericOperands.back(), 0.0f, 1.0f) * 255.0f);
				textColor = 0xFF000000u | (gray << 16) | (gray << 8) | gray;
			}
			else if (token == "k" && numericOperands.size() >= 4)
			{
				flushPendingText();
				const size_t n = numericOperands.size();
				const float c = fclamp((float)numericOperands[n - 4], 0.0f, 1.0f);
				const float m = fclamp((float)numericOperands[n - 3], 0.0f, 1.0f);
				const float y = fclamp((float)numericOperands[n - 2], 0.0f, 1.0f);
				const float k = fclamp((float)numericOperands[n - 1], 0.0f, 1.0f);
				const unsigned int r = (unsigned int)std::lround(
					(1.0f - std::min(1.0f, c + k)) * 255.0f);
				const unsigned int g = (unsigned int)std::lround(
					(1.0f - std::min(1.0f, m + k)) * 255.0f);
				const unsigned int b = (unsigned int)std::lround(
					(1.0f - std::min(1.0f, y + k)) * 255.0f);
				textColor = 0xFF000000u | (b << 16) | (g << 8) | r;
			}
			else if (token == "q")
			{
				savedTextColors.push_back(textColor);
			}
			else if (token == "Q")
			{
				flushPendingText();
				if (!savedTextColors.empty())
				{
					textColor = savedTextColors.back();
					savedTextColors.pop_back();
				}
			}
			if (token == "Tj" || token == "TJ")
			{
				flushPendingText();
			}
			else if (token == "'" || token == "\"")
			{
				if (!text.empty() && text.back() != '\n') appendText("\n");
				flushPendingText();
			}
			else if (token == "T*" || token == "TD")
			{
				flushPendingText();
				if (!text.empty() && text.back() != '\n')
					appendText("\n");
			}
			numericOperands.clear();
			pos = end;
		}
		flushPendingText();
		while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
			text.pop_back();
		return text;
	}

	bool readObjectBody(const std::string& bytes, const std::string& ref,
		std::string& body);
	bool parsePdfRef(const std::string& source, size_t& pos, std::string& ref);

	bool objectStream(const std::string& bytes, const std::string& objectRef,
		std::string& stream, std::string& error)
	{
		const std::string objectStart = objectRef + " obj";
		size_t object = bytes.find(objectStart);
		if (object == std::string::npos)
		{
			error = "Objet de flux PDF introuvable.";
			return false;
		}
		const size_t endObject = bytes.find("endobj", object);
		if (endObject == std::string::npos)
		{
			error = "Objet de flux PDF incomplet.";
			return false;
		}
		const size_t streamStart = bytes.find("stream", object);
		if (streamStart == std::string::npos || streamStart > endObject ||
			(streamStart + 6 < bytes.size() &&
			 bytes[streamStart + 6] != '\r' && bytes[streamStart + 6] != '\n'))
		{
			error = "Le contenu PDF ne contient pas de flux de page.";
			return false;
		}
		size_t dataStart = streamStart + 6;
		if (dataStart < bytes.size() && bytes[dataStart] == '\r') ++dataStart;
		if (dataStart < bytes.size() && bytes[dataStart] == '\n') ++dataStart;

		const std::string header = bytes.substr(object, streamStart - object);
		size_t dataEnd = std::string::npos;
		const size_t lengthKey = header.find("/Length");
		bool exactLength = false;
		if (lengthKey != std::string::npos)
		{
			size_t lengthPos = lengthKey + 7;
			while (lengthPos < header.size() && isSpace(header[lengthPos]))
				++lengthPos;
			char* lengthEnd = nullptr;
			const unsigned long directLength =
				strtoul(header.c_str() + lengthPos, &lengthEnd, 10);
			size_t afterLength = lengthEnd
				? (size_t)(lengthEnd - header.c_str()) : lengthPos;
			while (afterLength < header.size() && isSpace(header[afterLength]))
				++afterLength;
			const bool indirectLength = afterLength < header.size() &&
				isdigit((unsigned char)header[afterLength]);
			if (lengthEnd != header.c_str() + lengthPos &&
				!indirectLength &&
				directLength <= kMaxStreamBytes &&
				dataStart <= bytes.size() &&
				directLength <= bytes.size() - dataStart)
			{
				dataEnd = dataStart + directLength;
				exactLength = true;
			}
			else
			{
				std::string lengthRef;
				if (parsePdfRef(header, lengthPos, lengthRef))
				{
					std::string lengthBody;
					if (readObjectBody(bytes, lengthRef, lengthBody))
					{
						char* indirectEnd = nullptr;
						const unsigned long indirectLength =
							strtoul(lengthBody.c_str(), &indirectEnd, 10);
						if (indirectEnd != lengthBody.c_str() &&
							indirectLength <= kMaxStreamBytes &&
							dataStart <= bytes.size() &&
							indirectLength <= bytes.size() - dataStart)
						{
							dataEnd = dataStart + indirectLength;
							exactLength = true;
						}
					}
				}
			}
		}
		if (dataEnd == std::string::npos ||
			bytes.compare(dataEnd, 9, "endstream") != 0)
		{
			dataEnd = bytes.find("endstream", dataStart);
		}
		if (dataEnd == std::string::npos || dataEnd > endObject)
		{
			error = "Flux PDF incomplet.";
			return false;
		}
		size_t actualEnd = dataEnd;
		while (!exactLength && actualEnd > dataStart &&
			(bytes[actualEnd - 1] == '\r' || bytes[actualEnd - 1] == '\n'))
			--actualEnd;
		if (actualEnd - dataStart > kMaxStreamBytes)
		{
			error = "Flux PDF trop volumineux pour la New 3DS.";
			return false;
		}
		if (header.find("/FlateDecode") != std::string::npos)
		{
			std::vector<char> decoded(kMaxStreamBytes);
			const int decodedLength = stbi_zlib_decode_buffer(decoded.data(),
				(int)decoded.size(), bytes.data() + dataStart,
				(int)(actualEnd - dataStart));
			if (decodedLength < 0)
			{
				error = "Impossible de decomprimer le flux PDF.";
				return false;
			}
			stream.assign(decoded.data(), (size_t)decodedLength);
		}
		else if (header.find("/Filter") != std::string::npos)
		{
			error = "Ce filtre de compression PDF n'est pas pris en charge.";
			return false;
		}
		else stream.assign(bytes, dataStart, actualEnd - dataStart);
		return true;
	}

	bool parsePdfRef(const std::string& source, size_t& pos, std::string& ref)
	{
		while (pos < source.size() && isSpace(source[pos])) ++pos;
		const size_t firstStart = pos;
		while (pos < source.size() && isdigit((unsigned char)source[pos])) ++pos;
		if (firstStart == pos) return false;
		const std::string object = source.substr(firstStart, pos - firstStart);
		while (pos < source.size() && isSpace(source[pos])) ++pos;
		const size_t generationStart = pos;
		while (pos < source.size() && isdigit((unsigned char)source[pos])) ++pos;
		if (generationStart == pos) return false;
		const std::string generation =
			source.substr(generationStart, pos - generationStart);
		while (pos < source.size() && isSpace(source[pos])) ++pos;
		if (pos >= source.size() || source[pos] != 'R') return false;
		++pos;
		ref = object + " " + generation;
		return true;
	}

	bool parsePdfHex(const std::string& source, size_t& pos,
		std::string& bytes)
	{
		while (pos < source.size() && isSpace(source[pos])) ++pos;
		if (pos >= source.size() || source[pos] != '<' ||
			pos + 1 >= source.size() || source[pos + 1] == '<')
			return false;
		++pos;
		bytes.clear();
		int high = -1;
		while (pos < source.size() && source[pos] != '>')
		{
			const unsigned char c = (unsigned char)source[pos++];
			if (isSpace((char)c)) continue;
			const int digit = c >= '0' && c <= '9' ? c - '0' :
				(c >= 'A' && c <= 'F' ? c - 'A' + 10 :
					(c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1));
			if (digit < 0) return false;
			if (high < 0) high = digit;
			else
			{
				bytes += (char)((high << 4) | digit);
				high = -1;
			}
		}
		if (pos >= source.size()) return false;
		++pos;
		if (high >= 0) bytes += (char)(high << 4);
		return true;
	}

	uint32_t pdfCodeValue(const std::string& code)
	{
		uint32_t value = 0;
		for (unsigned char byte : code)
			value = (value << 8) | byte;
		return value;
	}

	std::string incrementUnicodeHex(const std::string& bytes, uint32_t value)
	{
		if (bytes.empty() || bytes.size() % 2 != 0) return std::string();
		std::string incremented = bytes;
		for (size_t i = incremented.size(); i >= 2; i -= 2)
		{
			unsigned int unit = ((unsigned char)incremented[i - 2] << 8) |
				(unsigned char)incremented[i - 1];
			const unsigned int add = value & 0xFFFF;
			value >>= 16;
			unit += add;
			incremented[i - 2] = (char)(unit >> 8);
			incremented[i - 1] = (char)unit;
			if (unit <= 0xFFFF && value == 0) break;
			value += unit >> 16;
		}
		return incremented;
	}

	void parseCMapBlock(const std::string& cmap, const std::string& begin,
		const std::string& end, PdfUnicodeMap& unicodeMap, bool ranges)
	{
		const size_t kMaxCMapEntries = 8192;
		size_t pos = 0;
		while (unicodeMap.size() < kMaxCMapEntries)
		{
			const size_t block = cmap.find(begin, pos);
			if (block == std::string::npos) return;
			pos = block + begin.size();
			const size_t blockEnd = cmap.find(end, pos);
			if (blockEnd == std::string::npos) return;
			while (pos < blockEnd && unicodeMap.size() < kMaxCMapEntries)
			{
				std::string first, last, destination;
				if (!parsePdfHex(cmap, pos, first))
				{
					++pos;
					continue;
				}
				if (!ranges)
				{
					if (parsePdfHex(cmap, pos, last))
						unicodeMap[first] = decodeUnicodeHex(last);
					continue;
				}
				if (!parsePdfHex(cmap, pos, last)) continue;
				const uint32_t firstCode = pdfCodeValue(first);
				const uint32_t lastCode = pdfCodeValue(last);
				if (lastCode < firstCode || lastCode - firstCode >= 4096)
					continue;

				size_t afterDestination = pos;
				while (afterDestination < blockEnd &&
					isSpace(cmap[afterDestination])) ++afterDestination;
				if (afterDestination < blockEnd &&
					cmap[afterDestination] == '[')
				{
					pos = afterDestination + 1;
					for (uint32_t code = firstCode; code <= lastCode &&
						unicodeMap.size() < kMaxCMapEntries; ++code)
					{
						while (pos < blockEnd && isSpace(cmap[pos])) ++pos;
						std::string target;
						if (!parsePdfHex(cmap, pos, target)) break;
						std::string sourceCode(first.size(), '\0');
						uint32_t sourceValue = code;
						for (size_t byte = sourceCode.size(); byte > 0; --byte)
						{
							sourceCode[byte - 1] = (char)sourceValue;
							sourceValue >>= 8;
						}
						unicodeMap[sourceCode] = decodeUnicodeHex(target);
					}
					const size_t arrayEnd = cmap.find(']', pos);
					pos = arrayEnd == std::string::npos ? blockEnd : arrayEnd + 1;
					continue;
				}

				if (!parsePdfHex(cmap, pos, destination)) continue;
				for (uint32_t code = firstCode; code <= lastCode &&
					unicodeMap.size() < kMaxCMapEntries; ++code)
				{
					std::string sourceCode(first.size(), '\0');
					uint32_t sourceValue = code;
					for (size_t byte = sourceCode.size(); byte > 0; --byte)
					{
						sourceCode[byte - 1] = (char)sourceValue;
						sourceValue >>= 8;
					}
					unicodeMap[sourceCode] = decodeUnicodeHex(
						incrementUnicodeHex(destination, code - firstCode));
				}
			}
			pos = blockEnd + end.size();
		}
	}

	bool readObjectBody(const std::string& bytes, const std::string& ref,
		std::string& body)
	{
		const size_t object = bytes.find(ref + " obj");
		if (object == std::string::npos) return false;
		size_t bodyStart = object + ref.size() + 4;
		while (bodyStart < bytes.size() && isSpace(bytes[bodyStart]))
			++bodyStart;
		const size_t endObject = bytes.find("endobj", bodyStart);
		if (endObject == std::string::npos) return false;
		body.assign(bytes, bodyStart, endObject - bodyStart);
		return true;
	}

	PdfFontMaps pageFontMaps(const std::string& bytes,
		const std::string& pageObject)
	{
		std::vector<std::pair<std::string, std::string>> fonts;
		std::vector<std::string> resourceObjects(1, pageObject);
		std::string inheritedObject = pageObject;
		for (int depth = 0; depth < 16; ++depth)
		{
			size_t parent = inheritedObject.find("/Parent");
			if (parent == std::string::npos) break;
			std::string parentRef;
			parent += 7;
			if (!parsePdfRef(inheritedObject, parent, parentRef)) break;
			if (!readObjectBody(bytes, parentRef, inheritedObject)) break;
			resourceObjects.push_back(inheritedObject);
		}

		for (size_t objectIndex = 0;
			objectIndex < resourceObjects.size(); ++objectIndex)
		{
			const std::string resourceObject = resourceObjects[objectIndex];
			size_t resources = resourceObject.find("/Resources");
			if (resources != std::string::npos)
			{
				resources += 10;
				std::string resourcesRef;
				size_t refPos = resources;
				if (parsePdfRef(resourceObject, refPos, resourcesRef))
				{
					std::string resourcesBody;
					if (readObjectBody(bytes, resourcesRef, resourcesBody))
						resourceObjects.push_back(std::move(resourcesBody));
				}
			}
			size_t pos = 0;
			while ((pos = resourceObject.find("/Font", pos)) !=
				std::string::npos)
			{
				pos += 5;
				while (pos < resourceObject.size() &&
					isSpace(resourceObject[pos])) ++pos;
				if (resourceObject.compare(pos, 2, "<<") != 0) continue;
				const size_t end = resourceObject.find(">>", pos + 2);
				if (end == std::string::npos) break;
				size_t entry = pos + 2;
				while (entry < end)
				{
					if (resourceObject[entry] != '/')
					{
						++entry;
						continue;
					}
					const size_t nameStart = ++entry;
					while (entry < end && !isSpace(resourceObject[entry]) &&
						resourceObject[entry] != '/') ++entry;
					const std::string name = resourceObject.substr(nameStart,
						entry - nameStart);
					std::string ref;
					size_t refPos = entry;
					if (parsePdfRef(resourceObject, refPos, ref) &&
						refPos <= end)
					{
						fonts.push_back({ name, ref });
						entry = refPos;
					}
				}
				pos = end + 2;
			}
		}

		PdfFontMaps result;
		for (const auto& font : fonts)
		{
			std::string fontObject;
			if (!readObjectBody(bytes, font.second, fontObject)) continue;
			PdfFontInfo fontInfo;
			fontInfo.characterIds =
				fontObject.find("/Type0") != std::string::npos ||
				fontObject.find("/Encoding /Identity-H") != std::string::npos ||
				fontObject.find("/Encoding /Identity-V") != std::string::npos;
			fontInfo.macRoman =
				fontObject.find("/MacRomanEncoding") != std::string::npos;
			const std::string lowerFont = toLower(fontObject);
			fontInfo.bold = lowerFont.find("bold") != std::string::npos;
			fontInfo.italic = lowerFont.find("italic") != std::string::npos ||
				lowerFont.find("oblique") != std::string::npos;
			fontInfo.monospace = lowerFont.find("courier") != std::string::npos;
			size_t differences = fontObject.find("/Differences");
			if (differences != std::string::npos)
			{
				differences = fontObject.find('[', differences);
				if (differences != std::string::npos)
				{
					++differences;
					int code = -1;
					while (differences < fontObject.size() &&
						fontObject[differences] != ']')
					{
						while (differences < fontObject.size() &&
							isSpace(fontObject[differences])) ++differences;
						if (differences >= fontObject.size() ||
							fontObject[differences] == ']') break;
						if (isdigit((unsigned char)fontObject[differences]))
						{
							char* end = nullptr;
							const long nextCode = strtol(
								fontObject.c_str() + differences, &end, 10);
							if (end == fontObject.c_str() + differences) break;
							differences = (size_t)(end - fontObject.c_str());
							code = nextCode >= 0 && nextCode <= 255
								? (int)nextCode : -1;
							continue;
						}
						if (fontObject[differences] != '/')
						{
							++differences;
							continue;
						}
						++differences;
						const size_t nameStart = differences;
						while (differences < fontObject.size() &&
							!isSpace(fontObject[differences]) &&
							fontObject[differences] != '/' &&
							fontObject[differences] != ']') ++differences;
						if (code < 0) continue;
						const std::string unicode = glyphNameUnicode(
							fontObject.substr(nameStart, differences - nameStart));
						if (code >= 0 && !unicode.empty())
						{
							fontInfo.simpleUnicode[std::string(1, (char)code)] =
								unicode;
						}
						if (code >= 0) ++code;
					}
				}
			}
			size_t toUnicode = fontObject.find("/ToUnicode");
			if (toUnicode != std::string::npos)
			{
				std::string cmapRef;
				toUnicode += 10;
				if (parsePdfRef(fontObject, toUnicode, cmapRef))
				{
					std::string cmapStream, error;
					if (objectStream(bytes, cmapRef, cmapStream, error))
					{
						parseCMapBlock(cmapStream, "beginbfchar",
							"endbfchar", fontInfo.unicode, false);
						parseCMapBlock(cmapStream, "beginbfrange",
							"endbfrange", fontInfo.unicode, true);
					}
				}
			}
			if (!fontInfo.unicode.empty() || fontInfo.characterIds ||
				fontInfo.macRoman || fontInfo.monospace ||
				!fontInfo.simpleUnicode.empty())
				result.emplace(font.first, std::move(fontInfo));
		}
		return result;
	}

	bool pageContentsRefs(const std::string& pageObject,
		std::vector<std::string>& refs)
	{
		refs.clear();
		const size_t contents = pageObject.find("/Contents");
		if (contents == std::string::npos) return false;
		size_t pos = contents + 9;
		while (pos < pageObject.size() && isSpace(pageObject[pos])) ++pos;
		const bool array = pos < pageObject.size() && pageObject[pos] == '[';
		if (array) ++pos;
		while (pos < pageObject.size())
		{
			while (pos < pageObject.size() && isSpace(pageObject[pos]))
				++pos;
			if (array && pos < pageObject.size() && pageObject[pos] == ']')
				break;
			std::string ref;
			if (!parsePdfRef(pageObject, pos, ref))
			{
				if (array)
				{
					++pos;
					continue;
				}
				return false;
			}
			refs.push_back(ref);
			if (!array) break;
		}
		return !refs.empty();
	}
}

bool pdfParse(const std::string& bytes, PdfDocument& document,
	std::string& error)
{
	document.pages.clear();
	document.pageRuns.clear();
	error.clear();
	if (bytes.size() < 8 || bytes.compare(0, 5, "%PDF-") != 0)
	{
		error = "Le fichier ne semble pas etre un PDF valide.";
		return false;
	}
	if (bytes.size() > kMaxPdfBytes)
	{
		error = "PDF trop volumineux pour cette premiere version (8 Mio max).";
		return false;
	}

	size_t pos = 0;
	while ((pos = bytes.find("/Type", pos)) != std::string::npos)
	{
		size_t type = pos + 5;
		while (type < bytes.size() && isSpace(bytes[type])) ++type;
		if (bytes.compare(type, 5, "/Page") != 0 ||
			(type + 5 < bytes.size() && bytes[type + 5] == 's'))
		{
			pos = type + 1;
			continue;
		}
		const size_t objectEnd = bytes.find("endobj", type);
		if (objectEnd == std::string::npos)
		{
			pos = type + 5;
			continue;
		}
		const size_t objectStart = bytes.rfind(" obj", type);
		if (objectStart == std::string::npos)
		{
			pos = type + 5;
			continue;
		}
		size_t refStart = objectStart;
		while (refStart > 0 && isdigit((unsigned char)bytes[refStart - 1]))
			--refStart;
		while (refStart > 0 && isSpace(bytes[refStart - 1])) --refStart;
		while (refStart > 0 && isdigit((unsigned char)bytes[refStart - 1]))
			--refStart;
		while (refStart < objectStart && isSpace(bytes[refStart])) ++refStart;
		const std::string pageObject = bytes.substr(refStart, objectEnd - refStart);
		const PdfFontMaps fontMaps = pageFontMaps(bytes, pageObject);
		std::vector<std::string> refs;
		if (!pageContentsRefs(pageObject, refs))
		{
			error = "Une page PDF utilise un format de contenu non pris en charge.";
			return false;
		}
		std::string stream;
		for (const std::string& ref : refs)
		{
			std::string content;
			if (!objectStream(bytes, ref, content, error)) return false;
			if (!stream.empty() && !content.empty() &&
				!isSpace(stream.back()) && !isSpace(content.front()))
				stream += '\n';
			stream += content;
		}
		bool unsupportedCharacterMap = false;
		std::vector<PdfTextRun> runs;
		document.pages.push_back(extractText(stream, fontMaps,
			unsupportedCharacterMap, runs));
		document.pageRuns.push_back(std::move(runs));
		if (unsupportedCharacterMap)
		{
			document.pages.clear();
			document.pageRuns.clear();
			error = "Police PDF sans table Unicode : extraction du texte impossible.";
			return false;
		}
		if (document.pages.size() > kMaxPages)
		{
			error = "PDF trop long pour cette premiere version (128 pages max).";
			document.pages.clear();
			document.pageRuns.clear();
			return false;
		}
		pos = objectEnd + 6;
	}
	if (document.pages.empty())
	{
		error = "Aucune page PDF simple n'a pu etre lue.";
		return false;
	}
	bool hasText = false;
	for (const std::string& page : document.pages)
		if (!strTrim(page).empty()) { hasText = true; break; }
	if (!hasText)
	{
		document.pages.clear();
		document.pageRuns.clear();
		error = "Ce PDF ne contient pas de texte lisible. Les PDF scannes ou en images ne sont pas encore pris en charge.";
		return false;
	}
	return true;
}

bool pdfReadFile(const std::string& path, PdfDocument& document,
	std::string& error)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (!file)
	{
		error = "Impossible d'ouvrir le fichier PDF.";
		return false;
	}
	std::string bytes;
	char buffer[4096];
	size_t count = 0;
	while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		if (bytes.size() + count > kMaxPdfBytes)
		{
			fclose(file);
			error = "PDF trop volumineux pour cette premiere version (8 Mio max).";
			return false;
		}
		bytes.append(buffer, count);
	}
	const bool readOk = !ferror(file);
	fclose(file);
	if (!readOk)
	{
		error = "Erreur pendant la lecture du fichier PDF.";
		return false;
	}
	return pdfParse(bytes, document, error);
}

bool pdfListFiles(std::vector<PdfFileInfo>& files, std::string& error)
{
	files.clear();
	error.clear();
#ifdef _WIN32
	const std::string pattern = std::string(NW_SD_PDFS) + "\\*.pdf";
	struct _finddata_t item;
	const intptr_t handle = _findfirst(pattern.c_str(), &item);
	if (handle == -1)
	{
		error = "Aucun PDF trouve dans le dossier.";
		return true;
	}
	do
	{
		PdfFileInfo file;
		file.name = item.name;
		file.path = std::string(NW_SD_PDFS) + "\\" + file.name;
		struct _stat info;
		if (_stat(file.path.c_str(), &info) == 0 &&
			(info.st_mode & _S_IFMT) == _S_IFREG)
		{
			file.size = (uint64_t)info.st_size;
			files.push_back(std::move(file));
		}
	} while (_findnext(handle, &item) == 0);
	_findclose(handle);
#else
	DIR* dir = opendir(NW_SD_PDFS);
	if (!dir)
	{
		error = "Aucun PDF trouve dans le dossier.";
		return true;
	}
	struct dirent* item = nullptr;
	while ((item = readdir(dir)) != nullptr)
	{
		const std::string name = item->d_name;
		if (name == "." || name == ".." || name.size() < 4) continue;
		if (toLower(name.substr(name.size() - 4)) != ".pdf") continue;
		PdfFileInfo file;
		file.name = name;
		file.path = std::string(NW_SD_PDFS) + "/" + name;
		struct stat info;
		if (stat(file.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
			continue;
		file.size = (uint64_t)info.st_size;
		files.push_back(std::move(file));
	}
	closedir(dir);
#endif
	std::sort(files.begin(), files.end(), [](const PdfFileInfo& a,
		const PdfFileInfo& b) { return a.name < b.name; });
	return true;
}

static std::string pdfCssColor(uint32_t color)
{
	static const char digits[] = "0123456789abcdef";
	std::string result = "#000000";
	for (int channel = 0; channel < 3; ++channel)
	{
		const unsigned int value = (color >> (channel * 8)) & 0xFF;
		result[1 + channel * 2] = digits[value >> 4];
		result[2 + channel * 2] = digits[value & 0x0F];
	}
	return result;
}

static std::string pdfStyledHtml(const PdfTextRun& run,
	const std::string& text, int zoomPercent)
{
	const int runSize = clamp(run.fontSize * 4 * zoomPercent / 300, 8, 96);
	std::string html = "<span style=\"font-size:" + itos(runSize) +
		"px;color:" + pdfCssColor(run.color);
	if (run.bold) html += ";font-weight:bold";
	if (run.italic) html += ";font-style:italic";
	if (run.monospace) html += ";font-family:monospace";
	html += "\">" + pdfHtmlEscape(text) + "</span>";
	return html;
}

std::string pdfDocumentHtml(const PdfDocument& document,
	const std::string& title, size_t pageIndex, int zoomPercent)
{
	if (document.pages.empty() || pageIndex >= document.pages.size())
		return "<html><body>Page PDF indisponible.</body></html>";
	static const int zoomLevels[] = { 100, 125, 150, 200, 300 };
	if (std::find(zoomLevels, zoomLevels + 5, zoomPercent) == zoomLevels + 5)
		zoomPercent = 100;
	std::string html = "<html><head><title>" + pdfHtmlEscape(title) +
		"</title><style>body{background:#fff;color:#111;margin:0;font-family:sans-serif}"
		".page{background:#fff;color:#111;padding:20px 16px;min-height:520px}"
		"h2{font-size:16px;color:#444;margin:0 0 12px}"
		".nav{font-size:14px;margin:0 0 14px}a{color:#174c87}"
		"pre.pdftext{font-size:16px;white-space:pre-wrap;line-height:1.3}"
		"table.pdf-table{border-collapse:collapse;margin:4px 0}"
		"table.pdf-table td{border:1px solid #888;padding:2px 4px;white-space:pre-wrap}</style>"
		"</head><body><section class=\"page\"><nav class=\"nav\">";
	if (pageIndex > 0)
		html += "<a href=\"nw://pdf-page?page=" +
			itos((long)pageIndex) + "\">Precedente</a> &nbsp; ";
	if (pageIndex + 1 < document.pages.size())
		html += "<a href=\"nw://pdf-page?page=" +
			itos((long)(pageIndex + 2)) + "\">Suivante</a>";
	html += "</nav><h2>Page " + itos((long)(pageIndex + 1)) + " / " +
		itos((long)document.pages.size()) + " - zoom " +
		itos(zoomPercent) + "%</h2>";
	if (pageIndex < document.pageRuns.size() &&
		!document.pageRuns[pageIndex].empty())
	{
		bool preOpen = false;
		bool tableOpen = false;
		std::string lineText;
		std::vector<std::string> cells(1);
		auto flushLine = [&]() {
			const bool tabular = lineText.find('\t') != std::string::npos;
			if (tabular)
			{
				if (preOpen)
				{
					html += "</pre>";
					preOpen = false;
				}
				if (!tableOpen)
				{
					html += "<table class=\"pdf-table\">";
					tableOpen = true;
				}
				html += "<tr>";
				for (const std::string& cell : cells)
					html += "<td>" + cell + "</td>";
				html += "</tr>";
			}
			else
			{
				if (tableOpen)
				{
					html += "</table>";
					tableOpen = false;
				}
				if (!preOpen)
				{
					html += "<pre class=\"pdftext\">";
					preOpen = true;
				}
				if (!cells.empty()) html += cells[0];
				html += "\n";
			}
			lineText.clear();
			cells.assign(1, std::string());
		};
		for (const PdfTextRun& run : document.pageRuns[pageIndex])
		{
			size_t start = 0;
			for (size_t pos = 0; pos <= run.text.size(); ++pos)
			{
				if (pos < run.text.size() && run.text[pos] != '\t' &&
					run.text[pos] != '\n' && run.text[pos] != '\r')
					continue;
				if (pos > start)
				{
					const std::string part = run.text.substr(start, pos - start);
					cells.back() += pdfStyledHtml(run, part, zoomPercent);
					lineText += part;
				}
				if (pos < run.text.size() && run.text[pos] == '\t')
				{
					lineText += '\t';
					cells.push_back(std::string());
					start = pos + 1;
				}
				else
				{
					if (pos < run.text.size() && run.text[pos] == '\r' &&
						pos + 1 < run.text.size() && run.text[pos + 1] == '\n')
						++pos;
					flushLine();
					start = pos + 1;
				}
			}
		}
		if (!lineText.empty() || cells.size() > 1 || !cells[0].empty())
			flushLine();
		if (tableOpen) html += "</table>";
		if (preOpen) html += "</pre>";
	}
	else
		html += "<pre class=\"pdftext\">" +
			pdfHtmlEscape(document.pages[pageIndex]) + "</pre>";
	html += "</section>";
	html += "</body></html>";
	return html;
}

int pdfSelfTest()
{
	const std::string pdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>\nendobj\n"
		"4 0 obj\n<< /Length 50 >>\nstream\nBT (Hello \\(PDF\\)) Tj ET\nendstream\nendobj\n"
		"%%EOF";
	PdfDocument document;
	std::string error;
	if (!pdfParse(pdf, document, error) || document.pages.size() != 1 ||
		document.pages[0].find("Hello (PDF)") == std::string::npos)
		return 1;
	const std::string html = pdfDocumentHtml(document, "test<&>", 0);
	if (html.find("test&lt;&amp;&gt;") == std::string::npos ||
		html.find("Hello (PDF)") == std::string::npos)
		return 1;
	const std::string zoomHtml = pdfDocumentHtml(document, "test", 0, 200);
	if (zoomHtml.find("font-size:32px") == std::string::npos ||
		zoomHtml.find("zoom 200%") == std::string::npos)
		return 1;
	const std::string utf16Pdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>\nendobj\n"
		"4 0 obj\n<< /Length 36 >>\nstream\n"
		"BT <00480065006C006C006F0020005000440046> Tj ET\n"
		"endstream\nendobj\n%%EOF";
	if (!pdfParse(utf16Pdf, document, error) ||
		document.pages.size() != 1 ||
		document.pages[0].find("Hello PDF") == std::string::npos)
		return 1;
	const std::string firstTextStream = "BT /F1 12 Tf <01020304> Tj";
	const std::string secondTextStream = "BT /F1 12 Tf <05060708090A> Tj ET";
	const std::string unicodeCMap =
		"begincmap\n7 beginbfchar\n"
		"<01> <0062>\n<02> <006F>\n<03> <006E>\n<04> <006A>\n"
		"<05> <006F>\n<06> <0075>\n<07> <0072>\n"
		"endbfchar\n3 beginbfrange\n<08> <0A> [<0061> <0063> <0062>]\n"
		"endbfrange\nendcmap";
	const std::string mappedFontPdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 "
		"/Resources 8 0 R >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R "
		"/Contents [4 0 R 7 0 R] >>\nendobj\n"
		"4 0 obj << /Length 9 0 R >>\nstream\n" + firstTextStream +
		"\nendstream\nendobj\n"
		"5 0 obj << /Type /Font /Subtype/Type0 /ToUnicode 6 0 R >>\nendobj\n"
		"6 0 obj << /Length 10 0 R >>\nstream\n" + unicodeCMap +
		"\nendstream\nendobj\n"
		"7 0 obj << /Length " + itos((long)secondTextStream.size()) +
		" >>\nstream\n" + secondTextStream + "\nendstream\nendobj\n"
		"8 0 obj\n<< /Font << /F1 5 0 R >> >>\nendobj\n"
		"9 0 obj\n" + itos((long)firstTextStream.size()) +
		"\nendobj\n10 0 obj\n" + itos((long)unicodeCMap.size()) +
		"\nendobj\n%%EOF";
	if (!pdfParse(mappedFontPdf, document, error) ||
		document.pages.size() != 1 ||
		document.pages[0].find("bonjouracb") == std::string::npos)
		return 1;
	std::string unmappedFontPdf = mappedFontPdf;
	const std::string toUnicodeEntry = "/ToUnicode 6 0 R";
	unmappedFontPdf.erase(unmappedFontPdf.find(toUnicodeEntry),
		toUnicodeEntry.size());
	if (pdfParse(unmappedFontPdf, document, error) ||
		error.find("sans table Unicode") == std::string::npos)
		return 1;
	const std::string winAnsiPdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>\nendobj\n"
		"4 0 obj\n<< /Length 17 >>\nstream\nBT (caf\xE9) Tj ET\n"
		"endstream\nendobj\n%%EOF";
	if (!pdfParse(winAnsiPdf, document, error) ||
		document.pages.size() != 1 ||
		document.pages[0].find("caf\xC3\xA9") == std::string::npos)
		return 1;
	const std::string styledContent = "BT /F1 18 Tf <4142> Tj ET";
	const std::string styledFontPdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R "
		"/Resources 6 0 R >>\nendobj\n"
		"4 0 obj << /Length " + itos((long)styledContent.size()) +
		" >>\nstream\n" + styledContent + "\nendstream\nendobj\n"
		"5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
		"/Encoding << /Differences [65 /eacute] >> >>\nendobj\n"
		"6 0 obj << /Font << /F1 5 0 R >> >>\nendobj\n%%EOF";
	if (!pdfParse(styledFontPdf, document, error) ||
		document.pages[0].find("\xC3\xA9" "B") == std::string::npos ||
		document.pageRuns.empty() || document.pageRuns[0].empty() ||
		!document.pageRuns[0][0].bold ||
		document.pageRuns[0][0].fontSize != 18)
		return 1;
	const std::string styledHtml = pdfDocumentHtml(document, "styled", 0);
	if (styledHtml.find("font-weight:bold") == std::string::npos ||
		styledHtml.find("font-size:24px") == std::string::npos)
		return 1;
	const std::string coloredContent =
		"q 0.8 0.1 0.2 rg BT /F1 12 Tf (red) Tj ET Q "
		"BT /F1 12 Tf 0.1 0.7 0.2 rg (green) Tj 0 g (gray) Tj ET";
	const std::string coloredPdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R "
		"/Resources 6 0 R >>\nendobj\n"
		"4 0 obj << /Length " + itos((long)coloredContent.size()) +
		" >>\nstream\n" + coloredContent + "\nendstream\nendobj\n"
		"5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n"
		"6 0 obj << /Font << /F1 5 0 R >> >>\nendobj\n%%EOF";
	if (!pdfParse(coloredPdf, document, error) ||
		document.pageRuns[0].size() < 3 ||
		document.pageRuns[0][0].color != 0xFF331ACC ||
		document.pageRuns[0][1].color != 0xFF33B31A ||
		document.pageRuns[0][2].color != 0xFF000000 ||
		!document.pageRuns[0][0].monospace)
		return 1;
	const std::string coloredHtml = pdfDocumentHtml(document, "colors", 0);
	if (coloredHtml.find("color:#cc1a33") == std::string::npos ||
		coloredHtml.find("color:#1ab333") == std::string::npos ||
		coloredHtml.find("font-family:monospace") == std::string::npos)
		return 1;
	const std::string tableContent = "BT [ (Nom) -600 (Quantite) -600 (Prix) ] TJ ET";
	const std::string tablePdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>\nendobj\n"
		"4 0 obj << /Length " + itos((long)tableContent.size()) +
		" >>\nstream\n" + tableContent + "\nendstream\nendobj\n%%EOF";
	if (!pdfParse(tablePdf, document, error) ||
		document.pages[0].find("Nom\tQuantite\tPrix") == std::string::npos)
		return 1;
	const std::string tableHtml = pdfDocumentHtml(document, "table", 0);
	if (tableHtml.find("<table class=\"pdf-table\">") == std::string::npos ||
		tableHtml.find("<td>") == std::string::npos ||
		tableHtml.find("Quantite") == std::string::npos)
		return 1;
	const std::string macRomanContent = "BT /F1 12 Tf (caf\x8e) Tj ET";
	const std::string macRomanPdf =
		"%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /Contents 4 0 R "
		"/Resources 6 0 R >>\nendobj\n"
		"4 0 obj << /Length " + itos((long)macRomanContent.size()) +
		" >>\nstream\n" + macRomanContent + "\nendstream\nendobj\n"
		"5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
		"/Encoding /MacRomanEncoding >>\nendobj\n"
		"6 0 obj << /Font << /F1 5 0 R >> >>\nendobj\n%%EOF";
	if (!pdfParse(macRomanPdf, document, error) ||
		document.pages[0].find("caf\xC3\xA9") == std::string::npos)
		return 1;
	if (pdfParse("not a pdf", document, error)) return 1;
	return 0;
}
