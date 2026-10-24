#include "css.hpp"
#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

// CSS est resolu sur le thread principal. Chaque moteur charge sa largeur
// avant d'analyser une feuille ou de resoudre les styles inline.
static int gCssViewportWidth = NW_PAGE_WIDTH;
static int cssViewportHeight()
{
	return (NW_TOP_HEIGHT * gCssViewportWidth + NW_PAGE_WIDTH / 2) / NW_PAGE_WIDTH;
}

//-----------------------------------------------------------------------------
// Couleurs nommees (selection courante)
//-----------------------------------------------------------------------------
struct NamedColor { const char* name; uint32_t rgb; };

static const NamedColor kColors[] =
{
	{ "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 },
	{ "green", 0x008000 }, { "lime", 0x00FF00 }, { "blue", 0x0000FF },
	{ "yellow", 0xFFFF00 }, { "cyan", 0x00FFFF }, { "aqua", 0x00FFFF },
	{ "magenta", 0xFF00FF }, { "fuchsia", 0xFF00FF }, { "silver", 0xC0C0C0 },
	{ "gray", 0x808080 }, { "grey", 0x808080 }, { "maroon", 0x800000 },
	{ "olive", 0x808000 }, { "purple", 0x800080 }, { "teal", 0x008080 },
	{ "navy", 0x000080 }, { "orange", 0xFFA500 }, { "brown", 0xA52A2A },
	{ "pink", 0xFFC0CB }, { "gold", 0xFFD700 }, { "violet", 0xEE82EE },
	{ "indigo", 0x4B0082 }, { "darkgray", 0xA9A9A9 }, { "darkgrey", 0xA9A9A9 },
	{ "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 },
	{ "whitesmoke", 0xF5F5F5 }, { "gainsboro", 0xDCDCDC },
	{ "dimgray", 0x696969 }, { "dimgrey", 0x696969 },
	{ "darkblue", 0x00008B }, { "darkred", 0x8B0000 }, { "darkgreen", 0x006400 },
	{ "darkorange", 0xFF8C00 }, { "darkviolet", 0x9400D3 },
	{ "lightblue", 0xADD8E6 }, { "lightgreen", 0x90EE90 },
	{ "lightyellow", 0xFFFFE0 }, { "lightcoral", 0xF08080 },
	{ "lightpink", 0xFFB6C1 }, { "lightsalmon", 0xFFA07A },
	{ "salmon", 0xFA8072 }, { "coral", 0xFF7F50 }, { "tomato", 0xFF6347 },
	{ "orangered", 0xFF4500 }, { "crimson", 0xDC143C }, { "firebrick", 0xB22222 },
	{ "chocolate", 0xD2691E }, { "peru", 0xCD853F }, { "tan", 0xD2B48C },
	{ "wheat", 0xF5DEB3 }, { "beige", 0xF5F5DC }, { "ivory", 0xFFFFF0 },
	{ "lavender", 0xE6E6FA }, { "plum", 0xDDA0DD }, { "orchid", 0xDA70D6 },
	{ "thistle", 0xD8BFD8 }, { "hotpink", 0xFF69B4 }, { "deeppink", 0xFF1493 },
	{ "royalblue", 0x4169E1 }, { "steelblue", 0x4682B4 }, { "skyblue", 0x87CEEB },
	{ "dodgerblue", 0x1E90FF }, { "cornflowerblue", 0x6495ED },
	{ "mediumblue", 0x0000CD }, { "cadetblue", 0x5F9EA0 },
	{ "turquoise", 0x40E0D0 }, { "mediumturquoise", 0x48D1CC },
	{ "springgreen", 0x00FF7F }, { "lawngreen", 0x7CFC00 },
	{ "chartreuse", 0x7FFF00 }, { "yellowgreen", 0x9ACD32 },
	{ "olivedrab", 0x6B8E23 }, { "seagreen", 0x2E8B57 }, { "forestgreen", 0x228B22 },
	{ "limegreen", 0x32CD32 }, { "darkslategray", 0x2F4F4F }, { "darkslategrey", 0x2F4F4F },
	{ "slategray", 0x708090 }, { "slategrey", 0x708090 },
	{ "lightslategray", 0x778899 }, { "lightslategrey", 0x778899 },
	{ "midnightblue", 0x191970 }, { "darkturquoise", 0x00CED1 },
	{ "sienna", 0xA0522D }, { "saddlebrown", 0x8B4513 },
	{ "snow", 0xFFFAFA }, { "mintcream", 0xF5FFFA }, { "honeydew", 0xF0FFF0 },
	{ "azure", 0xF0FFFF }, { "aliceblue", 0xF0F8FF }, { "ghostwhite", 0xF8F8FF },
	{ "floralwhite", 0xFFFAF0 }, { "linen", 0xFAF0E6 }, { "oldlace", 0xFDF5E6 },
	{ "antiquewhite", 0xFAEBD7 }, { "mistyrose", 0xFFE4E1 }, { "papayawhip", 0xFFEFD5 },
	{ "peachpuff", 0xFFDAB9 }, { "bisque", 0xFFE4C4 }, { "blanchedalmond", 0xFFEBCD },
	{ "moccasin", 0xFFE4B5 }, { "navajowhite", 0xFFDEAD }, { "burlywood", 0xDEB887 },
	{ "khaki", 0xF0E68C }, { "darkkhaki", 0xBDB76B }, { "palegoldenrod", 0xEEE8AA },
	{ "goldenrod", 0xDAA520 }, { "darkgoldenrod", 0xB8860B },
	{ "rosybrown", 0xBC8F8F }, { "indianred", 0xCD5C5C }, { "mediumvioletred", 0xC71585 },
	{ "palevioletred", 0xDB7093 }, { "deepskyblue", 0x00BFFF },
	{ "powderblue", 0xB0E0E6 }, { "paleturquoise", 0xAFEEEE },
	{ "mediumaquamarine", 0x66CDAA }, { "aquamarine", 0x7FFFD4 },
	{ "mediumspringgreen", 0x00FA9A }, { "palegreen", 0x98FB98 },
	{ "greenyellow", 0xADFF2F }, { "mediumseagreen", 0x3CB371 },
	{ "darkseagreen", 0x8FBC8F }, { "darkcyan", 0x008B8B }, { "darkmagenta", 0x8B008B },
	{ "mediumpurple", 0x9370DB }, { "blueviolet", 0x8A2BE2 }, { "darkorchid", 0x9932CC },
	{ "darkslateblue", 0x483D8B }, { "slateblue", 0x6A5ACD }, { "mediumslateblue", 0x7B68EE },
	{ "rebeccapurple", 0x663399 }, { "maroon", 0x800000 },
	{ "transparent", 0xFFFFFFFF },
	{ nullptr, 0 }
};

bool cssParseColor(const std::string& str, uint32_t& out)
{
	std::string s = strTrim(toLower(str));
	if (s.empty()) return false;
	if (s[0] == '#')
	{
		if (s.size() == 4)
		{
			unsigned r, g, b;
			if (sscanf(s.c_str() + 1, "%1x%1x%1x", &r, &g, &b) != 3) return false;
			out = 0xFF000000 | ((uint32_t)(b * 17) << 16) | ((uint32_t)(g * 17) << 8) | (uint32_t)(r * 17);
			return true;
		}
		if (s.size() == 5)  // #rgba
		{
			unsigned r, g, b, a;
			if (sscanf(s.c_str() + 1, "%1x%1x%1x%1x", &r, &g, &b, &a) != 4) return false;
			uint32_t aa = a * 17;
			out = (aa << 24) | ((uint32_t)(b * 17) << 16) | ((uint32_t)(g * 17) << 8) | (uint32_t)(r * 17);
			return true;
		}
		if (s.size() == 7)
		{
			unsigned v;
			if (sscanf(s.c_str() + 1, "%6x", &v) != 1) return false;
			out = 0xFF000000 | ((v & 0xFF) << 16) | (v & 0xFF00) | ((v & 0xFF0000) >> 16);
			return true;
		}
		if (s.size() == 9)  // #rrggbbaa
		{
			unsigned v;
			if (sscanf(s.c_str() + 1, "%8x", &v) != 1) return false;
			uint32_t aa = v & 0xFF;
			out = (aa << 24) | (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
			return true;
		}
		return false;
	}
	if (strStartsWith(s, "rgb(") || strStartsWith(s, "rgba("))
	{
		const size_t open = s.find('('), close = s.rfind(')');
		if (close == std::string::npos || close <= open) return false;
		std::string inner = s.substr(open + 1, close - open - 1);
		for (char& ch : inner)
			if (ch == ',' || ch == '/') ch = ' ';
		std::vector<std::string> parts;
		size_t pos = 0;
		while (pos < inner.size())
		{
			while (pos < inner.size() && isspace((unsigned char)inner[pos])) ++pos;
			if (pos >= inner.size()) break;
			size_t end = inner.find_first_of(" \t\r\n", pos);
			if (end == std::string::npos) end = inner.size();
			parts.push_back(inner.substr(pos, end - pos));
			pos = end;
		}
		if (parts.size() < 3 || parts.size() > 4) return false;
		float channels[3] = {};
		for (int i = 0; i < 3; ++i)
		{
			char* end = nullptr;
			channels[i] = strtof(parts[(size_t)i].c_str(), &end);
			if (end == parts[(size_t)i].c_str() || !isfinite(channels[i])) return false;
			if (*end == '%')
			{
				if (end[1] != '\0') return false;
				channels[i] = channels[i] * 2.55f;
			}
			else if (*end != '\0') return false;
			channels[i] = std::max(0.0f, std::min(255.0f, channels[i]));
		}
		float alpha = 255.0f;
		if (parts.size() == 4)
		{
			char* end = nullptr;
			alpha = strtof(parts[3].c_str(), &end);
			if (end == parts[3].c_str() || !isfinite(alpha)) return false;
			if (*end == '%')
			{
				if (end[1] != '\0') return false;
				alpha = alpha * 2.55f;
			}
			else if (*end == '\0') alpha *= 255.0f;
			else return false;
			alpha = std::max(0.0f, std::min(255.0f, alpha));
		}
		const uint32_t r = (uint32_t)(channels[0] + 0.5f);
		const uint32_t g = (uint32_t)(channels[1] + 0.5f);
		const uint32_t b = (uint32_t)(channels[2] + 0.5f);
		const uint32_t a = (uint32_t)(alpha + 0.5f);
		out = (a << 24) | (b << 16) | (g << 8) | r;
		return true;
	}
	if (strStartsWith(s, "hsl(") || strStartsWith(s, "hsla("))
	{
		const size_t open = s.find('('), close = s.rfind(')');
		if (close == std::string::npos || close <= open) return false;
		std::string inner = s.substr(open + 1, close - open - 1);
		for (char& ch : inner)
			if (ch == ',' || ch == '/') ch = ' ';
		std::vector<std::string> parts;
		size_t pos = 0;
		while (pos < inner.size())
		{
			while (pos < inner.size() && isspace((unsigned char)inner[pos])) ++pos;
			if (pos >= inner.size()) break;
			size_t end = inner.find_first_of(" \t\r\n", pos);
			if (end == std::string::npos) end = inner.size();
			parts.push_back(inner.substr(pos, end - pos));
			pos = end;
		}
		if (parts.size() < 3 || parts.size() > 4 ||
			parts[1].back() != '%' || parts[2].back() != '%') return false;
		char* hueEnd = nullptr;
		float hue = strtof(parts[0].c_str(), &hueEnd);
		if (hueEnd == parts[0].c_str() || !isfinite(hue)) return false;
		const std::string hueUnit = toLower(hueEnd);
		if (hueUnit == "turn") hue *= 360.0f;
		else if (hueUnit == "rad") hue *= 57.2957795f;
		else if (hueUnit == "grad") hue *= 0.9f;
		else if (!hueUnit.empty() && hueUnit != "deg") return false;
		hue = fmodf(hue, 360.0f);
		if (hue < 0.0f) hue += 360.0f;
		char* satEnd = nullptr;
		char* lightEnd = nullptr;
		float saturation = strtof(parts[1].c_str(), &satEnd);
		float lightness = strtof(parts[2].c_str(), &lightEnd);
		if (satEnd == parts[1].c_str() || strcmp(satEnd, "%") != 0 ||
			lightEnd == parts[2].c_str() || strcmp(lightEnd, "%") != 0 ||
			!isfinite(saturation) || !isfinite(lightness)) return false;
		saturation = std::max(0.0f, std::min(100.0f, saturation)) / 100.0f;
		lightness = std::max(0.0f, std::min(100.0f, lightness)) / 100.0f;
		const float chroma = (1.0f - fabsf(2.0f * lightness - 1.0f)) * saturation;
		const float h = hue / 60.0f;
		const float x = chroma * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
		float r = 0, g = 0, b = 0;
		if (h < 1) { r = chroma; g = x; }
		else if (h < 2) { r = x; g = chroma; }
		else if (h < 3) { g = chroma; b = x; }
		else if (h < 4) { g = x; b = chroma; }
		else if (h < 5) { r = x; b = chroma; }
		else { r = chroma; b = x; }
		const float offset = lightness - chroma * 0.5f;
		uint32_t alpha = 255;
		if (parts.size() == 4)
		{
			char* alphaEnd = nullptr;
			float value = strtof(parts[3].c_str(), &alphaEnd);
			if (alphaEnd == parts[3].c_str() || !isfinite(value)) return false;
			if (*alphaEnd == '%') value /= 100.0f;
			else if (*alphaEnd != '\0') return false;
			alpha = (uint32_t)(std::max(0.0f, std::min(1.0f, value)) * 255.0f + 0.5f);
		}
		const uint32_t red = (uint32_t)((r + offset) * 255.0f + 0.5f);
		const uint32_t green = (uint32_t)((g + offset) * 255.0f + 0.5f);
		const uint32_t blue = (uint32_t)((b + offset) * 255.0f + 0.5f);
		out = (alpha << 24) | (blue << 16) | (green << 8) | red;
		return true;
	}
	for (int k = 0; kColors[k].name; k++)
	{
		if (s == kColors[k].name)
		{
			uint32_t v = kColors[k].rgb;
			if (kColors[k].rgb == 0xFFFFFFFF)
			{
				out = 0; // transparent (alpha 0)
				return true;
			}
			out = 0xFF000000 | ((v & 0xFF) << 16) | (v & 0xFF00) | ((v & 0xFF0000) >> 16);
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Parse d'une longueur CSS (px, em, rem, pt ou sans unite)
//-----------------------------------------------------------------------------
class CssCalcParser
{
public:
	CssCalcParser(const std::string& expression, int base, int percentBase)
		: cursor_(expression.c_str()), base_(base), percentBase_(percentBase) {}

	bool parse(double& result)
	{
		result = parseSum();
		skipSpaces();
		return valid_ && *cursor_ == '\0' && isfinite(result);
	}

private:
	const char* cursor_;
	int base_;
	int percentBase_;
	bool valid_ = true;

	void skipSpaces()
	{
		while (isspace((unsigned char)*cursor_)) ++cursor_;
	}

	double parseSum()
	{
		double value = parseProduct();
		while (valid_)
		{
			skipSpaces();
			const char operation = *cursor_;
			if (operation != '+' && operation != '-') break;
			++cursor_;
			const double right = parseProduct();
			value = operation == '+' ? value + right : value - right;
		}
		return value;
	}

	double parseProduct()
	{
		double value = parseAtom();
		while (valid_)
		{
			skipSpaces();
			const char operation = *cursor_;
			if (operation != '*' && operation != '/') break;
			++cursor_;
			const double right = parseAtom();
			if (operation == '/' && right == 0.0)
			{
				valid_ = false;
				return 0.0;
			}
			value = operation == '*' ? value * right : value / right;
		}
		return value;
	}

	double parseAtom()
	{
		skipSpaces();
		if (*cursor_ == '(')
		{
			++cursor_;
			const double value = parseSum();
			skipSpaces();
			if (*cursor_ != ')') valid_ = false;
			else ++cursor_;
			return value;
		}
		char* end = nullptr;
		const double value = strtod(cursor_, &end);
		if (end == cursor_ || !isfinite(value))
		{
			valid_ = false;
			return 0.0;
		}
		cursor_ = end;
		const char* unitStart = cursor_;
		while (isalpha((unsigned char)*cursor_) || *cursor_ == '%') ++cursor_;
		const std::string unit = toLower(std::string(unitStart, cursor_));
		if (unit.empty() || unit == "px") return value;
		if (unit == "em" || unit == "rem") return value * base_;
		if (unit == "pt") return value * (4.0 / 3.0);
		if (unit == "%") return value * percentBase_ / 100.0;
		if (unit == "vw") return value * gCssViewportWidth / 100.0;
		if (unit == "vh") return value * cssViewportHeight() / 100.0;
		valid_ = false;
		return 0.0;
	}
};

static int parseCssLength(const std::string& token, int base = 16,
	int percentBase = 0, int depth = 0)
{
	if (percentBase <= 0) percentBase = gCssViewportWidth;
	std::string t = strTrim(token);
	if (t.empty() || t == "auto" || depth > 8) return 0;
	const std::string lower = toLower(t);
	const bool isMin = strStartsWith(lower, "min(");
	const bool isMax = strStartsWith(lower, "max(");
	const bool isClamp = strStartsWith(lower, "clamp(");
	if ((isMin || isMax || isClamp) && t.back() == ')')
	{
		const size_t open = t.find('(');
		std::vector<int> values;
		size_t start = open + 1;
		int nesting = 0;
		for (size_t i = start; i <= t.size() - 1; ++i)
		{
			if (i < t.size() - 1 && t[i] == '(') ++nesting;
			else if (i < t.size() - 1 && t[i] == ')' && nesting > 0) --nesting;
			if ((i == t.size() - 1 || (t[i] == ',' && nesting == 0)))
			{
				values.push_back(parseCssLength(t.substr(start, i - start),
					base, percentBase, depth + 1));
				start = i + 1;
			}

		}
		if ((isClamp && values.size() == 3) ||
			((isMin || isMax) && values.size() >= 2))
		{
			if (isClamp)
				return std::max(values[0], std::min(values[2], values[1]));
			return isMin ? *std::min_element(values.begin(), values.end())
				: *std::max_element(values.begin(), values.end());
		}
		return 0;
	}
	if (strStartsWith(toLower(t), "calc(") && t.back() == ')')
	{
		CssCalcParser parser(t.substr(5, t.size() - 6), base, percentBase);
		double value = 0.0;
		if (!parser.parse(value)) return 0;
		value = std::max(-100000.0, std::min(100000.0, value));
		return (int)value;
	}
	char* end = nullptr;
	float val = strtof(t.c_str(), &end);
	if (end == t.c_str() || !isfinite(val)) return 0;
	std::string unit = toLower(strTrim(std::string(end)));
	if (unit == "em" || unit == "rem") val *= (float)base;
	else if (unit == "pt") val *= 4.0f / 3.0f;
	else if (unit == "%") val *= (float)percentBase / 100.0f;
	else if (unit == "vw") val *= (float)gCssViewportWidth / 100.0f;
	else if (unit == "vh") val *= (float)cssViewportHeight() / 100.0f;
	else if (!unit.empty() && unit != "px") return 0;
	if (val > 100000.0f) val = 100000.0f;
	if (val < -100000.0f) val = -100000.0f;
	return (int)val;
}

static bool parseCssPercent(const std::string& value, int& percent)
{
	const std::string token = strTrim(value);
	if (token.empty() || token.back() != '%') return false;
	char* end = nullptr;
	const float parsed = strtof(token.c_str(), &end);
	if (end == token.c_str() || strcmp(end, "%") != 0 || !isfinite(parsed))
		return false;
	percent = (int)std::max(-100000.0f, std::min(100000.0f,
		parsed * 100.0f));
	return true;
}

static std::vector<std::string> splitCssWhitespace(const std::string& text)
{
	std::vector<std::string> tokens;
	size_t start = std::string::npos;
	int depth = 0;
	char quote = 0;
	for (size_t i = 0; i <= text.size(); ++i)
	{
		const char ch = i < text.size() ? text[i] : ' ';
		if (quote)
		{
			if (ch == '\\' && i + 1 < text.size()) { ++i; continue; }
			if (ch == quote) quote = 0;
			continue;
		}
		if (ch == '\'' || ch == '"') quote = ch;
		else if (ch == '(') ++depth;
		else if (ch == ')' && depth > 0) --depth;
		if (isspace((unsigned char)ch) && depth == 0)
		{
			if (start != std::string::npos)
			{
				tokens.push_back(text.substr(start, i - start));
				start = std::string::npos;
			}
		}
		else if (start == std::string::npos) start = i;
	}
	return tokens;
}

static size_t findCssFunctionComma(const std::string& text)
{
	int depth = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '(') ++depth;
		else if (text[i] == ')') --depth;
		else if (text[i] == ',' && depth == 1) return i;
	}
	return std::string::npos;
}

static bool parseGradient(const std::string& value, Style& out)
{
	const std::string v = strTrim(value);
	const size_t open = v.find('(');
	if (open == std::string::npos || v.back() != ')') return false;
	const std::string function = toLower(strTrim(v.substr(0, open)));
	const bool radial = function == "radial-gradient";
	if (!radial && function != "linear-gradient") return false;

	std::vector<std::string> parts;
	size_t start = open + 1;
	int depth = 0;
	for (size_t i = start; i < v.size() - 1; ++i)
	{
		if (v[i] == '(') ++depth;
		else if (v[i] == ')') --depth;
		else if (v[i] == ',' && depth == 0)
		{
			parts.push_back(strTrim(v.substr(start, i - start)));
			start = i + 1;
		}
	}
	parts.push_back(strTrim(v.substr(start, v.size() - 1 - start)));
	if (parts.size() < 2) return false;

	int angle = 180;
	if (radial)
	{
		uint32_t firstColor;
		if (!cssParseColor(parts[0], firstColor))
		{
			const std::string descriptor = toLower(parts[0]);
			if (descriptor.find("circle") == std::string::npos &&
				descriptor.find("ellipse") == std::string::npos &&
				descriptor.find(" at ") == std::string::npos &&
				!strStartsWith(descriptor, "at ") &&
				!strStartsWith(descriptor, "closest-") &&
				!strStartsWith(descriptor, "farthest-"))
				return false;
			parts.erase(parts.begin());
		}
	}
	else
	{
		const std::string first = toLower(parts[0]);
		if (first == "to right") { angle = 90; parts.erase(parts.begin()); }
		else if (first == "to left") { angle = 270; parts.erase(parts.begin()); }
		else if (first == "to top") { angle = 0; parts.erase(parts.begin()); }
		else if (first == "to bottom") { angle = 180; parts.erase(parts.begin()); }
		else if (first == "to top right" || first == "to right top")
			{ angle = 45; parts.erase(parts.begin()); }
		else if (first == "to bottom right" || first == "to right bottom")
			{ angle = 135; parts.erase(parts.begin()); }
		else if (first == "to bottom left" || first == "to left bottom")
			{ angle = 225; parts.erase(parts.begin()); }
		else if (first == "to top left" || first == "to left top")
			{ angle = 315; parts.erase(parts.begin()); }
		else
		{
			char* end = nullptr;
			const float degrees = strtof(first.c_str(), &end);
			if (end != first.c_str())
			{
				const std::string unit = toLower(strTrim(std::string(end)));
				if (!isfinite(degrees)) return false;
				float angleValue = degrees;
				if (unit == "deg") {}
				else if (unit == "turn") angleValue *= 360.0f;
				else if (unit == "rad") angleValue *= 57.2957795f;
				else if (unit == "grad") angleValue *= 0.9f;
				else return false;
				if (!isfinite(angleValue)) return false;
				angle = (int)fmodf(angleValue, 360.0f);
				parts.erase(parts.begin());
			}
		}
	}
	if (parts.size() < 2 || parts.size() > 8) return false;

	uint32_t colors[8] = {};
	int stops[8] = {};
	bool positioned[8] = {};
	for (size_t i = 0; i < parts.size(); ++i)
	{
		const std::string part = strTrim(parts[i]);
		size_t colorEnd = 0;
		if (strStartsWith(toLower(part), "rgb(") || strStartsWith(toLower(part), "rgba("))
		{
			colorEnd = part.find(')');
			if (colorEnd == std::string::npos) return false;
			++colorEnd;
		}
		else
		{
			colorEnd = part.find_first_of(" \t");
			if (colorEnd == std::string::npos) colorEnd = part.size();
		}

		if (!cssParseColor(part.substr(0, colorEnd), colors[i]))
			return false;
		const std::string position = strTrim(part.substr(colorEnd));
		if (!position.empty())
		{
			char* end = nullptr;
			const float percent = strtof(position.c_str(), &end);
			if (end == position.c_str() || !isfinite(percent) ||
				toLower(strTrim(std::string(end))) != "%" ||
				percent < 0.0f || percent > 100.0f)
				return false;
			stops[i] = (int)(percent * 100.0f + 0.5f);
			positioned[i] = true;
		}
	}

	if (!positioned[0]) { stops[0] = 0; positioned[0] = true; }
	if (!positioned[parts.size() - 1])
	{
		stops[parts.size() - 1] = 10000;
		positioned[parts.size() - 1] = true;
	}
	int previousPosition = stops[0];
	for (size_t i = 1; i < parts.size(); ++i)
		if (positioned[i])
		{
			if (stops[i] < previousPosition) stops[i] = previousPosition;
			previousPosition = stops[i];
		}
	for (size_t i = 1; i + 1 < parts.size();)
	{
		if (positioned[i]) { ++i; continue; }
		const size_t firstMissing = i;
		while (i < parts.size() && !positioned[i]) ++i;
		const int left = stops[firstMissing - 1];
		const int right = stops[i];
		const size_t count = i - firstMissing + 1;
		for (size_t j = firstMissing; j < i; ++j)
		{
			stops[j] = left + (int)((long long)(right - left) * (j - firstMissing + 1) / count);
			positioned[j] = true;
		}
	}

	out.gradientCount = (int)parts.size();
	out.gradientAngle = ((angle % 360) + 360) % 360;
	out.gradientRadial = radial;
	for (int i = 0; i < out.gradientCount; ++i)
	{
		out.gradientColors[i] = colors[i];
		out.gradientStops[i] = stops[i];
	}
	out.bg = colors[0];
	out.bgSet = true;
	out.declared |= CssPropBackground;
	return true;
}

//-----------------------------------------------------------------------------
// Parse d'une declaration "prop: value"
//-----------------------------------------------------------------------------
static void parseDeclarationValues(const std::string& prop, const std::string& value, Style& out)
{
	std::string p = toLower(strTrim(prop));
	std::string v = toLower(strTrim(value));

	if (p == "color" || p == "text-color")
	{
		uint32_t c;
		if (cssParseColor(v, c))
		{
			out.color = c; out.colorSet = true;
			out.declared |= CssPropColor;
		}
		return;
	}
	if (p == "background" || p == "background-color" || p == "bgcolor")
	{
		if (p == "background" && parseGradient(value, out)) return;
		uint32_t c;
		if (cssParseColor(v, c))
		{
			out.bg = c; out.bgSet = true;
			out.gradientCount = 0;
			out.gradientRadial = false;
			out.declared |= CssPropBackground;
		}
		return;
	}
	if (p == "font-size")
	{
		int px = 0;
		if (v == "xx-small") px = 9;
		else if (v == "x-small") px = 10;
		else if (v == "small") px = 13;
		else if (v == "medium") px = 16;
		else if (v == "large") px = 18;
		else if (v == "x-large") px = 24;
		else if (v == "xx-large") px = 32;
		else px = parseCssLength(v, 16, 16);
		if (px > 0)
		{
			out.fontSize = std::min(px, 256);
			out.declared |= CssPropFontSize;
		}
		return;
	}
	if (p == "line-height")
	{
		if (v == "normal")
		{
			out.lineHeight = 0;
			out.lineHeightRelative = false;
			out.lineHeightUnitless = false;
			out.declared |= CssPropLineHeight;
			return;
		}

		{
			char* end = nullptr;
			const float value = strtof(v.c_str(), &end);
			if (end != v.c_str() && value >= 0.0f && value <= 10000.0f)
			{
				const std::string unit = toLower(strTrim(std::string(end)));
				if (unit.empty())
				{
					out.lineHeight = (int)(value * 1000.0f + 0.5f);
					out.lineHeightRelative = true;
					out.lineHeightUnitless = true;
				}
				else if (unit == "em" || unit == "rem")
				{
					out.lineHeight = (int)(value * 1000.0f + 0.5f);
					out.lineHeightRelative = true;
					out.lineHeightUnitless = false;
				}
				else if (unit == "%")
				{
					out.lineHeight = (int)(value * 10.0f + 0.5f);
					out.lineHeightRelative = true;
					out.lineHeightUnitless = false;
				}
				else if (unit == "px" || unit == "pt")
				{
					out.lineHeight = std::max(0, parseCssLength(v));
					out.lineHeightRelative = false;
					out.lineHeightUnitless = false;
				}
				else
				{
					return;
				}
				out.declared |= CssPropLineHeight;
			}
		}
		return;
	}
	if (p == "font-weight")
	{
		int w = 0;
		sscanf(v.c_str(), "%d", &w);
		out.bold = (v == "bold" || v == "bolder" || v == "600" || w >= 600);
		out.declared |= CssPropBold;
		return;
	}
	if (p == "font-style")
	{
		out.italic = (v == "italic" || v == "oblique");
		out.declared |= CssPropItalic;
		return;
	}
	if (p == "text-decoration")
	{
		out.underline = (v.find("underline") != std::string::npos && v.find("none") == std::string::npos);
		out.declared |= CssPropUnderline;
		return;
	}
	if (p == "text-transform")
	{
		if (v == "uppercase") out.textTransform = CssTextTransformUppercase;
		else if (v == "lowercase") out.textTransform = CssTextTransformLowercase;
		else if (v == "capitalize") out.textTransform = CssTextTransformCapitalize;
		else if (v == "none") out.textTransform = CssTextTransformNone;
		else return;
		out.declared |= CssPropTextTransform;
		return;
	}
	if (p == "letter-spacing")
	{
		if (v == "normal")
		{
			out.letterSpacing = 0.0f;
			out.letterSpacingIsEm = false;
		}
		else
		{
			char* end = nullptr;
			float spacing = strtof(v.c_str(), &end);
			if (end == v.c_str() || !isfinite(spacing)) return;
			const std::string unit = toLower(strTrim(std::string(end)));
			if (unit == "em")
				out.letterSpacingIsEm = true;
			else if (unit == "px" || unit.empty())
				out.letterSpacingIsEm = false;
			else
				return;
			out.letterSpacing = std::max(-100.0f, std::min(100.0f, spacing));
		}
		out.declared |= CssPropLetterSpacing;
		return;
	}
	if (p == "white-space")
	{
		if (v == "normal") out.whiteSpace = CssWhiteSpaceNormal;
		else if (v == "nowrap") out.whiteSpace = CssWhiteSpaceNoWrap;
		else if (v == "pre") out.whiteSpace = CssWhiteSpacePre;
		else if (v == "pre-line") out.whiteSpace = CssWhiteSpacePreLine;
		else if (v == "pre-wrap") out.whiteSpace = CssWhiteSpacePreWrap;
		else return;
		out.declared |= CssPropWhiteSpace;
		return;
	}
	if (p == "text-align")
	{
		if (v == "center") out.align = 1;
		else if (v == "right" || v == "end") out.align = 2;
		else if (v == "justify") out.align = 3;
		else if (v != "left" && v != "start") return;
		else out.align = 0;
		out.declared |= CssPropAlign;
		return;
	}
	if (p == "vertical-align")
	{
		if (v == "middle") out.verticalAlignMiddle = true;
		else if (v == "top" || v == "bottom" || v == "baseline")
			out.verticalAlignMiddle = false;
		else
			return;
		out.declared |= CssPropVerticalAlign;
		return;
	}
	if (p == "position")
	{
		if (v == "static")
		{
			out.positionRelative = false;
			out.positionAbsolute = false;
			out.positionFixed = false;
		}
		else if (v == "relative")
		{
			out.positionRelative = true;
			out.positionAbsolute = false;
			out.positionFixed = false;
		}
		else if (v == "absolute")
		{
			out.positionRelative = false;
			out.positionAbsolute = true;
			out.positionFixed = false;
		}
		else if (v == "fixed")
		{
			out.positionRelative = false;
			out.positionAbsolute = false;
			out.positionFixed = true;
		}
		else
			return;
		out.declared |= CssPropPosition;
		return;
	}
	if (p == "left" || p == "right" || p == "top" || p == "bottom")
	{
		const int offset = v == "auto" ? 0 : parseCssLength(v);
		int percent = 0;
		const bool isPercent = parseCssPercent(v, percent);
		if (p == "left")
		{
			out.positionLeft = offset;
			out.positionLeftPercent = percent;
			out.positionLeftIsPercent = isPercent;
			out.declared |= CssPropLeft;
		}
		else if (p == "right")
		{
			out.positionRight = offset;
			out.positionRightPercent = percent;
			out.positionRightIsPercent = isPercent;
			out.declared |= CssPropRight;
		}
		else if (p == "top")
		{
			out.positionTop = offset;
			out.positionTopPercent = percent;
			out.positionTopIsPercent = isPercent;
			out.declared |= CssPropTop;
		}
		else
		{
			out.positionBottom = offset;
			out.positionBottomPercent = percent;
			out.positionBottomIsPercent = isPercent;
			out.declared |= CssPropBottom;
		}
		return;
	}
	if (p == "overflow" || p == "overflow-x" || p == "overflow-y")
	{
		const std::vector<std::string> values = splitCssWhitespace(v);
		if (values.empty() || (p == "overflow" && values.size() > 2)) return;
		auto known = [](const std::string& value)
		{
			return value == "visible" || value == "hidden" ||
				value == "clip" || value == "auto" || value == "scroll";
		};
		if (!known(values[0]) ||
			(p == "overflow" && values.size() > 1 && !known(values[1])))
			return;
		auto clips = [](const std::string& value)
		{
			return value == "hidden" || value == "clip" ||
				value == "auto" || value == "scroll";
		};
		if (p == "overflow")
		{
			out.overflowXClip = clips(values[0]);
			out.overflowYClip = clips(values.size() > 1 ? values[1] : values[0]);
			out.declared |= CssPropOverflowX | CssPropOverflowY;
		}
		else if (p == "overflow-x")
		{
			out.overflowXClip = clips(values[0]);
			out.declared |= CssPropOverflowX;
		}
		else
		{
			out.overflowYClip = clips(values[0]);
			out.declared |= CssPropOverflowY;
		}
		return;
	}
	if (p == "display")
	{
		if (v == "none")
		{
			out.displayNone = true; out.block = false; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "block")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "flex")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = true;
			out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "grid" || v == "inline-grid")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = true;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "inline")
		{
			out.displayNone = false; out.block = false; out.displayInline = true;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "inline-block")
		{
			out.displayNone = false; out.block = false; out.displayInline = false;
			out.displayInlineBlock = true;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "table")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = true; out.displayTableRow = false;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "table-row")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = true;
			out.displayTableCell = false;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		else if (v == "table-cell")
		{
			out.displayNone = false; out.block = true; out.displayInline = false;
			out.displayInlineBlock = false;
			out.flex = false; out.grid = false;
			out.displayTable = false; out.displayTableRow = false;
			out.displayTableCell = true;
			out.declared |= CssPropDisplay | CssPropFlex | CssPropGrid;
		}
		return;
	}
	if (p == "grid-template-columns")
	{
		std::vector<CssGridTrack> tracks;
		const std::vector<std::string> tokens = splitCssWhitespace(v);
		for (const std::string& token : tokens)
		{
			std::string track = token;
			if (strStartsWith(track, "repeat(") && track.back() == ')')
			{
				const size_t comma = findCssFunctionComma(track);
				if (comma == std::string::npos) continue;
				const std::string repeatCount = strTrim(track.substr(7, comma - 7));
				const bool autoFit = repeatCount == "auto-fit";
				const bool autoRepeat = autoFit || repeatCount == "auto-fill";
				const int count = autoRepeat ? 1 : atoi(repeatCount.c_str());
				if (count <= 0 || count > 12) continue;
				track = strTrim(track.substr(comma + 1,
					track.size() - comma - 2));
				CssGridTrack parsed;
				if (strStartsWith(track, "minmax("))
				{
					const size_t innerComma = findCssFunctionComma(track);
					const size_t close = track.rfind(')');
					if (innerComma != std::string::npos && close > innerComma)
					{
						const std::string minimum = strTrim(track.substr(
							7, innerComma - 7));
						const std::string maximum = strTrim(track.substr(
							innerComma + 1, close - innerComma - 1));
						parsed.minWidth = std::max(0, parseCssLength(minimum));
						track = maximum;
					}
				}
				const size_t fr = track.find("fr");
				if (fr != std::string::npos)
				{
					const float weight = strtof(track.c_str(), nullptr);
					parsed.fraction = std::max(1, std::min(1000,
						(int)(weight * 100.0f + 0.5f)));
				}
				else
				{
					parsed.fixedWidth = std::max(0, parseCssLength(track));
					parsed.fraction = 0;
				}
				if (autoRepeat)
				{
					parsed.autoRepeat = true;
					parsed.autoFit = autoFit;
					tracks.push_back(parsed);
					continue;
				}
				for (int i = 0; i < count && tracks.size() < 12; ++i)
					tracks.push_back(parsed);
				continue;
			}

			CssGridTrack parsed;
			if (strStartsWith(track, "minmax("))
			{
				const size_t comma = findCssFunctionComma(track);
				const size_t close = track.rfind(')');
				if (comma != std::string::npos && close > comma)
				{
					parsed.minWidth = std::max(0, parseCssLength(
						strTrim(track.substr(7, comma - 7))));
					track = strTrim(track.substr(comma + 1,
						close - comma - 1));
				}
			}
			const size_t fr = track.find("fr");
			if (fr != std::string::npos)
			{
				const float weight = strtof(track.c_str(), nullptr);
				parsed.fraction = std::max(1, std::min(1000,
					(int)(weight * 100.0f + 0.5f)));
			}
			else if (track == "auto" || track == "max-content" ||
				track == "min-content")
			{
				parsed.fraction = 1;
			}
			else
			{
				parsed.fixedWidth = std::max(0, parseCssLength(track));
				parsed.fraction = 0;
			}
			if (tracks.size() < 12) tracks.push_back(parsed);
		}
		if (!tracks.empty())
		{
			out.gridColumns = tracks;
			out.grid = true;
			out.block = true;
			out.displayNone = false;
			out.declared |= CssPropGridColumns | CssPropGrid;
		}
		return;
	}
	if (p == "grid-column")
	{
		int span = 1;
		const std::vector<std::string> parts = splitCssWhitespace(v);
		if (parts.size() == 2 && parts[0] == "span")
			span = atoi(parts[1].c_str());
		else if (parts.size() == 3 && parts[1] == "/")
		{
			const int start = atoi(parts[0].c_str());
			const int end = atoi(parts[2].c_str());
			if (start > 0 && end > start) span = end - start;
		}
		else if (parts.size() == 4 && parts[1] == "/" &&
			parts[2] == "span")
			span = atoi(parts[3].c_str());
		if (span > 0 && span <= 12)
		{
			out.gridColumnSpan = span;
			out.declared |= CssPropGridColumnSpan;
		}
		return;
	}
	if (p == "flex-direction")
	{
		out.flexRow = (v == "row" || v == "row-reverse");
		out.flexReverse = (v == "row-reverse" || v == "column-reverse");
		out.flex = true;
		out.declared |= CssPropFlex | CssPropFlexRow;
		return;
	}
	if (p == "flex-grow" || p == "flex")
	{
		std::string grow = p == "flex-grow" ? v : strTrim(v.substr(0,
			v.find_first_of(" \t")));
		char* end = nullptr;
		const float parsed = strtof(grow.c_str(), &end);
		if (end == grow.c_str() || *end != '\0' || !isfinite(parsed) ||
			parsed < 0.0f || parsed > 100.0f)
			return;
		out.flexGrow = (int)(parsed * 100.0f + 0.5f);
		out.flex = true;
		out.declared |= CssPropFlex | CssPropFlexGrow;
		return;
	}
	if (p == "flex-wrap")
	{
		out.flexWrap = (v == "wrap" || v == "wrap-reverse");
		out.flex = true;
		out.declared |= CssPropFlex | CssPropFlexWrap;
		return;
	}
	if (p == "justify-content")
	{
		if (v == "center") out.justifyContent = 1;
		else if (v == "space-between") out.justifyContent = 2;
		else if (v == "flex-end" || v == "end" || v == "right")
			out.justifyContent = 3;
		else if (v == "space-around") out.justifyContent = 4;
		else if (v == "space-evenly") out.justifyContent = 5;
		else out.justifyContent = 0;
		out.flex = true;
		out.declared |= CssPropFlex | CssPropJustify;
		return;
	}
	if (p == "gap" || p == "row-gap" || p == "column-gap")
	{
		std::vector<std::string> tokens;
		size_t pos = 0;
		while (pos < v.size())
		{
			while (pos < v.size() && (v[pos] == ' ' || v[pos] == '\t')) ++pos;
			if (pos >= v.size()) break;
			size_t next = v.find_first_of(" \t", pos);
			if (next == std::string::npos) next = v.size();
			tokens.push_back(v.substr(pos, next - pos));
			pos = next;
		}
		if (p == "row-gap")
		{
			out.rowGap = std::max(0, parseCssLength(v));
			out.declared |= CssPropRowGap;
		}
		else if (p == "column-gap")
		{
			out.columnGap = std::max(0, parseCssLength(v));
			out.declared |= CssPropColumnGap;
		}
		else if (!tokens.empty())
		{
			out.rowGap = std::max(0, parseCssLength(tokens[0]));
			out.columnGap = tokens.size() > 1
				? std::max(0, parseCssLength(tokens[1])) : out.rowGap;
			out.declared |= CssPropRowGap | CssPropColumnGap;
		}
		return;
	}
	if (p == "box-sizing")
	{
		out.borderBox = (v == "border-box");
		out.declared |= CssPropBoxSizing;
		return;
	}
	if (p == "margin" || p == "padding")
	{
		std::vector<std::string> tokens;
		size_t pos = 0;
		while (pos < v.size())
		{
			while (pos < v.size() && (v[pos] == ' ' || v[pos] == '\t')) pos++;
			if (pos >= v.size()) break;
			size_t next = v.find_first_of(" \t", pos);
			if (next == std::string::npos) next = v.size();
			tokens.push_back(v.substr(pos, next - pos));
			pos = next;
		}
		int m[4] = { 0, 0, 0, 0 };
		if (tokens.size() == 1)
		{
			m[0] = m[1] = m[2] = m[3] = parseCssLength(tokens[0]);
		}
		else if (tokens.size() == 2)
		{
			m[0] = m[2] = parseCssLength(tokens[0]);
			m[1] = m[3] = parseCssLength(tokens[1]);
		}
		else if (tokens.size() == 3)
		{
			m[0] = parseCssLength(tokens[0]);
			m[1] = m[3] = parseCssLength(tokens[1]);
			m[2] = parseCssLength(tokens[2]);
		}
		else if (tokens.size() >= 4)
		{
			m[0] = parseCssLength(tokens[0]);
			m[1] = parseCssLength(tokens[1]);
			m[2] = parseCssLength(tokens[2]);
			m[3] = parseCssLength(tokens[3]);
		}
		int percentages[4] = {};
		bool isPercent[4] = {};
		if (!tokens.empty())
		{
			for (int side = 0; side < 4; ++side)
			{
				const size_t tokenIndex = tokens.size() == 1 ? 0 :
					tokens.size() == 2 ? (side == 1 || side == 3 ? 1 : 0) :
					tokens.size() == 3 ? (side == 0 ? 0 : side == 2 ? 2 : 1) :
					(size_t)side;
				if (tokenIndex < tokens.size())
					isPercent[side] = parseCssPercent(tokens[tokenIndex],
						percentages[side]);
			}
		}
		if (p == "margin")
		{
			out.marginTop = m[0]; out.marginRight = m[1]; out.marginBottom = m[2]; out.marginLeft = m[3];
			out.marginTopPercent = percentages[0];
			out.marginRightPercent = percentages[1];
			out.marginBottomPercent = percentages[2];
			out.marginLeftPercent = percentages[3];
			out.marginTopIsPercent = isPercent[0];
			out.marginRightIsPercent = isPercent[1];
			out.marginBottomIsPercent = isPercent[2];
			out.marginLeftIsPercent = isPercent[3];
			const size_t leftToken = tokens.size() >= 4 ? 3 : (tokens.size() == 3 ? 1 : (tokens.size() == 2 ? 1 : 0));
			const size_t rightToken = tokens.size() >= 4 ? 1 : (tokens.size() == 3 ? 1 : (tokens.size() == 2 ? 1 : 0));
			const bool autoLeft = !tokens.empty() && tokens[leftToken] == "auto";
			const bool autoRight = !tokens.empty() && tokens[rightToken] == "auto";
			out.marginLeftAuto = autoLeft;
			out.marginRightAuto = autoRight;
			out.declared |= CssPropMarginTop | CssPropMarginRight |
				CssPropMarginBottom | CssPropMarginLeft |
				CssPropMarginLeftAuto | CssPropMarginRightAuto;
		}
		else
		{
			out.paddingTop = m[0]; out.paddingRight = m[1]; out.paddingBottom = m[2]; out.paddingLeft = m[3];
			out.paddingTopPercent = percentages[0];
			out.paddingRightPercent = percentages[1];
			out.paddingBottomPercent = percentages[2];
			out.paddingLeftPercent = percentages[3];
			out.paddingTopIsPercent = isPercent[0];
			out.paddingRightIsPercent = isPercent[1];
			out.paddingBottomIsPercent = isPercent[2];
			out.paddingLeftIsPercent = isPercent[3];
			out.declared |= CssPropPaddingTop | CssPropPaddingRight |
				CssPropPaddingBottom | CssPropPaddingLeft;
		}
		return;
	}
	if (p == "margin-top" || p == "margin-bottom" || p == "margin-left" || p == "margin-right")
	{
		int m = parseCssLength(v);
		int percent = 0;
		const bool isPercent = parseCssPercent(v, percent);
		if (p == "margin-top") { out.marginTop = m; out.marginTopPercent = percent; out.marginTopIsPercent = isPercent; out.declared |= CssPropMarginTop; }
		else if (p == "margin-bottom") { out.marginBottom = m; out.marginBottomPercent = percent; out.marginBottomIsPercent = isPercent; out.declared |= CssPropMarginBottom; }
		else if (p == "margin-left") { out.marginLeft = m; out.marginLeftPercent = percent; out.marginLeftIsPercent = isPercent; out.marginLeftAuto = (v == "auto"); out.declared |= CssPropMarginLeft | CssPropMarginLeftAuto; }
		else { out.marginRight = m; out.marginRightPercent = percent; out.marginRightIsPercent = isPercent; out.marginRightAuto = (v == "auto"); out.declared |= CssPropMarginRight | CssPropMarginRightAuto; }
		return;
	}
	if (p == "padding-top" || p == "padding-bottom" || p == "padding-left" || p == "padding-right")
	{
		int m = parseCssLength(v);
		int percent = 0;
		const bool isPercent = parseCssPercent(v, percent);
		if (p == "padding-top") { out.paddingTop = m; out.paddingTopPercent = percent; out.paddingTopIsPercent = isPercent; out.declared |= CssPropPaddingTop; }
		else if (p == "padding-bottom") { out.paddingBottom = m; out.paddingBottomPercent = percent; out.paddingBottomIsPercent = isPercent; out.declared |= CssPropPaddingBottom; }
		else if (p == "padding-left") { out.paddingLeft = m; out.paddingLeftPercent = percent; out.paddingLeftIsPercent = isPercent; out.declared |= CssPropPaddingLeft; }
		else { out.paddingRight = m; out.paddingRightPercent = percent; out.paddingRightIsPercent = isPercent; out.declared |= CssPropPaddingRight; }
		return;
	}
	if (p == "width")
	{
		int percent = 0;
		if (parseCssPercent(v, percent))
		{
			out.width = parseCssLength(v, 16, gCssViewportWidth);
			out.widthPercent = percent;
			out.widthIsPercent = true;
			out.declared |= CssPropWidth;
		}
		else
		{
			int w = parseCssLength(v);
			if (w > 0) { out.width = w; out.declared |= CssPropWidth; }
		}
		return;
	}
	if (p == "min-width" || p == "max-width")
	{
		int percent = 0;
		const bool isPercent = parseCssPercent(v, percent);
		const int w = parseCssLength(v, 16, gCssViewportWidth);
		if (p == "min-width")
		{
			out.minWidth = w;
			out.minWidthPercent = percent;
			out.minWidthIsPercent = isPercent;
			out.declared |= CssPropMinWidth;
		}
		else
		{
			out.maxWidth = w;
			out.maxWidthPercent = percent;
			out.maxWidthIsPercent = isPercent;
			out.declared |= CssPropMaxWidth;
		}
		return;
	}
	if (p == "height" || p == "min-height" || p == "max-height")
	{
		const int h = parseCssLength(v, 16, cssViewportHeight());
		int percent = 0;
		const bool isPercent = parseCssPercent(v, percent);
		if (p == "height" && h > 0)
		{
			out.height = h;
			out.heightPercent = percent;
			out.heightIsPercent = isPercent;
			out.declared |= CssPropHeight;
		}
		else if (p == "height" && isPercent)
		{
			out.height = 0;
			out.heightPercent = percent;
			out.heightIsPercent = true;
			out.declared |= CssPropHeight;
		}
		else if (p == "min-height")
		{
			out.minHeight = std::max(0, h);
			out.minHeightPercent = percent;
			out.minHeightIsPercent = isPercent;
			out.declared |= CssPropMinHeight;
		}
		else if (p == "max-height")
		{
			out.maxHeight = std::max(0, h);
			out.maxHeightPercent = percent;
			out.maxHeightIsPercent = isPercent;
			out.declared |= CssPropMaxHeight;
		}
		return;
	}
	if (p == "font-family")
	{
		out.mono = (v.find("mono") != std::string::npos || v.find("courier") != std::string::npos);
		out.declared |= CssPropMono;
		return;
	}
	if (p == "list-style" || p == "list-style-type")
	{
		out.listStyleNone = (v == "none");
		out.declared |= CssPropListStyle;
		return;
	}
	if (p == "border-radius")
	{
		std::string horizontal = strTrim(value);
		const size_t slash = horizontal.find('/');
		if (slash != std::string::npos) horizontal = strTrim(horizontal.substr(0, slash));
		const std::vector<std::string> radii = splitCssWhitespace(horizontal);
		if (!radii.empty())
		{
			out.borderRadius = std::max(0, std::min(1000, parseCssLength(radii[0])));
			out.declared |= CssPropBorderRadius;
		}
		return;
	}
	if (p == "box-shadow")
	{
		if (v == "none")
		{
			out.boxShadowSet = false;
			out.declared |= CssPropBoxShadow;
			return;
		}
		if (v.find("inset") != std::string::npos) return;
		const std::vector<std::string> tokens = splitCssWhitespace(value);
		int lengths[4] = {};
		int lengthCount = 0;
		uint32_t shadowColor = 0x66000000;
		bool colorSet = false;
		for (const std::string& token : tokens)
		{
			uint32_t parsedColor;
			if (!colorSet && cssParseColor(token, parsedColor))
			{
				shadowColor = parsedColor;
				colorSet = true;
				continue;
			}
			if (lengthCount >= 4) return;
			char* end = nullptr;
			const float numeric = strtof(token.c_str(), &end);
			if (end == token.c_str() || !isfinite(numeric)) return;
			if (numeric != 0.0f && *end == '\0') return;
			const int length = parseCssLength(token);
			lengths[lengthCount++] = length;
		}
		if (lengthCount < 2) return;
		out.shadowX = std::max(-64, std::min(64, lengths[0]));
		out.shadowY = std::max(-64, std::min(64, lengths[1]));
		out.shadowBlur = lengthCount > 2
			? std::max(0, std::min(24, lengths[2])) : 0;
		out.shadowSpread = lengthCount > 3
			? std::max(-16, std::min(16, lengths[3])) : 0;
		out.shadowColor = shadowColor;
		out.boxShadowSet = true;
		out.declared |= CssPropBoxShadow;
		return;
	}
	const char* borderSides[] = { "top", "right", "bottom", "left" };
	const uint64_t borderProperties[4][3] = {
		{ CssPropBorderTopWidth, CssPropBorderTopColor, CssPropBorderTopStyle },
		{ CssPropBorderRightWidth, CssPropBorderRightColor, CssPropBorderRightStyle },
		{ CssPropBorderBottomWidth, CssPropBorderBottomColor, CssPropBorderBottomStyle },
		{ CssPropBorderLeftWidth, CssPropBorderLeftColor, CssPropBorderLeftStyle }
	};
	CssBorderSide* sides[] = {
		&out.borderTop, &out.borderRight, &out.borderBottom, &out.borderLeft
	};
	for (int sideIndex = -1; sideIndex < 4; ++sideIndex)
	{
		const std::string side = sideIndex < 0 ? "" :
			std::string("-") + borderSides[sideIndex];
		const std::string prefix = std::string("border") + side;
		const bool shorthand = p == prefix;
		const bool widthProperty = p == prefix + "-width";
		const bool colorProperty = p == prefix + "-color";
		const bool styleProperty = p == prefix + "-style";
		if (!shorthand && !widthProperty && !colorProperty && !styleProperty)
			continue;

		std::vector<std::string> tokens = splitCssWhitespace(value);
		if (tokens.empty()) return;
		if (shorthand)
		{
			const int firstSide = sideIndex < 0 ? 0 : sideIndex;
			const int lastSide = sideIndex < 0 ? 3 : sideIndex;
			for (int i = firstSide; i <= lastSide; ++i)
				*sides[i] = CssBorderSide{ 1, 0xFF968A8A, CssBorderSolid };

			for (const std::string& token : tokens)
			{
				const std::string lowerToken = toLower(token);
				uint32_t parsedColor;
				if (cssParseColor(token, parsedColor))
				{
					for (int i = firstSide; i <= lastSide; ++i)
						sides[i]->color = parsedColor;
				}
				else if (lowerToken == "none" || lowerToken == "hidden" ||
					lowerToken == "solid" || lowerToken == "dashed" ||
					lowerToken == "dotted")
				{
					const CssBorderStyle borderStyle =
						lowerToken == "none" || lowerToken == "hidden" ? CssBorderNone :
						lowerToken == "dashed" ? CssBorderDashed :
						lowerToken == "dotted" ? CssBorderDotted : CssBorderSolid;
					for (int i = firstSide; i <= lastSide; ++i)
						sides[i]->style = borderStyle;
				}
				else
				{
					int width = 0;
					if (lowerToken == "thin") width = 1;
					else if (lowerToken == "medium") width = 3;
					else if (lowerToken == "thick") width = 5;
					else
					{
						char* end = nullptr;
						strtof(lowerToken.c_str(), &end);
						if (end == lowerToken.c_str() ||
							(*end != '\0' && strcmp(end, "px") != 0 &&
							 strcmp(end, "em") != 0 && strcmp(end, "rem") != 0 &&
							 strcmp(end, "pt") != 0))
							continue;
						width = std::max(0, parseCssLength(lowerToken));
					}
					for (int i = firstSide; i <= lastSide; ++i)
						sides[i]->width = width;
				}
			}
			for (int i = firstSide; i <= lastSide; ++i)
				out.declared |= borderProperties[i][0] |
					borderProperties[i][1] | borderProperties[i][2];
		}
		else
		{
			int targets[4];
			int targetCount = 0;
			if (sideIndex >= 0)
			{
				targets[targetCount++] = sideIndex;
				if (widthProperty)
				{
					const std::string token = toLower(tokens[0]);
					sides[sideIndex]->width = token == "thin" ? 1 :
						token == "medium" ? 3 : token == "thick" ? 5 :
						std::max(0, parseCssLength(token));
				}
				else if (styleProperty)
				{
					const std::string token = toLower(tokens[0]);
					if (token == "none" || token == "hidden")
						sides[sideIndex]->style = CssBorderNone;
					else if (token == "dashed")
						sides[sideIndex]->style = CssBorderDashed;
					else if (token == "dotted")
						sides[sideIndex]->style = CssBorderDotted;
					else if (token == "solid")
						sides[sideIndex]->style = CssBorderSolid;
					else return;
				}
				else
				{
					uint32_t color;
					if (!cssParseColor(tokens[0], color)) return;
					sides[sideIndex]->color = color;
				}
			}
			else if (widthProperty || styleProperty)
			{
				int values[4] = {};
				const int count = std::min(4, (int)tokens.size());
				for (int i = 0; i < count; ++i)
				{
					if (widthProperty)
					{
						const std::string token = toLower(tokens[i]);
						values[i] = token == "thin" ? 1 : token == "medium" ? 3 :
							token == "thick" ? 5 : std::max(0, parseCssLength(token));
					}
					else
					{
						const std::string token = toLower(tokens[i]);
						values[i] = token == "none" || token == "hidden"
							? CssBorderNone : token == "dashed" ? CssBorderDashed :
							token == "dotted" ? CssBorderDotted : CssBorderSolid;
					}
				}
				for (int i = 0; i < 4; ++i)
				{
					int value = 0;
					if (count == 1) value = values[0];
					else if (count == 2) value = values[i == 1 || i == 3 ? 1 : 0];
					else if (count == 3) value = values[i == 0 ? 0 : i == 2 ? 2 : 1];
					else if (count >= 4) value = values[i];
					else continue;
					if (widthProperty) sides[i]->width = value;
					else sides[i]->style = (CssBorderStyle)value;
					targets[targetCount++] = i;
				}
			}
			else
			{
				uint32_t colors[4];
				const int count = std::min(4, (int)tokens.size());
				for (int i = 0; i < count; ++i)
					if (!cssParseColor(tokens[i], colors[i])) return;
				for (int i = 0; i < 4; ++i)
				{
					int colorIndex = count == 1 ? 0 :
						count == 2 ? (i == 1 || i == 3 ? 1 : 0) :
						count == 3 ? (i == 0 ? 0 : i == 2 ? 2 : 1) : i;
					sides[i]->color = colors[colorIndex];
					targets[targetCount++] = i;
				}
			}

			for (int i = 0; i < targetCount; ++i)
			{
				const int target = targets[i];
				out.declared |= borderProperties[target][widthProperty ? 0 :
					colorProperty ? 1 : 2];
			}
		}

		if (sideIndex < 0)
		{
			out.borderWidth = out.borderTop.width;
			out.borderColor = out.borderTop.color;
			out.borderStyle = out.borderTop.style;
			out.declared |= CssPropBorder;
		}
		return;
	}
}

static bool parseImageFit(const std::string& value, CssImageFit& fit)
{
	const std::string normalized = toLower(strTrim(value));
	if (normalized == "fill") fit = CssImageFitFill;
	else if (normalized == "contain") fit = CssImageFitContain;
	else if (normalized == "cover") fit = CssImageFitCover;
	else return false;
	return true;
}

static bool parseImagePositionComponent(const std::string& token,
	bool horizontal, int& position)
{
	if (token == "center")
	{
		position = 50;
		return true;
	}
	if (horizontal && token == "left") { position = 0; return true; }
	if (horizontal && token == "right") { position = 100; return true; }
	if (!horizontal && token == "top") { position = 0; return true; }
	if (!horizontal && token == "bottom") { position = 100; return true; }
	if (token.empty() || token.back() != '%') return false;

	char* end = nullptr;
	const double parsed = strtod(token.c_str(), &end);
	if (end != token.c_str() + token.size() - 1 ||
		!(parsed >= 0.0 && parsed <= 100.0))
		return false;
	position = (int)(parsed + 0.5);
	return true;
}

static bool parseImagePosition(const std::string& value, int& x, int& y)
{
	const std::string normalized = toLower(strTrim(value));
	std::vector<std::string> tokens;
	for (size_t start = 0; start < normalized.size();)
	{
		while (start < normalized.size() &&
			isspace((unsigned char)normalized[start])) ++start;
		if (start == normalized.size()) break;
		const size_t end = normalized.find_first_of(" \t\r\n", start);
		tokens.push_back(normalized.substr(start,
			end == std::string::npos ? std::string::npos : end - start));
		if (tokens.size() > 2) return false;
		if (end == std::string::npos) break;
		start = end + 1;
	}
	if (tokens.empty()) return false;
	if (tokens.size() == 1)
	{
		if (tokens[0] == "left" || tokens[0] == "right")
		{
			x = tokens[0] == "left" ? 0 : 100;
			y = 50;
			return true;
		}
		if (tokens[0] == "top" || tokens[0] == "bottom")
		{
			x = 50;
			y = tokens[0] == "top" ? 0 : 100;
			return true;
		}
		if (tokens[0] == "center")
		{
			x = y = 50;
			return true;
		}
		if (!parseImagePositionComponent(tokens[0], true, x)) return false;
		y = 50;
		return true;
	}

	return parseImagePositionComponent(tokens[0], true, x) &&
		parseImagePositionComponent(tokens[1], false, y);
}

static void applyProperty(Style& out, const Style& source, uint64_t property)
{
	out.declared |= property;
	switch (property)
	{
	case CssPropColor: out.color = source.color; out.colorSet = source.colorSet; break;
	case CssPropBackground:
		out.bg = source.bg;
		out.bgSet = source.bgSet;
		out.gradientAngle = source.gradientAngle;
		out.gradientCount = source.gradientCount;
		out.gradientRadial = source.gradientRadial;
		for (int i = 0; i < 8; ++i)
		{
			out.gradientColors[i] = source.gradientColors[i];
			out.gradientStops[i] = source.gradientStops[i];
		}
		break;
	case CssPropFontSize: out.fontSize = source.fontSize; break;
	case CssPropLineHeight:
		out.lineHeight = source.lineHeight;
		out.lineHeightRelative = source.lineHeightRelative;
		out.lineHeightUnitless = source.lineHeightUnitless;
		break;
	case CssPropBold: out.bold = source.bold; break;
	case CssPropItalic: out.italic = source.italic; break;
	case CssPropUnderline: out.underline = source.underline; break;
	case CssPropWhiteSpace: out.whiteSpace = source.whiteSpace; break;
	case CssPropTextTransform: out.textTransform = source.textTransform; break;
	case CssPropLetterSpacing:
		out.letterSpacing = source.letterSpacing;
		out.letterSpacingIsEm = source.letterSpacingIsEm;
		break;
	case CssPropAlign: out.align = source.align; break;
	case CssPropMarginTop:
		out.marginTop = source.marginTop;
		out.marginTopPercent = source.marginTopPercent;
		out.marginTopIsPercent = source.marginTopIsPercent;
		break;
	case CssPropMarginRight:
		out.marginRight = source.marginRight;
		out.marginRightPercent = source.marginRightPercent;
		out.marginRightIsPercent = source.marginRightIsPercent;
		break;
	case CssPropMarginBottom:
		out.marginBottom = source.marginBottom;
		out.marginBottomPercent = source.marginBottomPercent;
		out.marginBottomIsPercent = source.marginBottomIsPercent;
		break;
	case CssPropMarginLeft:
		out.marginLeft = source.marginLeft;
		out.marginLeftPercent = source.marginLeftPercent;
		out.marginLeftIsPercent = source.marginLeftIsPercent;
		break;
	case CssPropPaddingTop:
		out.paddingTop = source.paddingTop;
		out.paddingTopPercent = source.paddingTopPercent;
		out.paddingTopIsPercent = source.paddingTopIsPercent;
		break;
	case CssPropPaddingRight:
		out.paddingRight = source.paddingRight;
		out.paddingRightPercent = source.paddingRightPercent;
		out.paddingRightIsPercent = source.paddingRightIsPercent;
		break;
	case CssPropPaddingBottom:
		out.paddingBottom = source.paddingBottom;
		out.paddingBottomPercent = source.paddingBottomPercent;
		out.paddingBottomIsPercent = source.paddingBottomIsPercent;
		break;
	case CssPropPaddingLeft:
		out.paddingLeft = source.paddingLeft;
		out.paddingLeftPercent = source.paddingLeftPercent;
		out.paddingLeftIsPercent = source.paddingLeftIsPercent;
		break;
	case CssPropWidth:
		out.width = source.width;
		out.widthPercent = source.widthPercent;
		out.widthIsPercent = source.widthIsPercent;
		break;
	case CssPropMinWidth:
		out.minWidth = source.minWidth;
		out.minWidthPercent = source.minWidthPercent;
		out.minWidthIsPercent = source.minWidthIsPercent;
		break;
	case CssPropMaxWidth:
		out.maxWidth = source.maxWidth;
		out.maxWidthPercent = source.maxWidthPercent;
		out.maxWidthIsPercent = source.maxWidthIsPercent;
		break;
	case CssPropBoxSizing: out.borderBox = source.borderBox; break;
	case CssPropFlex: out.flex = source.flex; break;
	case CssPropFlexRow:
		out.flexRow = source.flexRow;
		out.flexReverse = source.flexReverse;
		break;
	case CssPropFlexWrap: out.flexWrap = source.flexWrap; break;
	case CssPropJustify: out.justifyContent = source.justifyContent; break;
	case CssPropFlexGrow: out.flexGrow = source.flexGrow; break;
	case CssPropOverflowX: out.overflowXClip = source.overflowXClip; break;
	case CssPropOverflowY: out.overflowYClip = source.overflowYClip; break;
	case CssPropPosition:
		out.positionRelative = source.positionRelative;
		out.positionAbsolute = source.positionAbsolute;
		out.positionFixed = source.positionFixed;
		break;
	case CssPropLeft:
		out.positionLeft = source.positionLeft;
		out.positionLeftPercent = source.positionLeftPercent;
		out.positionLeftIsPercent = source.positionLeftIsPercent;
		break;
	case CssPropRight:
		out.positionRight = source.positionRight;
		out.positionRightPercent = source.positionRightPercent;
		out.positionRightIsPercent = source.positionRightIsPercent;
		break;
	case CssPropTop:
		out.positionTop = source.positionTop;
		out.positionTopPercent = source.positionTopPercent;
		out.positionTopIsPercent = source.positionTopIsPercent;
		break;
	case CssPropBottom:
		out.positionBottom = source.positionBottom;
		out.positionBottomPercent = source.positionBottomPercent;
		out.positionBottomIsPercent = source.positionBottomIsPercent;
		break;
	case CssPropMarginLeftAuto: out.marginLeftAuto = source.marginLeftAuto; break;
	case CssPropMarginRightAuto: out.marginRightAuto = source.marginRightAuto; break;
	case CssPropHeight:
		out.height = source.height;
		out.heightPercent = source.heightPercent;
		out.heightIsPercent = source.heightIsPercent;
		break;
	case CssPropMinHeight:
		out.minHeight = source.minHeight;
		out.minHeightPercent = source.minHeightPercent;
		out.minHeightIsPercent = source.minHeightIsPercent;
		break;
	case CssPropMaxHeight:
		out.maxHeight = source.maxHeight;
		out.maxHeightPercent = source.maxHeightPercent;
		out.maxHeightIsPercent = source.maxHeightIsPercent;
		break;
	case CssPropDisplay:
		out.displayNone = source.displayNone;
		out.block = source.block;
		out.displayInline = source.displayInline;
		out.displayInlineBlock = source.displayInlineBlock;
		out.displayTable = source.displayTable;
		out.displayTableRow = source.displayTableRow;
		out.displayTableCell = source.displayTableCell;
		break;
	case CssPropVerticalAlign:
		out.verticalAlignMiddle = source.verticalAlignMiddle;
		break;
	case CssPropGrid: out.grid = source.grid; break;
	case CssPropGridColumns: out.gridColumns = source.gridColumns; break;
	case CssPropGridColumnSpan: out.gridColumnSpan = source.gridColumnSpan; break;
	case CssPropMono: out.mono = source.mono; break;
	case CssPropListStyle: out.listStyleNone = source.listStyleNone; break;
	case CssPropBorder:
		out.borderWidth = source.borderWidth;
		out.borderColor = source.borderColor;
		out.borderStyle = source.borderStyle;
		break;
	case CssPropBorderTopWidth: out.borderTop.width = source.borderTop.width; break;
	case CssPropBorderTopColor: out.borderTop.color = source.borderTop.color; break;
	case CssPropBorderTopStyle: out.borderTop.style = source.borderTop.style; break;
	case CssPropBorderRightWidth: out.borderRight.width = source.borderRight.width; break;
	case CssPropBorderRightColor: out.borderRight.color = source.borderRight.color; break;
	case CssPropBorderRightStyle: out.borderRight.style = source.borderRight.style; break;
	case CssPropBorderBottomWidth: out.borderBottom.width = source.borderBottom.width; break;
	case CssPropBorderBottomColor: out.borderBottom.color = source.borderBottom.color; break;
	case CssPropBorderBottomStyle: out.borderBottom.style = source.borderBottom.style; break;
	case CssPropBorderLeftWidth: out.borderLeft.width = source.borderLeft.width; break;
	case CssPropBorderLeftColor: out.borderLeft.color = source.borderLeft.color; break;
	case CssPropBorderLeftStyle: out.borderLeft.style = source.borderLeft.style; break;
	case CssPropBorderRadius: out.borderRadius = source.borderRadius; break;
	case CssPropBoxShadow:
		out.shadowX = source.shadowX;
		out.shadowY = source.shadowY;
		out.shadowBlur = source.shadowBlur;
		out.shadowSpread = source.shadowSpread;
		out.shadowColor = source.shadowColor;
		out.boxShadowSet = source.boxShadowSet;
		break;
	case CssPropRowGap: out.rowGap = source.rowGap; break;
	case CssPropColumnGap: out.columnGap = source.columnGap; break;
	}
	out.declared |= property;
}

static const uint64_t kCssProperties[] =
{
	CssPropColor, CssPropBackground, CssPropFontSize, CssPropBold, CssPropItalic,
	CssPropUnderline, CssPropAlign, CssPropVerticalAlign, CssPropLetterSpacing,
	CssPropMarginTop, CssPropMarginRight,
	CssPropWhiteSpace, CssPropTextTransform,
	CssPropMarginBottom, CssPropMarginLeft, CssPropPaddingTop,
	CssPropPaddingRight, CssPropPaddingBottom, CssPropPaddingLeft,
	CssPropWidth, CssPropMinWidth, CssPropMaxWidth, CssPropBoxSizing,
	CssPropHeight, CssPropMinHeight, CssPropMaxHeight,
	CssPropDisplay, CssPropMono, CssPropFlex, CssPropFlexRow,
	CssPropFlexWrap, CssPropJustify, CssPropFlexGrow,
	CssPropOverflowX, CssPropOverflowY, CssPropPosition, CssPropLeft,
	CssPropRight, CssPropTop, CssPropBottom,
	CssPropMarginLeftAuto, CssPropMarginRightAuto,
	CssPropListStyle, CssPropBorder, CssPropBorderTopWidth,
	CssPropBorderTopColor, CssPropBorderTopStyle, CssPropBorderRightWidth,
	CssPropBorderRightColor, CssPropBorderRightStyle, CssPropBorderBottomWidth,
	CssPropBorderBottomColor, CssPropBorderBottomStyle, CssPropBorderLeftWidth,
	CssPropBorderLeftColor, CssPropBorderLeftStyle, CssPropRowGap, CssPropColumnGap,
	CssPropLineHeight, CssPropBorderRadius, CssPropBoxShadow,
	CssPropGridColumns, CssPropGridColumnSpan, CssPropGrid
};

static std::vector<std::string> splitCssTopLevel(const std::string& text, char delimiter)
{
	std::vector<std::string> parts;
	size_t start = 0;
	int depth = 0;
	char quote = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		const char ch = text[i];
		if (quote)
		{
			if (ch == '\\' && i + 1 < text.size()) { ++i; continue; }
			if (ch == quote) quote = 0;
			continue;
		}
		if (ch == '\'' || ch == '"') quote = ch;
		else if (ch == '(') ++depth;
		else if (ch == ')' && depth > 0) --depth;
		else if (ch == delimiter && depth == 0)
		{
			parts.push_back(strTrim(text.substr(start, i - start)));
			start = i + 1;
		}
	}
	parts.push_back(strTrim(text.substr(start)));
	return parts;
}

static bool stripImportant(std::string& value)
{
	const std::string lower = toLower(value);
	const size_t pos = lower.rfind("!important");
	if (pos == std::string::npos) return false;
	value = strTrim(value.substr(0, pos));
	return true;
}

static void parseDeclaration(const std::string& prop, const std::string& value, Style& out)
{
	std::string cleaned = strTrim(value);
	const bool important = stripImportant(cleaned);
	const std::string property = strTrim(prop);
	if (property.size() > 2 && property[0] == '-' && property[1] == '-')
	{
		for (CssCustomProperty& existing : out.customProperties)
			if (existing.name == property)
			{
				if (!existing.important || important)
				{
					existing.value = cleaned;
					existing.important = important;
				}
				return;
			}
		out.customProperties.push_back({ property, cleaned, important });
		return;
	}

	const std::string normalizedProperty = toLower(property);
	if (normalizedProperty == "object-fit")
	{
		CssImageFit fit;
		if (parseImageFit(cleaned, fit) &&
			(!out.imageFitSet || !out.imageFitImportant || important))
		{
			out.imageFit = fit;
			out.imageFitSet = true;
			out.imageFitImportant = important;
		}
		return;
	}
	if (normalizedProperty == "object-position")
	{
		int x = 50, y = 50;
		if (parseImagePosition(cleaned, x, y) &&
			(!out.imagePositionSet || !out.imagePositionImportant || important))
		{
			out.imagePositionX = x;
			out.imagePositionY = y;
			out.imagePositionSet = true;
			out.imagePositionImportant = important;
		}
		return;
	}

	Style parsed;
	parseDeclarationValues(property, cleaned, parsed);
	for (uint64_t property : kCssProperties)
	{
		if (!(parsed.declared & property)) continue;
		if ((out.important & property) && !important) continue;
		applyProperty(out, parsed, property);
		if (important) out.important |= property;
		else out.important &= ~((uint64_t)property);
	}
}

//-----------------------------------------------------------------------------
// Parse de declarations inline "a: b; c: d"
//-----------------------------------------------------------------------------
void parseInlineStyle(const std::string& decls, Style& out)
{
	for (const std::string& part : splitCssTopLevel(decls, ';'))
	{
		size_t colon = part.find(':');
		if (colon != std::string::npos)
			parseDeclaration(part.substr(0, colon), part.substr(colon + 1), out);
	}
}

static void parseCssDeclarations(const std::string& text,
	std::vector<CssDeclaration>& declarations)
{
	for (const std::string& part : splitCssTopLevel(text, ';'))
	{
		const size_t colon = part.find(':');
		if (colon == std::string::npos) continue;
		const std::string property = strTrim(part.substr(0, colon));
		if (property.empty()) continue;
		declarations.push_back({ property, strTrim(part.substr(colon + 1)) });
	}
}

static const CssCustomProperty* findCustomProperty(
	const std::vector<CssCustomProperty>& properties, const std::string& name)
{
	for (const CssCustomProperty& property : properties)
		if (property.name == name) return &property;
	return nullptr;
}

static bool substituteCssVariables(const std::string& input,
	const std::vector<CssCustomProperty>& properties, std::string& output,
	int depth = 0)
{
	if (depth >= 8) return false;
	const std::string lower = toLower(input);
	size_t cursor = 0;
	output.clear();
	while (cursor < input.size())
	{
		const size_t start = lower.find("var(", cursor);
		if (start == std::string::npos)
		{
			output.append(input, cursor, std::string::npos);
			return true;
		}

		output.append(input, cursor, start - cursor);
		size_t end = start + 4;
		int parentheses = 1;
		char quote = 0;
		for (; end < input.size() && parentheses > 0; ++end)
		{
			const char ch = input[end];
			if (quote)
			{
				if (ch == '\\' && end + 1 < input.size()) { ++end; continue; }
				if (ch == quote) quote = 0;
			}
			else if (ch == '\'' || ch == '"') quote = ch;
			else if (ch == '(') ++parentheses;
			else if (ch == ')') --parentheses;
		}
		if (parentheses != 0) return false;

		const std::string expression = input.substr(start + 4, end - start - 5);
		const std::vector<std::string> arguments = splitCssTopLevel(expression, ',');
		if (arguments.empty()) return false;
		const std::string name = strTrim(arguments[0]);
		const CssCustomProperty* variable = findCustomProperty(properties, name);
		std::string replacement;
		const bool resolved = variable &&
			substituteCssVariables(variable->value, properties, replacement, depth + 1);
		if (!resolved && arguments.size() > 1)
		{
			std::string fallback = arguments[1];
			for (size_t i = 2; i < arguments.size(); ++i)
				fallback += ", " + arguments[i];
			if (!substituteCssVariables(fallback, properties, replacement, depth + 1))
				return false;
		}
		else if (!resolved) return false;
		output += replacement;
		cursor = end;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Parsing et correspondance des selecteurs avances
//-----------------------------------------------------------------------------
static bool parseNthChildArgument(const std::string& argument, int& a, int& b)
{
	std::string value = toLower(strTrim(argument));
	value.erase(std::remove_if(value.begin(), value.end(),
		[](unsigned char c) { return isspace(c) != 0; }), value.end());
	if (value == "odd") { a = 2; b = 1; return true; }
	if (value == "even") { a = 2; b = 0; return true; }

	const size_t n = value.find('n');
	if (n == std::string::npos)
	{
		if (value.empty()) return false;
		char* end = nullptr;
		long number = strtol(value.c_str(), &end, 10);
		if (!end || *end != '\0' || number < 1 || number > 1000000)
			return false;
		a = 0;
		b = (int)number;
		return true;
	}
	if (value.find('n', n + 1) != std::string::npos) return false;

	const std::string coefficient = value.substr(0, n);
	if (coefficient.empty() || coefficient == "+") a = 1;
	else if (coefficient == "-") a = -1;
	else
	{
		char* end = nullptr;
		long number = strtol(coefficient.c_str(), &end, 10);
		if (!end || *end != '\0' || number < -1000000 || number > 1000000)
			return false;
		a = (int)number;
	}

	const std::string offset = value.substr(n + 1);
	if (offset.empty()) b = 0;
	else
	{
		char* end = nullptr;
		long number = strtol(offset.c_str(), &end, 10);
		if (!end || *end != '\0' || number < -1000000 || number > 1000000)
			return false;
		b = (int)number;
	}
	return a != 0 || b >= 1;
}

static void parseCompound(const std::string& token, CssCompoundSelector& comp, int& spec)
{
	size_t i = 0;
	while (i < token.size())
	{
		char c = token[i];
		if (c == '.')
		{
			size_t j = i + 1;
			std::string cls;
			while (j < token.size() && (isalnum((unsigned char)token[j]) || token[j] == '-' || token[j] == '_'))
				cls += token[j++];
			if (!cls.empty())
			{
				comp.classes.push_back(cls);
				spec += 10;
			}
			i = j;
		}
		else if (c == '#')
		{
			size_t j = i + 1;
			std::string id;
			while (j < token.size() && (isalnum((unsigned char)token[j]) || token[j] == '-' || token[j] == '_'))
				id += token[j++];
			if (!id.empty())
			{
				comp.id = id;
				spec += 100;
			}
			i = j;
		}
		else if (c == '[')
		{
			size_t close = token.find(']', i);
			if (close != std::string::npos)
			{
				std::string inner = strTrim(token.substr(i + 1, close - i - 1));
				CssAttrSelector a;
				size_t op = inner.find_first_of("~|^$*=");
				if (op != std::string::npos)
				{
					a.name = toLower(strTrim(inner.substr(0, op)));
					a.operation = inner[op];
					size_t valueStart = op + 1;
					if (a.operation != '=' && valueStart < inner.size() &&
						inner[valueStart] == '=')
						++valueStart;
					std::string val = strTrim(inner.substr(valueStart));
					if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
					                        (val.front() == '\'' && val.back() == '\'')))
						val = val.substr(1, val.size() - 2);
					a.value = val;
					a.hasValue = true;
				}
				else
				{
					a.name = toLower(inner);
					a.hasValue = false;
				}
				if (!a.name.empty())
				{
					comp.attrs.push_back(a);
					spec += 10;
				}
				i = close + 1;
			}
			else i++;
		}
		else if (c == ':')
		{
			// Pseudo-classes / pseudo-elements
			size_t j = i + 1;
			// double-colon ::before ::after etc.
			if (j < token.size() && token[j] == ':') j++;
			while (j < token.size() && (isalnum((unsigned char)token[j]) || token[j] == '-'))
				j++;
			std::string pseudo = toLower(token.substr(i, j - i));
			std::string pseudoArgument;
			if (j < token.size() && token[j] == '(')
			{
				const size_t argumentStart = j + 1;
				size_t depth = 1;
				j++;
				while (j < token.size() && depth > 0)
				{
					if (token[j] == '(') depth++;
					else if (token[j] == ')') depth--;
					j++;
				}
				if (depth == 0) pseudoArgument = strTrim(token.substr(argumentStart, j - argumentStart - 1));
			}
			if (pseudo == ":first-child")
			{
				comp.firstChild = true;
				spec += 10;
			}
			else if (pseudo == ":last-child")
			{
				comp.lastChild = true;
				spec += 10;
			}
			else if (pseudo == ":only-child")
			{
				comp.onlyChild = true;
				spec += 10;
			}
			else if (pseudo == ":nth-child")
			{
				if (parseNthChildArgument(pseudoArgument, comp.nthChildA,
					comp.nthChildB))
				{
					comp.nthChildSet = true;
					spec += 10;
				}
				else comp.unsupported = true;
			}
			else if (pseudo == ":nth-of-type")
			{
				if (parseNthChildArgument(pseudoArgument, comp.nthOfTypeA,
					comp.nthOfTypeB))
				{
					comp.nthOfTypeSet = true;
					spec += 10;
				}
				else comp.unsupported = true;
			}
			else if (pseudo == ":hover") { comp.hover = true; spec += 10; }
			else if (pseudo == ":focus") { comp.focus = true; spec += 10; }
			else if (pseudo == ":root") { comp.root = true; spec += 10; }
			else if (pseudo == ":checked") { comp.checked = true; spec += 10; }
			else if (pseudo == ":disabled") { comp.disabled = true; spec += 10; }
			else if (pseudo == ":not" && !pseudoArgument.empty() && pseudoArgument.find_first_of(" >+~") == std::string::npos)
			{ comp.notSelector = pseudoArgument; spec += 10; }
			else
			{
				// :hover, :focus, :not(), ::before, ::after, etc.
				// On marque la regle comme non supportee : elle ne doit pas s'appliquer.
				comp.unsupported = true;
			}
			i = j;
		}
		else if (c == '*')
		{
			comp.tag = "*";
			i++;
		}
		else if (isalnum((unsigned char)c) || c == '-')
		{
			size_t j = i;
			std::string tag;
			while (j < token.size() && (isalnum((unsigned char)token[j]) || token[j] == '-'))
				tag += (char)(token[j] >= 'A' && token[j] <= 'Z' ? token[j] + 32 : token[j]), j++;
			comp.tag = tag;
			spec += 1;
			i = j;
		}
		else i++;
	}
}

static bool matchCompound(const Document& doc, int nodeIdx, const CssCompoundSelector& sel);
static bool matchCompound(const Document& doc, int nodeIdx, const CssCompoundSelector& sel)
{
	if (nodeIdx < 0 || (size_t)nodeIdx >= doc.nodes.size()) return false;
	const Node& node = doc.nodes[nodeIdx];
	if (!node.isElement()) return false;

	if (!sel.tag.empty() && sel.tag != "*" && sel.tag != node.tag)
		return false;

	if (!sel.id.empty())
	{
		const char* nid = node.getAttr("id");
		if (!nid || std::string(nid) != sel.id) return false;
	}
	if (sel.hover)
	{
		bool hovered = false;
		for (int current = doc.hoveredNode; current >= 0 && (size_t)current < doc.nodes.size(); current = doc.nodes[(size_t)current].parent)
			if (current == nodeIdx) { hovered = true; break; }
		if (!hovered) return false;
	}
	if (sel.focus && doc.focusedNode != nodeIdx) return false;
	if (sel.root && node.tag != "html") return false;
	if (sel.checked && !node.hasAttr("checked")) return false;
	if (sel.disabled && !node.hasAttr("disabled")) return false;
	if (!sel.notSelector.empty())
	{
		CssCompoundSelector excluded; int specificity = 0;
		parseCompound(sel.notSelector, excluded, specificity);
		if (matchCompound(doc, nodeIdx, excluded)) return false;
	}

	if (!sel.classes.empty())
	{
		const char* ncls = node.getAttr("class");
		if (!ncls) return false;
		std::string all = ncls;
		for (const auto& reqCls : sel.classes)
		{
			bool found = false;
			size_t p = 0;
			while (p <= all.size())
			{
				size_t sp = all.find_first_of(" \t\r\n", p);
				std::string one = all.substr(p, (sp == std::string::npos) ? std::string::npos : sp - p);
				if (one == reqCls) { found = true; break; }
				if (sp == std::string::npos) break;
				p = sp + 1;
			}
			if (!found) return false;
		}
	}

	for (const auto& a : sel.attrs)
	{
		const char* val = node.getAttr(a.name.c_str());
		if (!val) return false;
		if (!a.hasValue) continue;
		const std::string actual(val);
		bool matches = false;
		switch (a.operation)
		{
		case '=': matches = actual == a.value; break;
		case '^': matches = !a.value.empty() &&
			actual.size() >= a.value.size() &&
			actual.compare(0, a.value.size(), a.value) == 0; break;
		case '$': matches = !a.value.empty() &&
			actual.size() >= a.value.size() &&
			actual.compare(actual.size() - a.value.size(), a.value.size(),
				a.value) == 0; break;
		case '*': matches = !a.value.empty() &&
			actual.find(a.value) != std::string::npos; break;
		case '~':
		{
			size_t start = 0;
			while (start < actual.size())
			{
				while (start < actual.size() &&
					isspace((unsigned char)actual[start])) ++start;
				const size_t end = actual.find_first_of(" \t\r\n", start);
				if (actual.substr(start, end == std::string::npos
					? std::string::npos : end - start) == a.value)
				{
					matches = true;
					break;
				}
				if (end == std::string::npos) break;
				start = end + 1;
			}
			break;
		}
		case '|': matches = actual == a.value ||
			(actual.size() > a.value.size() &&
			 actual.compare(0, a.value.size(), a.value) == 0 &&
			 actual[a.value.size()] == '-'); break;
		default: break;
		}
		if (!matches) return false;
	}

	if (sel.firstChild || sel.lastChild || sel.onlyChild || sel.nthChildSet ||
		sel.nthOfTypeSet)
	{
		int parent = node.parent;
		if (parent < 0 || (size_t)parent >= doc.nodes.size())
			return false;
		const Node& pNode = doc.nodes[parent];
		int elementIndex = 0;
		int elementCount = 0;
		int typeIndex = 0;
		int typeCount = 0;
		for (int child : pNode.children)
		{
			if (child < 0 || (size_t)child >= doc.nodes.size() ||
				!doc.nodes[child].isElement())
				continue;
			++elementCount;
			if (child == nodeIdx)
				elementIndex = elementCount;
			if (doc.nodes[child].tag == node.tag)
			{
				++typeCount;
				if (child == nodeIdx) typeIndex = typeCount;
			}
		}
		if (elementIndex == 0) return false;
		if (sel.firstChild && elementIndex != 1) return false;
		if (sel.lastChild && elementIndex != elementCount) return false;
		if (sel.onlyChild && elementCount != 1) return false;
		if (sel.nthChildSet)
		{
			const int difference = elementIndex - sel.nthChildB;
			if (sel.nthChildA == 0)
			{
				if (difference != 0) return false;
			}
			else if (difference % sel.nthChildA != 0 ||
				difference / sel.nthChildA < 0)
				return false;
		}
		if (sel.nthOfTypeSet)
		{
			const int difference = typeIndex - sel.nthOfTypeB;
			if (sel.nthOfTypeA == 0)
			{
				if (difference != 0) return false;
			}
			else if (difference % sel.nthOfTypeA != 0 ||
				difference / sel.nthOfTypeA < 0)
				return false;
		}
	}

	return true;
}

static bool matchRule(const Document& doc, int nodeIdx, const CssRule& rule)
{
	if (rule.selectorChain.empty())
	{
		if (!rule.tag.empty() && rule.tag != "*" && rule.tag != doc.nodes[nodeIdx].tag) return false;
		if (!rule.id.empty())
		{
			const char* id = doc.nodes[nodeIdx].getAttr("id");
			if (!id || std::string(id) != rule.id) return false;
		}
		if (!rule.cls.empty())
		{
			const char* cls = doc.nodes[nodeIdx].getAttr("class");
			if (!cls) return false;
			std::string all = cls;
			size_t p = 0;
			bool found = false;
			while (p <= all.size())
			{
				size_t sp = all.find_first_of(" \t", p);
				std::string one = all.substr(p, (sp == std::string::npos) ? std::string::npos : sp - p);
				if (one == rule.cls) { found = true; break; }
				if (sp == std::string::npos) break;
				p = sp + 1;
			}
			if (!found) return false;
		}
		return true;
	}

	if (!matchCompound(doc, nodeIdx, rule.selectorChain[0]))
		return false;

	int cur = nodeIdx;
	for (size_t i = 0; i + 1 < rule.selectorChain.size(); i++)
	{
		char comb = rule.selectorChain[i].combinator;
		const auto& nextSel = rule.selectorChain[i + 1];
		if (comb == '+' || comb == '~')
		{
			const int parent = doc.nodes[cur].parent;
			if (parent < 0 || (size_t)parent >= doc.nodes.size()) return false;
			const std::vector<int>& siblings = doc.nodes[parent].children;
			size_t position = siblings.size();
			for (size_t sibling = 0; sibling < siblings.size(); ++sibling)
				if (siblings[sibling] == cur) { position = sibling; break; }
			if (position == siblings.size()) return false;
			bool found = false;
			while (position > 0)
			{
				const int sibling = siblings[--position];
				if (sibling < 0 || (size_t)sibling >= doc.nodes.size() ||
					!doc.nodes[sibling].isElement())
					continue;
				if (matchCompound(doc, sibling, nextSel))
				{
					cur = sibling;
					found = true;
				}
				if (found || comb == '+') break;
			}
			if (!found) return false;
		}
		else if (comb == '>')
		{
			cur = doc.nodes[cur].parent;
			if (cur < 0 || !matchCompound(doc, cur, nextSel)) return false;
		}
		else // descendant ' '
		{
			cur = doc.nodes[cur].parent;
			bool found = false;
			while (cur >= 0)
			{
				if (matchCompound(doc, cur, nextSel)) { found = true; break; }
				cur = doc.nodes[cur].parent;
			}
			if (!found) return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Parse d'une regle "selector { ... }"
//-----------------------------------------------------------------------------
void CssEngine::parseRule(const std::string& text)
{
	size_t brace = text.find('{');
	if (brace == std::string::npos) return;
	std::string selector = strTrim(text.substr(0, brace));
	std::string body = text.substr(brace + 1);
	size_t endBrace = body.rfind('}');
	if (endBrace != std::string::npos) body = body.substr(0, endBrace);

	if (selector.empty()) return;
	// Decoupe en composants selon les combinateurs (' ' ou '>')
	struct RawToken { std::string text; char comb; };
	std::vector<RawToken> rawTokens;
	size_t pos = 0;
	while (pos < selector.size())
	{
		while (pos < selector.size() && (selector[pos] == ' ' || selector[pos] == '\t')) pos++;
		if (pos >= selector.size()) break;

		char comb = 0;
		if (selector[pos] == '>' || selector[pos] == '+' ||
			selector[pos] == '~')
		{
			comb = selector[pos];
			pos++;
			while (pos < selector.size() && (selector[pos] == ' ' || selector[pos] == '\t')) pos++;
		}
		else if (!rawTokens.empty())
		{
			comb = ' ';
		}

		size_t next = pos;
		int parentheses = 0;
		int brackets = 0;
		while (next < selector.size())
		{
			const char c = selector[next];
			if (c == '(') ++parentheses;
			else if (c == ')' && parentheses > 0) --parentheses;
			else if (c == '[') ++brackets;
			else if (c == ']' && brackets > 0) --brackets;
			else if (parentheses == 0 && brackets == 0 &&
				(c == ' ' || c == '\t' || c == '>' || c == '+' || c == '~'))
				break;
			++next;
		}
		std::string tok = selector.substr(pos, next - pos);
		if (!tok.empty())
			rawTokens.push_back({ tok, comb });
		pos = next;
	}

	if (rawTokens.empty()) return;

	CssRule rule;
	rule.order = sourceOrder_ >= 0
		? sourceOrder_ * ((int)MaxRules + 1) + sourceRuleOrder_++
		: (int)rules_.size();

	// Construire la chaine de droite a gauche (la cible en premier index 0)
	for (int k = (int)rawTokens.size() - 1; k >= 0; k--)
	{
		CssCompoundSelector comp;
		parseCompound(rawTokens[k].text, comp, rule.specificity);
		if (comp.unsupported) return;
		comp.combinator = (k > 0) ? rawTokens[k].comb : 0;
		rule.selectorChain.push_back(comp);
	}

	if (rule.selectorChain.empty()) return;
	const auto& target = rule.selectorChain[0];
	rule.tag = target.tag;
	if (!target.classes.empty()) rule.cls = target.classes[0];
	rule.id = target.id;

	if (rule.tag.empty() && rule.cls.empty() && rule.id.empty() &&
		target.attrs.empty() && !target.firstChild && !target.lastChild &&
		!target.onlyChild && !target.nthChildSet &&
		!target.nthOfTypeSet && !target.root && !target.hover &&
		!target.focus && !target.checked && !target.disabled &&
		target.notSelector.empty())
		return;

	parseCssDeclarations(body, rule.declarations);
	for (const CssDeclaration& declaration : rule.declarations)
		parseDeclaration(declaration.property, declaration.value, rule.style);
	const size_t ruleIndex = rules_.size();
	rules_.push_back(rule);
	if (!target.id.empty())
		rulesById_[target.id].push_back(ruleIndex);
	else if (!target.classes.empty())
		rulesByClass_[target.classes[0]].push_back(ruleIndex);
	else if (!target.tag.empty() && target.tag != "*")
		rulesByTag_[target.tag].push_back(ruleIndex);
	else if (target.tag == "*" && target.attrs.empty() &&
		!target.firstChild && !target.lastChild && !target.onlyChild &&
		!target.nthChildSet && !target.nthOfTypeSet && !target.root &&
		!target.hover && !target.focus && !target.checked &&
		!target.disabled && target.notSelector.empty())
		universalRules_.push_back(ruleIndex);
	else
		fallbackRules_.push_back(ruleIndex);
}

static bool mediaDimension(const std::string& value, float& pixels)
{
	char* end = nullptr;
	const float number = strtof(value.c_str(), &end);
	if (end == value.c_str() || !isfinite(number) || number < 0.0f)
		return false;
	const std::string unit = toLower(strTrim(std::string(end)));
	pixels = number;
	if (unit == "rem" || unit == "em") pixels *= 16.0f;
	else if (unit == "pt") pixels *= 4.0f / 3.0f;
	else if (unit == "vw")
		pixels *= (float)gCssViewportWidth / 100.0f;
	else if (unit == "vh")
		pixels *= (float)cssViewportHeight() / 100.0f;
	else if (unit != "px" && !(unit.empty() && number == 0.0f))
		return false;
	return true;
}

static bool splitMediaAnd(const std::string& query,
	std::vector<std::string>& clauses)
{
	size_t start = 0;
	int parentheses = 0;
	for (size_t i = 0; i < query.size(); ++i)
	{
		if (query[i] == '(')
			++parentheses;
		else if (query[i] == ')')
		{
			if (--parentheses < 0) return false;
		}
		else if (parentheses == 0 && query.compare(i, 3, "and") == 0 &&
			(i == 0 || isspace((unsigned char)query[i - 1])) &&
			(i + 3 == query.size() ||
				isspace((unsigned char)query[i + 3])))
		{
			clauses.push_back(strTrim(query.substr(start, i - start)));
			start = i + 3;
			i += 2;
		}
	}
	if (parentheses != 0) return false;
	clauses.push_back(strTrim(query.substr(start)));
	return std::all_of(clauses.begin(), clauses.end(),
		[](const std::string& clause) { return !clause.empty(); });
}

static bool evalMediaFeature(const std::string& clause, bool& matches)
{
	if (clause.size() < 2 || clause.front() != '(' || clause.back() != ')')
		return false;
	const std::string feature = strTrim(clause.substr(1,
		clause.size() - 2));
	const size_t colon = feature.find(':');
	if (colon == std::string::npos) return false;
	const std::string name = strTrim(feature.substr(0, colon));
	const std::string value = strTrim(feature.substr(colon + 1));
	if (name == "orientation")
	{
		if (value != "portrait" && value != "landscape") return false;
		matches = value == "landscape"
			? gCssViewportWidth > cssViewportHeight()
			: gCssViewportWidth <= cssViewportHeight();
		return true;
	}
	if (name != "min-width" && name != "max-width" &&
		name != "min-height" && name != "max-height")
		return false;
	float limit = 0.0f;
	if (!mediaDimension(value, limit)) return false;
	const float dimension = name.find("width") != std::string::npos
		? (float)gCssViewportWidth : (float)cssViewportHeight();
	if (name == "min-width" || name == "min-height")
		matches = dimension >= limit;
	else
		matches = dimension <= limit;
	return true;
}

static bool evalMediaQuerySingle(const std::string& query)
{
	std::string q = toLower(strTrim(query));
	bool negate = false;
	if (strStartsWith(q, "not ") || strStartsWith(q, "only "))
	{
		negate = strStartsWith(q, "not ");
		q = strTrim(q.substr(q.find(' ') + 1));
	}
	std::vector<std::string> clauses;
	if (!splitMediaAnd(q, clauses)) return false;
	bool matches = true;
	size_t firstFeature = 0;
	if (clauses[0].front() != '(')
	{
		if (clauses[0] == "print")
			matches = false;
		else if (clauses[0] != "screen" && clauses[0] != "all")
			matches = false;
		firstFeature = 1;
	}
	for (size_t i = firstFeature; i < clauses.size(); ++i)
	{
		bool featureMatches = false;
		if (!evalMediaFeature(clauses[i], featureMatches))
			featureMatches = false;
		matches = matches && featureMatches;
	}
	return negate ? !matches : matches;
}

static bool evalMediaQuery(const std::string& query)
{
	size_t start = 0;
	int parentheses = 0;
	for (size_t i = 0; i <= query.size(); ++i)
	{
		if (i < query.size())
		{
			if (query[i] == '(') ++parentheses;
			else if (query[i] == ')' && parentheses > 0) --parentheses;
		}
		if (i != query.size() && (query[i] != ',' || parentheses != 0))
			continue;
		if (evalMediaQuerySingle(strTrim(query.substr(start, i - start))))
			return true;
		start = i + 1;
	}
	return false;
}

static bool splitSupportsOperator(const std::string& condition,
	const char* operation, std::vector<std::string>& parts)
{
	const size_t operationLength = strlen(operation);
	size_t start = 0;
	int parentheses = 0;
	for (size_t i = 0; i < condition.size(); ++i)
	{
		if (condition[i] == '(')
			++parentheses;
		else if (condition[i] == ')')
		{
			if (--parentheses < 0) return false;
		}
		else if (parentheses == 0 &&
			condition.compare(i, operationLength, operation) == 0 &&
			(i == 0 || isspace((unsigned char)condition[i - 1])) &&
			(i + operationLength == condition.size() ||
				isspace((unsigned char)condition[i + operationLength])))
		{
			parts.push_back(strTrim(condition.substr(start, i - start)));
			start = i + operationLength;
			i += operationLength - 1;
		}
	}
	if (parentheses != 0) return false;
	if (parts.empty()) return true;
	parts.push_back(strTrim(condition.substr(start)));
	return std::all_of(parts.begin(), parts.end(),
		[](const std::string& part) { return !part.empty(); });
}

static bool unwrapSupportsCondition(std::string& condition)
{
	if (condition.empty() || condition.front() != '(') return false;
	int parentheses = 0;
	for (size_t i = 0; i < condition.size(); ++i)
	{
		if (condition[i] == '(')
			++parentheses;
		else if (condition[i] == ')')
		{
			if (--parentheses < 0) return false;
			if (parentheses == 0 && i + 1 < condition.size())
				return false;
		}
	}
	if (parentheses != 0) return false;
	condition = strTrim(condition.substr(1, condition.size() - 2));
	return !condition.empty();
}

static bool evalSupportsCondition(const std::string& input, int depth = 0)
{
	if (depth >= 16) return false;
	std::string condition = toLower(strTrim(input));
	if (condition.empty()) return false;
	if (condition.front() == '(')
	{
		std::string unwrapped = condition;
		if (unwrapSupportsCondition(unwrapped))
		{
			const size_t colon = unwrapped.find(':');
			if (colon != std::string::npos &&
				unwrapped.find_first_of("()") == std::string::npos)
			{
				Style parsed;
				parseDeclaration(strTrim(unwrapped.substr(0, colon)),
					strTrim(unwrapped.substr(colon + 1)), parsed);
				return parsed.declared != 0 || parsed.imageFitSet ||
					parsed.imagePositionSet;
			}
			return evalSupportsCondition(unwrapped, depth + 1);
		}
	}
	if (strStartsWith(condition, "not "))
	{
		const std::string operand = strTrim(condition.substr(4));
		if (operand.empty()) return false;
		const bool result = evalSupportsCondition(operand, depth + 1);
		return !result;
	}

	std::vector<std::string> parts;
	if (!splitSupportsOperator(condition, "or", parts)) return false;
	if (!parts.empty())
	{
		for (const std::string& part : parts)
			if (evalSupportsCondition(part, depth + 1)) return true;
		return false;
	}
	if (!splitSupportsOperator(condition, "and", parts)) return false;
	if (!parts.empty())
	{
		for (const std::string& part : parts)
			if (!evalSupportsCondition(part, depth + 1)) return false;
		return true;
	}
	return false;
}

void CssEngine::setViewportWidth(int width)
{
	viewportWidth_ = width >= NW_PAGE_WIDTH && width <= 640 ? width : NW_PAGE_WIDTH;
	gCssViewportWidth = viewportWidth_;
}

void CssEngine::parseStylesheet(const std::string& css, int sourceOrder)
{
	gCssViewportWidth = viewportWidth_;
	const int previousSourceOrder = sourceOrder_;
	const int previousSourceRuleOrder = sourceRuleOrder_;
	if (sourceOrder >= 0)
	{
		sourceOrder_ = sourceOrder;
		sourceRuleOrder_ = 0;
	}
	if (stylesheetBytes_ >= MaxStylesheetBytes)
	{
		sourceOrder_ = previousSourceOrder;
		sourceRuleOrder_ = previousSourceRuleOrder;
		return;
	}
	const size_t bytesToParse = (css.size() < MaxStylesheetBytes - stylesheetBytes_)
		? css.size() : MaxStylesheetBytes - stylesheetBytes_;
	stylesheetBytes_ += bytesToParse;

	// Supprime les commentaires /* ... */ sans copier toute la feuille.
	std::string clean;
	clean.reserve(bytesToParse);
	size_t i = 0;
	while (i < bytesToParse)
	{
		if (css.compare(i, 2, "/*") == 0)
		{
			size_t e = css.find("*/", i + 2);
			if (e == std::string::npos || e + 2 > bytesToParse) break;
			i = e + 2;
		}
		else clean += css[i++];
	}

	size_t pos = 0;
	while (pos < clean.size() && rules_.size() < MaxRules)
	{
		// Les at-rules simples terminent par un point-virgule et n'ont pas de
		// bloc. Les ignorer avant de rechercher le prochain bloc de regles.
		while (pos < clean.size())
		{
			while (pos < clean.size() && isspace((unsigned char)clean[pos])) ++pos;
			if (pos >= clean.size() || clean[pos] != '@') break;
			size_t semi = clean.find(';', pos);
			size_t braceAt = clean.find('{', pos);
			if (semi != std::string::npos && (braceAt == std::string::npos || semi < braceAt))
				pos = semi + 1;
			else break;
		}
		if (pos >= clean.size()) break;
		size_t brace = clean.find('{', pos);
		if (brace == std::string::npos) break;
		const std::string selector = strTrim(clean.substr(pos, brace - pos));
		size_t endBrace = brace + 1;
		int depth = 1;
		while (endBrace < clean.size() && depth > 0)
		{
			if (clean[endBrace] == '{') ++depth;
			else if (clean[endBrace] == '}') --depth;
			++endBrace;
		}
		if (depth != 0) break;

		const std::string body = clean.substr(brace + 1, endBrace - brace - 2);

		if (strStartsWith(selector, "@media"))
		{
			if (evalMediaQuery(strTrim(selector.substr(6))))
			{
				// Evaluer le bloc imbrique avec la largeur courante (400 ou 640 px).
				parseStylesheet(body);
			}
		}
		else if (strStartsWith(selector, "@supports"))
		{
			if (evalSupportsCondition(strTrim(selector.substr(9))))
				parseStylesheet(body);
		}
		else if (strStartsWith(selector, "@layer"))
		{
			parseStylesheet(body);
		}
		else if (selector.empty() || selector[0] != '@')
		{
			size_t start = 0;
			while (start <= selector.size() && rules_.size() < MaxRules)
			{
				size_t comma = selector.find(',', start);
				const std::string single = strTrim(selector.substr(start,
					comma == std::string::npos ? std::string::npos : comma - start));
				if (!single.empty())
					parseRule(single + " {" + body + "}");
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
		}
		pos = endBrace;
	}
	sourceOrder_ = previousSourceOrder;
	sourceRuleOrder_ = previousSourceRuleOrder;
}

Style defaultStyle()
{
	Style s;
	return s;
}

//-----------------------------------------------------------------------------
// Collecte des blocs <style> d'un document.
//-----------------------------------------------------------------------------
void collectStyleBlocks(const Document& doc, CssEngine& css)
{
	if (doc.nodes.empty()) return;
	for (size_t i = 0; i < doc.nodes.size(); i++)
	{
		const Node& n = doc.nodes[i];
		if (n.isElement() && n.tag == "style")
		{
			std::string sheet;
			for (int c : n.children)
			{
				const Node& ch = doc.nodes[c];
				if (ch.type == NodeType::RawText || ch.type == NodeType::Text)
				{
					const std::string_view text = doc.nodeTextView(c);
					if (!text.empty()) sheet.append(text.data(), text.size());
				}
			}
			if (!sheet.empty()) css.parseStylesheet(sheet, (int)i);
		}
	}
}

//-----------------------------------------------------------------------------
// Resolution : applique les regles matchees avec calcul de specificite propre
//-----------------------------------------------------------------------------
Style CssEngine::resolve(const Document& doc, int nodeIdx, const Style& parent) const
{
	gCssViewportWidth = viewportWidth_;
	Style out;
	out.color = parent.color;
	out.colorSet = parent.colorSet;
	out.fontSize = parent.fontSize;
	out.bold = parent.bold;
	out.italic = parent.italic;
	out.whiteSpace = parent.whiteSpace;
	out.textTransform = parent.textTransform;
	out.letterSpacing = parent.letterSpacing;
	out.letterSpacingIsEm = parent.letterSpacingIsEm;
	out.align = parent.align;
	out.mono = parent.mono;
	out.lineHeight = parent.lineHeight;
	out.lineHeightRelative = parent.lineHeightRelative;
	out.lineHeightUnitless = parent.lineHeightUnitless;
	out.customProperties = parent.customProperties;
	if (nodeIdx < 0 || (size_t)nodeIdx >= doc.nodes.size()) return out;
	const Node& node = doc.nodes[nodeIdx];
	if (!node.isElement()) return out;

	struct CustomWinner
	{
		CssCustomProperty property;
		int specificity = -1;
		int order = -1;
		bool important = false;
	};
	std::vector<const CssRule*> matchingRules;
	std::vector<CustomWinner> customWinners;
	std::vector<CssDeclaration> inlineDeclarations;

	auto outranks = [](bool important, int specificity, int order,
		bool oldImportant, int oldSpecificity, int oldOrder)
	{
		return (important && !oldImportant) ||
			(important == oldImportant &&
			 (specificity > oldSpecificity ||
			  (specificity == oldSpecificity && order >= oldOrder)));
	};
	auto collectCustomProperties = [&](const std::vector<CssDeclaration>& declarations,
		int specificity, int order)
	{
		for (const CssDeclaration& declaration : declarations)
		{
			const std::string name = strTrim(declaration.property);
			if (name.size() <= 2 || name[0] != '-' || name[1] != '-') continue;
			std::string value = declaration.value;
			const bool important = stripImportant(value);
			auto existing = std::find_if(customWinners.begin(), customWinners.end(),
				[&](const CustomWinner& winner) { return winner.property.name == name; });
			if (existing == customWinners.end())
			{
				customWinners.push_back({ { name, value, important },
					specificity, order, important });
			}
			else if (outranks(important, specificity, order,
				existing->important, existing->specificity, existing->order))
			{
				existing->property.value = value;
				existing->property.important = important;
				existing->specificity = specificity;
				existing->order = order;
				existing->important = important;
			}
		}
	};

	std::vector<size_t> candidates;
	const Node& targetNode = doc.nodes[(size_t)nodeIdx];
	candidates.reserve(universalRules_.size() + fallbackRules_.size() + 8);
	candidates.insert(candidates.end(), universalRules_.begin(),
		universalRules_.end());
	candidates.insert(candidates.end(), fallbackRules_.begin(),
		fallbackRules_.end());
	auto appendIndexedRules = [&](const auto& index, const std::string& key)
	{
		const auto found = index.find(key);
		if (found != index.end())
			candidates.insert(candidates.end(), found->second.begin(),
				found->second.end());
	};
	appendIndexedRules(rulesByTag_, targetNode.tag);
	if (const char* id = targetNode.getAttr("id"))
		appendIndexedRules(rulesById_, id);
	if (const char* classList = targetNode.getAttr("class"))
	{
		const std::string classes(classList);
		size_t start = 0;
		while (start < classes.size())
		{
			while (start < classes.size() &&
				isspace((unsigned char)classes[start])) ++start;
			const size_t end = classes.find_first_of(" \t\r\n", start);
			if (start < classes.size())
				appendIndexedRules(rulesByClass_,
					classes.substr(start, end == std::string::npos
						? std::string::npos : end - start));
			if (end == std::string::npos) break;
			start = end + 1;
		}
	}
	std::sort(candidates.begin(), candidates.end());
	candidates.erase(std::unique(candidates.begin(), candidates.end()),
		candidates.end());
	for (size_t candidate : candidates)
	{
		const CssRule& rule = rules_[candidate];
		if (matchRule(doc, nodeIdx, rule))
		{
			matchingRules.push_back(&rule);
			collectCustomProperties(rule.declarations, rule.specificity, rule.order);
		}
	}

	if (const char* inlineStyle = node.getAttr("style"))
	{
		parseCssDeclarations(inlineStyle, inlineDeclarations);
		collectCustomProperties(inlineDeclarations, 1000000, 1000000);
	}

	for (const CustomWinner& winner : customWinners)
	{
		auto inherited = std::find_if(out.customProperties.begin(),
			out.customProperties.end(), [&](const CssCustomProperty& property)
			{ return property.name == winner.property.name; });
		if (inherited == out.customProperties.end())
			out.customProperties.push_back(winner.property);
		else
			*inherited = winner.property;
	}

	int bestSpec[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	int bestOrder[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	bool bestImportant[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
	{
		bestSpec[p] = -1;
		bestOrder[p] = -1;
		bestImportant[p] = false;
	}
	int bestImageFitSpec = -1, bestImageFitOrder = -1;
	bool bestImageFitImportant = false;
	int bestImagePositionSpec = -1, bestImagePositionOrder = -1;
	bool bestImagePositionImportant = false;

	auto applyDeclarations = [&](const std::vector<CssDeclaration>& declarations,
		int specificity, int order)
	{
		for (const CssDeclaration& declaration : declarations)
		{
			const std::string property = strTrim(declaration.property);
			if (property.size() > 2 && property[0] == '-' && property[1] == '-')
				continue;
			std::string value = declaration.value;
			const bool important = stripImportant(value);
			std::string substituted;
			if (!substituteCssVariables(value, out.customProperties, substituted))
				continue;
			if (important) substituted += " !important";
			Style parsed;
			parseDeclaration(property, substituted, parsed);
			for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
			{
				const uint64_t property = kCssProperties[p];
				if (parsed.declared & property)
				{
					const bool parsedImportant = (parsed.important & property) != 0;
					if (outranks(parsedImportant, specificity, order,
						bestImportant[p], bestSpec[p], bestOrder[p]))
					{
						bestSpec[p] = specificity;
						bestOrder[p] = order;
						bestImportant[p] = parsedImportant;
						applyProperty(out, parsed, property);
					}
				}
			}
			if (parsed.imageFitSet &&
				outranks(parsed.imageFitImportant, specificity, order,
					bestImageFitImportant, bestImageFitSpec, bestImageFitOrder))
			{
				out.imageFit = parsed.imageFit;
				out.imageFitSet = true;
				out.imageFitImportant = parsed.imageFitImportant;
				bestImageFitSpec = specificity;
				bestImageFitOrder = order;
				bestImageFitImportant = parsed.imageFitImportant;
			}
			if (parsed.imagePositionSet &&
				outranks(parsed.imagePositionImportant, specificity, order,
					bestImagePositionImportant, bestImagePositionSpec,
					bestImagePositionOrder))
			{
				out.imagePositionX = parsed.imagePositionX;
				out.imagePositionY = parsed.imagePositionY;
				out.imagePositionSet = true;
				out.imagePositionImportant = parsed.imagePositionImportant;
				bestImagePositionSpec = specificity;
				bestImagePositionOrder = order;
				bestImagePositionImportant = parsed.imagePositionImportant;
			}
		}
	};
	bool floatRight = false;
	int floatSpecificity = -1;
	int floatOrder = -1;
	bool floatImportant = false;
	auto applyFloatDeclarations = [&](const std::vector<CssDeclaration>& declarations,
		int specificity, int order)
	{
		for (const CssDeclaration& declaration : declarations)
		{
			if (toLower(strTrim(declaration.property)) != "float") continue;
			std::string value = declaration.value;
			const bool important = stripImportant(value);
			std::string substituted;
			if (!substituteCssVariables(value, out.customProperties,
				substituted))
				continue;
			value = toLower(strTrim(substituted));
			if (value != "right" && value != "left" && value != "none")
				continue;
			if (!outranks(important, specificity, order, floatImportant,
				floatSpecificity, floatOrder))
				continue;
			floatRight = value == "right";
			floatImportant = important;
			floatSpecificity = specificity;
			floatOrder = order;
		}
	};
	for (const CssRule* rule : matchingRules)
		applyFloatDeclarations(rule->declarations, rule->specificity,
			rule->order);
	applyFloatDeclarations(inlineDeclarations, 1000000, 1000000);
	for (const CssRule* rule : matchingRules)
		applyDeclarations(rule->declarations, rule->specificity, rule->order);
	applyDeclarations(inlineDeclarations, 1000000, 1000000);
	out.floatRight = floatRight;

	if (out.letterSpacingIsEm)
	{
		out.letterSpacing *= (float)(out.fontSize > 0 ? out.fontSize : 16);
		out.letterSpacingIsEm = false;
	}
	if (out.lineHeightRelative && !out.lineHeightUnitless)
	{
		const int fontSize = out.fontSize > 0 ? out.fontSize : 16;
		out.lineHeight = std::max(0, (fontSize * out.lineHeight + 500) / 1000);
		out.lineHeightRelative = false;
	}

	return out;
}

//-----------------------------------------------------------------------------
// Tests unitaires
//-----------------------------------------------------------------------------
int cssSelfTest()
{
	int fails = 0;
#define CSS_SELF_FAIL() do { fails++; printf("CSS self-test failure at line %d\n", __LINE__); } while (0)
	{
		uint32_t c;
		if (!cssParseColor("#ff0000", c) || (c & 0xFFFFFF) != 0x0000FF) CSS_SELF_FAIL();
		if (!cssParseColor("blue", c) || (c & 0xFFFFFF) != 0xFF0000) CSS_SELF_FAIL();
		if (!cssParseColor("rgb(1,2,3)", c) || (c & 0xFFFFFF) != 0x030201) CSS_SELF_FAIL();
		if (!cssParseColor("rgb(100% 0% 0% / 50%)", c) || c != 0x800000FF) CSS_SELF_FAIL();
		if (!cssParseColor("hsl(120 100% 50% / 25%)", c) || c != 0x4000FF00) CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("color: #00ff00; font-size: 20px; font-weight: bold;", s);
		if (s.fontSize != 20) CSS_SELF_FAIL();
		if (!s.bold) CSS_SELF_FAIL();
		if (!s.colorSet) CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "img");
		doc.setAttr(node, "class", "thumbnail");
		doc.setAttr(node, "style", "object-fit: fill; object-position: center !important");
		const int supportedNode = doc.createElement(-1, "img");
		doc.setAttr(supportedNode, "class", "supported");
		CssEngine css;
		css.parseStylesheet(".thumbnail { object-fit: cover !important; "
			"object-position: 75% 25% !important; } "
			"img { object-fit: contain; object-position: left top; } "
			"@supports (object-fit: cover) { "
			".supported { object-fit: contain; } }");
		const Style resolved = css.resolve(doc, node, defaultStyle());
		const Style supported = css.resolve(doc, supportedNode, defaultStyle());
		if (resolved.imageFit != CssImageFitCover ||
			resolved.imagePositionX != 50 || resolved.imagePositionY != 50 ||
			supported.imageFit != CssImageFitContain)
			CSS_SELF_FAIL();
	}
	{
		CssEngine css;
		css.parseStylesheet("p { font-size: 18px; } a { font-size: 24px; }");
		if (css.rules().size() != 2) CSS_SELF_FAIL();
		else if (css.rules()[0].tag != "p" || css.rules()[0].style.fontSize != 18 ||
				 css.rules()[1].tag != "a" || css.rules()[1].style.fontSize != 24) CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int parent = doc.createElement(-1, "div");
		const int first = doc.createElement(parent, "p");
		doc.createText(parent, "ignored text");
		const int second = doc.createElement(parent, "p");
		const int third = doc.createElement(parent, "p");
		CssEngine css;
		css.parseStylesheet(
			"p:first-child { padding-top: 1px; } "
			"p:last-child { margin-top: 2px; } "
			"p:nth-child(odd) { padding-bottom: 3px; } "
			"p:nth-child(even) { padding-bottom: 4px; } "
			"p:nth-child(2n + 1) { margin-left: 5px; } "
			"p:nth-child(-n+2) { margin-right: 6px; } "
			"p:nth-child(1.5n) { color: red; }");
		const Style firstStyle = css.resolve(doc, first, defaultStyle());
		const Style secondStyle = css.resolve(doc, second, defaultStyle());
		const Style thirdStyle = css.resolve(doc, third, defaultStyle());
		if (css.rules().size() != 6 ||
			firstStyle.paddingTop != 1 || firstStyle.paddingBottom != 3 ||
			firstStyle.marginLeft != 5 || firstStyle.marginRight != 6 ||
			secondStyle.paddingBottom != 4 || secondStyle.marginRight != 6 ||
			thirdStyle.marginTop != 2 || thirdStyle.paddingBottom != 3 ||
			thirdStyle.marginLeft != 5)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int parent = doc.createElement(-1, "section");
		const int first = doc.createElement(parent, "div");
		const int adjacent = doc.createElement(parent, "p");
		const int later = doc.createElement(parent, "p");
		const int onlyParent = doc.createElement(-1, "div");
		const int onlyChild = doc.createElement(onlyParent, "p");
		doc.setAttr(first, "class", "item selected");
		doc.setAttr(adjacent, "data-kind", "article-card");
		doc.setAttr(adjacent, "lang", "en-US");
		CssEngine css;
		css.parseStylesheet(
			".item + p { color: red; } "
			".selected ~ p { background: blue; } "
			"[data-kind^=article][lang|=en] { font-weight: bold; } "
			"p:only-child { text-decoration: underline; } "
			"p:nth-of-type(2) { font-size: 20px; }");
		const Style adjacentStyle = css.resolve(doc, adjacent, defaultStyle());
		const Style laterStyle = css.resolve(doc, later, defaultStyle());
		const Style onlyStyle = css.resolve(doc, onlyChild, defaultStyle());
		if (adjacentStyle.color != 0xFF0000FF ||
			!adjacentStyle.bgSet || adjacentStyle.bg != 0xFFFF0000 ||
			!adjacentStyle.bold || adjacentStyle.fontSize != 0 ||
			!onlyStyle.underline ||
			laterStyle.color != 0xE8E8EC || !laterStyle.bgSet ||
			laterStyle.bg != 0xFFFF0000 ||
			laterStyle.fontSize != 20)
			CSS_SELF_FAIL();
	}
	{
		CssEngine css;
		css.parseStylesheet(".first, h2 { color: red; } "
			"@media screen and (max-width: 400px) { p { color: blue; } "
			".nested { padding: 8px; } }");
		if (css.rules().size() != 4) CSS_SELF_FAIL();
		else if (css.rules()[0].cls != "first" || css.rules()[1].tag != "h2" ||
			css.rules()[2].tag != "p" || css.rules()[3].cls != "nested")
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int root = doc.createElement(-1, "main");
		std::vector<int> nodes;
		for (int i = 0; i < 128; ++i)
		{
			const int node = doc.createElement(root, "p");
			doc.setAttr(node, "class", "target extra");
			doc.setAttr(node, "data-state", "ready");
			nodes.push_back(node);
		}
		CssEngine css;
		std::string stylesheet = "* { color: red; } ";
		for (int i = 0; i < 512; ++i)
			stylesheet += ".unused-" + itos(i) + " { padding: 1px; } ";
		stylesheet += ".target { color: blue; } "
			"[data-state=ready] { font-weight: bold; }";
		css.parseStylesheet(stylesheet);
		if (css.rules().size() != 515) CSS_SELF_FAIL();
		for (int node : nodes)
		{
			const Style resolved = css.resolve(doc, node, defaultStyle());
			if (resolved.color != 0xFFFF0000 || !resolved.bold)
			{
				CSS_SELF_FAIL();
				break;
			}
		}
		css.clear();
		css.parseStylesheet("* { color: green; }");
		const Style resolved = css.resolve(doc, nodes[0], defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFF008000)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int root = doc.createElement(-1, "div");
		const int child = doc.createElement(root, "span");
		CssEngine css;
		Style parent;
		parent.bgSet = true;
		parent.bg = 0x12345678;
		parent.paddingLeft = 12;
		parent.marginTop = 8;
		parent.block = true;
		parent.colorSet = true;
		parent.color = 0x87654321;
		parent.fontSize = 20;
		parent.bold = true;
		Style inherited = css.resolve(doc, child, parent);
		if (inherited.color != parent.color || inherited.fontSize != parent.fontSize ||
			!inherited.bold || inherited.bgSet || inherited.paddingLeft ||
			inherited.marginTop || inherited.block)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		doc.setAttr(node, "class", "target");
		doc.setAttr(node, "id", "target");
		doc.setAttr(node, "style",
			"color: #123456; margin-top: 4px; font-weight: bold; display: block");

		CssEngine css;
		css.parseStylesheet(
			"p { color: red; background: yellow; font-weight: bold; font-size: 18px; display: none; "
			"margin: 1px; padding: 2px; } "
			".target { color: blue; font-weight: normal; display: none; margin-left: 9px; } "
			"#target { font-size: 22px; padding-left: 11px; } "
			"p { color: green; } .target { color: orange; }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (resolved.color != 0xFF563412 || !resolved.bgSet ||
			resolved.bg != 0xFF00FFFF || !resolved.bold || resolved.fontSize != 22 ||
			resolved.marginTop != 4 || resolved.marginLeft != 9 ||
			resolved.paddingTop != 2 || resolved.paddingLeft != 11 ||
			resolved.displayNone || !resolved.block)
			CSS_SELF_FAIL();

		const int stylesheetNode = doc.createElement(-1, "p");
		doc.setAttr(stylesheetNode, "class", "target");
		doc.setAttr(stylesheetNode, "id", "target");
		Style stylesheetStyle = css.resolve(doc, stylesheetNode, defaultStyle());
		if (stylesheetStyle.color != 0xFF00A5FF || !stylesheetStyle.displayNone ||
			stylesheetStyle.block)
			CSS_SELF_FAIL();
	}
	{
		CssEngine css;
		std::string sheet;
		for (size_t i = 0; i < CssEngine::MaxRules + 128; ++i)
			sheet += "p { color: red; }";
		css.parseStylesheet(sheet);
		if (css.rules().size() != CssEngine::MaxRules) CSS_SELF_FAIL();
	}
	{
		CssEngine css;
		std::string sheet = "/*";
		sheet.append(CssEngine::MaxStylesheetBytes, 'x');
		sheet += "*/ p { color: red; }";
		css.parseStylesheet(sheet);
		if (css.stylesheetBytes() != CssEngine::MaxStylesheetBytes ||
			!css.rules().empty())
			CSS_SELF_FAIL();
		css.parseStylesheet("p { color: red; }");
		if (css.stylesheetBytes() != CssEngine::MaxStylesheetBytes ||
			!css.rules().empty())
			CSS_SELF_FAIL();
		css.clear();
		const std::string smallSheet = "p { color: red; }";
		css.parseStylesheet(smallSheet);
		if (css.stylesheetBytes() != smallSheet.size() || css.rules().size() != 1)
			CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("color: red !important; font-size: 2em; margin: 10px 20px; padding: 5px 10px 15px 20px; font-weight: normal; text-decoration: none;", s);
		if (s.color != 0xFF0000FF || !s.colorSet) CSS_SELF_FAIL();
		if (s.fontSize != 32) CSS_SELF_FAIL();
		if (s.marginTop != 10 || s.marginRight != 20 || s.marginBottom != 10 || s.marginLeft != 20) CSS_SELF_FAIL();
		if (s.paddingTop != 5 || s.paddingRight != 10 || s.paddingBottom != 15 || s.paddingLeft != 20) CSS_SELF_FAIL();
		if (s.bold) CSS_SELF_FAIL();
		if (s.underline) CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("width: 50vw; min-width: 25%; max-width: 80%; margin: 0 auto; "
			"box-sizing: border-box; display: flex; flex-direction: row; "
			"flex-wrap: wrap; justify-content: space-between; gap: 8px 12px;", s);
		if (s.width != 200 || s.minWidth != 100 || s.maxWidth != 320 ||
			!s.marginLeftAuto || !s.marginRightAuto || !s.borderBox || !s.flex ||
			!s.flexRow || !s.flexWrap || s.justifyContent != 2 ||
			s.rowGap != 8 || s.columnGap != 12)
			CSS_SELF_FAIL();
		Document doc;
		const int node = doc.createElement(-1, "div");
		CssEngine css;
		css.parseStylesheet("div { min-width: 30%; max-width: 70%; margin: 0 auto; display: flex; "
			"flex-direction: row; flex-wrap: nowrap; justify-content: center; box-sizing: border-box; "
			"row-gap: 5px; column-gap: 9px; }");
		const Style resolved = css.resolve(doc, node, defaultStyle());
		if (resolved.minWidth != 120 || resolved.maxWidth != 280 ||
			!resolved.marginLeftAuto || !resolved.marginRightAuto || !resolved.flex ||
			!resolved.flexRow || resolved.flexWrap || resolved.justifyContent != 1 ||
			!resolved.borderBox || resolved.rowGap != 5 || resolved.columnGap != 9)
			CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("width: 50%; min-width: 25%; max-width: 80%; "
			"white-space: pre-wrap; text-transform: uppercase;", s);
		if (!s.widthIsPercent || s.widthPercent != 5000 ||
			!s.minWidthIsPercent || s.minWidthPercent != 2500 ||
			!s.maxWidthIsPercent || s.maxWidthPercent != 8000 ||
			s.whiteSpace != CssWhiteSpacePreWrap ||
			s.textTransform != CssTextTransformUppercase)
			CSS_SELF_FAIL();
		parseInlineStyle("overflow: hidden auto;", s);
		if (!s.overflowXClip || !s.overflowYClip) CSS_SELF_FAIL();
		parseInlineStyle("overflow-x: visible; overflow-y: scroll;", s);
		if (s.overflowXClip || !s.overflowYClip) CSS_SELF_FAIL();
		parseInlineStyle("height: 50%; min-height: 25%; max-height: 75%;", s);
		if (!s.heightIsPercent || s.heightPercent != 5000 ||
			!s.minHeightIsPercent || s.minHeightPercent != 2500 ||
			!s.maxHeightIsPercent || s.maxHeightPercent != 7500)
			CSS_SELF_FAIL();
		parseInlineStyle("position: relative; left: 12px; bottom: 3px;", s);
		if (!s.positionRelative || s.positionLeft != 12 ||
			s.positionBottom != 3)
			CSS_SELF_FAIL();
		parseInlineStyle("left: 10%; top: 25%;", s);
		if (!s.positionLeftIsPercent || s.positionLeftPercent != 1000 ||
			!s.positionTopIsPercent || s.positionTopPercent != 2500)
			CSS_SELF_FAIL();
		Style unsupportedPosition;
		parseInlineStyle("position: absolute;", unsupportedPosition);
		if (!unsupportedPosition.positionAbsolute ||
			!(unsupportedPosition.declared & CssPropPosition))
			CSS_SELF_FAIL();
		parseInlineStyle("position: fixed;", unsupportedPosition);
		if (!unsupportedPosition.positionFixed ||
			unsupportedPosition.positionAbsolute)
			CSS_SELF_FAIL();
		parseInlineStyle("margin: 1% 2% 3% 4%; padding: 5% 6%;", s);
		if (!s.marginTopIsPercent || s.marginTopPercent != 100 ||
			!s.marginRightIsPercent || s.marginRightPercent != 200 ||
			!s.marginBottomIsPercent || s.marginBottomPercent != 300 ||
			!s.marginLeftIsPercent || s.marginLeftPercent != 400 ||
			!s.paddingTopIsPercent || s.paddingTopPercent != 500 ||
			!s.paddingRightIsPercent || s.paddingRightPercent != 600 ||
			!s.paddingBottomIsPercent || s.paddingBottomPercent != 500 ||
			!s.paddingLeftIsPercent || s.paddingLeftPercent != 600)
			CSS_SELF_FAIL();
		parseInlineStyle("flex: 1.5;", s);
		if (!s.flex || s.flexGrow != 150) CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("width: 999999999999999999999px; height: 12qu; margin-left: 1e999px;", s);
		if (s.width != 100000 || s.height != 0 || s.marginLeft != 0)
			CSS_SELF_FAIL();
		parseInlineStyle("width: calc(100% - 20px); margin-left: calc((2px + 4px) / 2);",
			s);
		if (s.width != 380 || s.marginLeft != 3)
			CSS_SELF_FAIL();
		parseInlineStyle("min-height: min(62vh, 560px); "
			"max-height: clamp(70px, 80vh, 200px); height: 100%;", s);
		if (s.minHeight != 148 || s.maxHeight != 192 || s.height != 240)
			CSS_SELF_FAIL();
		parseInlineStyle("width: calc(10px / 0);", s);
		if (s.width != 380)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet("p { color: red !important; } p { color: blue; } "
			"p { color: green !important; }", 2);
		css.parseStylesheet("p { color: blue; }", 1);
		doc.setAttr(node, "style", "color: blue");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (resolved.color != 0xFF008000) CSS_SELF_FAIL();
		doc.setAttr(node, "style", "color: blue !important");
		resolved = css.resolve(doc, node, defaultStyle());
		if (resolved.color != 0xFFFF0000) CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int root = doc.createElement(-1, "html");
		const int themed = doc.createElement(root, "section");
		const int node = doc.createElement(themed, "p");
		doc.setAttr(themed, "class", "theme");
		doc.setAttr(node, "style",
			"--accent: var(--missing, #00ff00); color: var(--accent); "
			"margin: 2px var(--space, 6px)");
		CssEngine css;
		css.parseStylesheet(
			":root { --accent: #0000ff; --space: 4px; color: #f2e7d5; } "
			".theme { --accent: #ff0000; } "
			"p { color: var(--accent); padding: var(--space); }");
		const Style rootStyle = css.resolve(doc, root, defaultStyle());
		const Style themedStyle = css.resolve(doc, themed, rootStyle);
		const Style resolved = css.resolve(doc, node, themedStyle);
		if (rootStyle.color != 0xFFD5E7F2 || !rootStyle.colorSet ||
			resolved.color != 0xFF00FF00 ||
			resolved.marginTop != 2 || resolved.marginLeft != 4 ||
			resolved.paddingTop != 4 ||
			findCustomProperty(resolved.customProperties, "--accent") == nullptr)
			CSS_SELF_FAIL();

		doc.setAttr(node, "style",
			"--loop: var(--loop); color: var(--loop, rgb(0, 0, 255))");
		const Style fallback = css.resolve(doc, node, themedStyle);
		if (fallback.color != 0xFFFF0000)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet("@media screen and (max-width: 48em), print { p { color: red; } } "
			"@media (min-width: 48em) { p { color: blue; } }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFF0000FF)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.setViewportWidth(640);
		css.parseStylesheet(
			"@media screen and (max-width: 400px), "
			"screen and (min-width: 600px) { p { color: red; } }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFF0000FF)
			CSS_SELF_FAIL();

		css.clear();
		css.setViewportWidth(500);
		css.parseStylesheet(
			"@media screen and (max-width: 400px), "
			"screen and (min-width: 600px) { p { color: red; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet(
			"@supports (display: flex) and (color: red) { "
			"p { color: blue; } } "
			"@supports (unknown-property: value) { "
			"p { background: red; } }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 ||
			resolved.color != 0xFFFF0000 || resolved.bgSet)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@supports not (unknown-property: value) { "
			"p { color: blue; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFFFF0000)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@supports (unknown-property: value) or (color: red) { "
			"p { color: blue; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFFFF0000)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@supports (color: red) and (unknown-property: value) { "
			"p { color: blue; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@supports ((unknown-property: value) and (color: red)) or "
			"(display: flex) { p { color: blue; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFFFF0000)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@supports only (color: red) { p { color: blue; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet(
			"@media not screen and (max-width: 400px) { "
			"p { color: red; } }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@media not screen and (min-width: 500px) { "
			"p { color: red; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFF0000FF)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@media screen and (prefers-color-scheme: dark) { "
			"p { color: red; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();

		css.clear();
		css.parseStylesheet(
			"@media screen and (max-width: 0px) { p { color: red; } }");
		resolved = css.resolve(doc, node, defaultStyle());
		if (!css.rules().empty() || resolved.colorSet)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet(
			"@media screen and (orientation: landscape) { p { color: red; } } "
			"@media screen and (min-height: 200px) { p { font-weight: 700; } }");
		const Style resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 2 || resolved.color != 0xFF0000FF ||
			!resolved.bold)
			CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("display: inline; list-style: none; border: 2px solid #123456;", s);
		if (!s.displayInline || !s.listStyleNone || s.borderWidth != 2 ||
			s.borderColor != 0xFF563412 || s.borderStyle != CssBorderSolid)
			CSS_SELF_FAIL();
		parseInlineStyle("display: table;", s);
		if (!s.displayTable || s.displayTableRow || s.displayTableCell)
			CSS_SELF_FAIL();
		parseInlineStyle("display: table-row;", s);
		if (s.displayTable || !s.displayTableRow || s.displayTableCell)
			CSS_SELF_FAIL();
		parseInlineStyle("display: table-cell;", s);
		if (s.displayTable || s.displayTableRow || !s.displayTableCell)
			CSS_SELF_FAIL();
		parseInlineStyle("display: inline-block;", s);
		if (!s.displayInlineBlock || s.displayInline || s.block)
			CSS_SELF_FAIL();
		parseInlineStyle("vertical-align: middle", s);
		if (!s.verticalAlignMiddle) CSS_SELF_FAIL();
		parseInlineStyle("letter-spacing: 0.5px", s);
		if (s.letterSpacing < 0.49f || s.letterSpacing > 0.51f)
			CSS_SELF_FAIL();
		parseInlineStyle("letter-spacing: 0.1em", s);
		if (!s.letterSpacingIsEm || s.letterSpacing < 0.09f ||
			s.letterSpacing > 0.11f)
			CSS_SELF_FAIL();
		parseInlineStyle("border: 3px dotted red;", s);
		if (s.borderWidth != 3 || s.borderStyle != CssBorderDotted ||
			s.borderColor != 0xFF0000FF)
			CSS_SELF_FAIL();
		parseInlineStyle("border-style: dashed;", s);
		if (s.borderStyle != CssBorderDashed) CSS_SELF_FAIL();
		parseInlineStyle("border-style: none;", s);
		if (s.borderStyle != CssBorderNone)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "div");
		doc.setAttr(node, "style", "border: 2px solid red; "
			"border-top: 4px dashed blue; border-left-width: 3px");
		CssEngine css;
		const Style s = css.resolve(doc, node, defaultStyle());
		if (s.borderTop.width != 4 || s.borderTop.color != 0xFFFF0000 ||
			s.borderTop.style != CssBorderDashed ||
			s.borderRight.width != 2 || s.borderRight.color != 0xFF0000FF ||
			s.borderRight.style != CssBorderSolid ||
			s.borderBottom.width != 2 || s.borderBottom.color != 0xFF0000FF ||
			s.borderLeft.width != 3 || s.borderLeft.color != 0xFF0000FF)
			CSS_SELF_FAIL();
		doc.setAttr(node, "style",
			"border-top: 4px dashed blue !important; border: 2px solid red");
		Style importantSide = css.resolve(doc, node, defaultStyle());
		if (importantSide.borderTop.width != 4 ||
			importantSide.borderTop.color != 0xFFFF0000 ||
			importantSide.borderTop.style != CssBorderDashed ||
			importantSide.borderRight.width != 2 ||
			importantSide.borderRight.color != 0xFF0000FF)
			CSS_SELF_FAIL();
		doc.setAttr(node, "style",
			"border: 2px solid red !important; border-top: 4px dashed blue");
		Style importantShorthand = css.resolve(doc, node, defaultStyle());
		if (importantShorthand.borderTop.width != 2 ||
			importantShorthand.borderTop.color != 0xFF0000FF ||
			importantShorthand.borderTop.style != CssBorderSolid ||
			importantShorthand.borderRight.width != 2 ||
			importantShorthand.borderRight.color != 0xFF0000FF)
			CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("border-radius: 12px; "
			"box-shadow: 1px 2px 8px 1px hsl(0 100% 50% / 50%);", s);
		if (s.borderRadius != 12 || !s.boxShadowSet ||
			s.shadowX != 1 || s.shadowY != 2 ||
			s.shadowBlur != 8 || s.shadowSpread != 1 ||
			s.shadowColor != 0x800000FF)
			CSS_SELF_FAIL();
		parseInlineStyle("box-shadow: none;", s);
		if (s.boxShadowSet) CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("background: linear-gradient(to right, red 0%, rgb(0, 128, 0) 50%, #0000ff);", s);
		if (!s.bgSet || s.gradientCount != 3 || s.gradientAngle != 90 ||
			s.gradientStops[0] != 0 || s.gradientStops[1] != 5000 ||
			s.gradientStops[2] != 10000 ||
			s.gradientColors[0] != 0xFF0000FF ||
			s.gradientColors[1] != 0xFF008000 ||
			s.gradientColors[2] != 0xFFFF0000)
			CSS_SELF_FAIL();

		parseInlineStyle("background: linear-gradient(0.25turn, white, black 80%);", s);
		if (s.gradientCount != 2 || s.gradientAngle != 90 ||
			s.gradientStops[0] != 0 || s.gradientStops[1] != 8000)
			CSS_SELF_FAIL();

		Style invalid;
		parseInlineStyle("background: linear-gradient(red, unknown);", invalid);
		if (invalid.bgSet || invalid.gradientCount != 0)
			CSS_SELF_FAIL();

		Style translucent;
		parseInlineStyle("background: linear-gradient(90deg, #ff000080, rgb(0 0 255 / 50%));",
			translucent);
		if (translucent.gradientCount != 2 ||
			translucent.gradientColors[0] != 0x800000FF ||
			translucent.gradientColors[1] != 0x80FF0000)
			CSS_SELF_FAIL();

		Style radial;
		parseInlineStyle("background: radial-gradient(at center, white, #0000ff);",
			radial);
		if (radial.gradientCount != 2 || !radial.gradientRadial ||
			radial.gradientColors[1] != 0xFFFF0000)
			CSS_SELF_FAIL();
	}
	{
		Document doc;
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet("p { font-size: 20px; line-height: 1.5; } "
			"p { line-height: unsupported; }");
		const Style resolved = css.resolve(doc, node, defaultStyle());
		if (resolved.fontSize != 20 || resolved.lineHeight != 1500 ||
			!resolved.lineHeightRelative || !resolved.lineHeightUnitless)
			CSS_SELF_FAIL();

		const int child = doc.createElement(node, "span");
		const Style inherited = css.resolve(doc, child, resolved);
		if (inherited.lineHeight != 1500 || !inherited.lineHeightRelative ||
			!inherited.lineHeightUnitless)
			CSS_SELF_FAIL();

		doc.setAttr(child, "style", "font-size: 12px;");
		const Style inheritedForSmallText = css.resolve(doc, child, resolved);
		if (inheritedForSmallText.fontSize != 12 ||
			inheritedForSmallText.lineHeight != 1500 ||
			!inheritedForSmallText.lineHeightUnitless)
			CSS_SELF_FAIL();

		doc.setAttr(child, "style", "font-size: 12px; line-height: 150%;");
		const Style percentage = css.resolve(doc, child, resolved);
		if (percentage.fontSize != 12 || percentage.lineHeight != 18 ||
			percentage.lineHeightRelative || percentage.lineHeightUnitless)
			CSS_SELF_FAIL();

		doc.setAttr(child, "style", "line-height: 24px;");
		const Style fixed = css.resolve(doc, child, resolved);
		if (fixed.lineHeight != 24 || fixed.lineHeightRelative)
			CSS_SELF_FAIL();
	}
#undef CSS_SELF_FAIL
	return fails;
}
