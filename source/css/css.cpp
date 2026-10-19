#include "css.hpp"

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
			if (aa < 26) { out = 0; return true; }  // alpha <= ~0.1 -> transparent
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
			if (aa < 26) { out = 0; return true; }  // alpha <= ~0.1 -> transparent
			out = (aa << 24) | (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
			return true;
		}
		return false;
	}
	if (strStartsWith(s, "rgb(") || strStartsWith(s, "rgba("))
	{
		bool isRgba = strStartsWith(s, "rgba(");
		size_t start = isRgba ? 5 : 4;
		int r = 0, g = 0, b = 0;
		float af = 1.0f;
		std::string inner = s.substr(start);
		for (char& ch : inner) if (ch == ',' || ch == ')') ch = ' ';
		int parsed = sscanf(inner.c_str(), "%d %d %d %f", &r, &g, &b, &af);
		if (parsed >= 3)
		{
			if (r < 0) r = 0;
			if (r > 255) r = 255;
			if (g < 0) g = 0;
			if (g > 255) g = 255;
			if (b < 0) b = 0;
			if (b > 255) b = 255;
			if (isRgba && parsed >= 4 && af <= 0.1f) { out = 0; return true; }
			uint32_t aa = 0xFF;
			if (isRgba && parsed >= 4) aa = (uint32_t)(af * 255.0f + 0.5f);
			if (aa > 255) aa = 255;
			out = (aa << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
			return true;
		}
		return false;
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
static int parseCssLength(const std::string& token, int base = 16, int percentBase = NW_PAGE_WIDTH)
{
	std::string t = strTrim(token);
	if (t.empty() || t == "auto") return 0;
	char* end = nullptr;
	float val = strtof(t.c_str(), &end);
	if (end == t.c_str()) return 0;
	std::string unit = toLower(strTrim(std::string(end)));
	if (unit == "em" || unit == "rem") return (int)(val * (float)base);
	if (unit == "pt") return (int)(val * 4.0f / 3.0f);
	if (unit == "%") return (int)(val * (float)percentBase / 100.0f);
	if (unit == "vw") return (int)(val * (float)NW_PAGE_WIDTH / 100.0f);
	if (unit == "vh") return (int)(val * (float)NW_TOP_HEIGHT / 100.0f);
	return (int)val;
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
		uint32_t c;
		if (cssParseColor(v, c))
		{
			out.bg = c; out.bgSet = true;
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
			out.fontSize = px;
			out.declared |= CssPropFontSize;
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
	if (p == "text-align")
	{
		if (v == "center") out.align = 1;
		else if (v == "right") out.align = 2;
		else out.align = 0;
		out.declared |= CssPropAlign;
		return;
	}
	if (p == "display")
	{
		if (v == "none")
		{
			out.displayNone = true; out.block = false; out.displayInline = false; out.flex = false;
			out.declared |= CssPropDisplay | CssPropFlex;
		}
		else if (v == "block")
		{
			out.displayNone = false; out.block = true; out.displayInline = false; out.flex = false;
			out.declared |= CssPropDisplay | CssPropFlex;
		}
		else if (v == "flex")
		{
			out.displayNone = false; out.block = true; out.displayInline = false; out.flex = true;
			out.declared |= CssPropDisplay | CssPropFlex;
		}
		else if (v == "inline")
		{
			out.displayNone = false; out.block = false; out.displayInline = true; out.flex = false;
			out.declared |= CssPropDisplay | CssPropFlex;
		}
		else if (v == "inline-block")
		{
			out.displayNone = false; out.block = true; out.displayInline = false; out.flex = false;
			out.declared |= CssPropDisplay | CssPropFlex;
		}
		return;
	}
	if (p == "flex-direction")
	{
		out.flexRow = (v == "row" || v == "row-reverse");
		out.flex = true;
		out.declared |= CssPropFlex | CssPropFlexRow;
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
		out.justifyContent = (v == "center" ? 1 :
			(v == "space-between" ? 2 : 0));
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
		if (p == "margin")
		{
			out.marginTop = m[0]; out.marginRight = m[1]; out.marginBottom = m[2]; out.marginLeft = m[3];
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
			out.declared |= CssPropPaddingTop | CssPropPaddingRight |
				CssPropPaddingBottom | CssPropPaddingLeft;
		}
		return;
	}
	if (p == "margin-top" || p == "margin-bottom" || p == "margin-left" || p == "margin-right")
	{
		int m = parseCssLength(v);
		if (p == "margin-top") { out.marginTop = m; out.declared |= CssPropMarginTop; }
		else if (p == "margin-bottom") { out.marginBottom = m; out.declared |= CssPropMarginBottom; }
		else if (p == "margin-left") { out.marginLeft = m; out.marginLeftAuto = (v == "auto"); out.declared |= CssPropMarginLeft | CssPropMarginLeftAuto; }
		else { out.marginRight = m; out.marginRightAuto = (v == "auto"); out.declared |= CssPropMarginRight | CssPropMarginRightAuto; }
		return;
	}
	if (p == "padding-top" || p == "padding-bottom" || p == "padding-left" || p == "padding-right")
	{
		int m = parseCssLength(v);
		if (p == "padding-top") { out.paddingTop = m; out.declared |= CssPropPaddingTop; }
		else if (p == "padding-bottom") { out.paddingBottom = m; out.declared |= CssPropPaddingBottom; }
		else if (p == "padding-left") { out.paddingLeft = m; out.declared |= CssPropPaddingLeft; }
		else { out.paddingRight = m; out.declared |= CssPropPaddingRight; }
		return;
	}
	if (p == "width")
	{
		int w = parseCssLength(v);
		if (w > 0) { out.width = w; out.declared |= CssPropWidth; }
		return;
	}
	if (p == "min-width" || p == "max-width")
	{
		const int w = parseCssLength(v, 16, NW_PAGE_WIDTH);
		if (p == "min-width") { out.minWidth = w; out.declared |= CssPropMinWidth; }
		else { out.maxWidth = w; out.declared |= CssPropMaxWidth; }
		return;
	}
	if (p == "height")
	{
		int h = parseCssLength(v);
		if (h > 0) { out.height = h; out.declared |= CssPropHeight; }
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
	if (p == "border" || p == "border-width" || p == "border-color")
	{
		if (p == "border")
		{
			out.borderWidth = v == "none" ? 0 : 1;
			size_t start = 0;
			while (start < v.size())
			{
				while (start < v.size() && isspace((unsigned char)v[start])) ++start;
				size_t end = v.find_first_of(" \t", start);
				if (end == std::string::npos) end = v.size();
				const std::string token = v.substr(start, end - start);
				if (!token.empty())
				{
					uint32_t color;
					if (cssParseColor(token, color)) out.borderColor = color;
					else if (token == "none") out.borderWidth = 0;
					else if (token == "0") out.borderWidth = 0;
					else if (token.find("px") != std::string::npos)
						out.borderWidth = std::max(0, parseCssLength(token));
				}
				start = end;
			}
		}
		else if (p == "border-width")
		{
			out.borderWidth = std::max(0, parseCssLength(v));
		}
		else
		{
			uint32_t color;
			if (cssParseColor(v, color)) out.borderColor = color;
		}
		out.declared |= CssPropBorder;
		return;
	}
}

static void applyProperty(Style& out, const Style& source, uint64_t property)
{
	out.declared |= property;
	switch (property)
	{
	case CssPropColor: out.color = source.color; out.colorSet = source.colorSet; break;
	case CssPropBackground: out.bg = source.bg; out.bgSet = source.bgSet; break;
	case CssPropFontSize: out.fontSize = source.fontSize; break;
	case CssPropBold: out.bold = source.bold; break;
	case CssPropItalic: out.italic = source.italic; break;
	case CssPropUnderline: out.underline = source.underline; break;
	case CssPropAlign: out.align = source.align; break;
	case CssPropMarginTop: out.marginTop = source.marginTop; break;
	case CssPropMarginRight: out.marginRight = source.marginRight; break;
	case CssPropMarginBottom: out.marginBottom = source.marginBottom; break;
	case CssPropMarginLeft: out.marginLeft = source.marginLeft; break;
	case CssPropPaddingTop: out.paddingTop = source.paddingTop; break;
	case CssPropPaddingRight: out.paddingRight = source.paddingRight; break;
	case CssPropPaddingBottom: out.paddingBottom = source.paddingBottom; break;
	case CssPropPaddingLeft: out.paddingLeft = source.paddingLeft; break;
	case CssPropWidth: out.width = source.width; break;
	case CssPropMinWidth: out.minWidth = source.minWidth; break;
	case CssPropMaxWidth: out.maxWidth = source.maxWidth; break;
	case CssPropBoxSizing: out.borderBox = source.borderBox; break;
	case CssPropFlex: out.flex = source.flex; break;
	case CssPropFlexRow: out.flexRow = source.flexRow; break;
	case CssPropFlexWrap: out.flexWrap = source.flexWrap; break;
	case CssPropJustify: out.justifyContent = source.justifyContent; break;
	case CssPropMarginLeftAuto: out.marginLeftAuto = source.marginLeftAuto; break;
	case CssPropMarginRightAuto: out.marginRightAuto = source.marginRightAuto; break;
	case CssPropHeight: out.height = source.height; break;
	case CssPropDisplay:
		out.displayNone = source.displayNone;
		out.block = source.block;
		out.displayInline = source.displayInline;
		break;
	case CssPropMono: out.mono = source.mono; break;
	case CssPropListStyle: out.listStyleNone = source.listStyleNone; break;
	case CssPropBorder:
		out.borderWidth = source.borderWidth;
		out.borderColor = source.borderColor;
		break;
	case CssPropRowGap: out.rowGap = source.rowGap; break;
	case CssPropColumnGap: out.columnGap = source.columnGap; break;
	}
	out.declared |= property;
}

static const uint64_t kCssProperties[] =
{
	CssPropColor, CssPropBackground, CssPropFontSize, CssPropBold, CssPropItalic,
	CssPropUnderline, CssPropAlign, CssPropMarginTop, CssPropMarginRight,
	CssPropMarginBottom, CssPropMarginLeft, CssPropPaddingTop,
	CssPropPaddingRight, CssPropPaddingBottom, CssPropPaddingLeft,
	CssPropWidth, CssPropMinWidth, CssPropMaxWidth, CssPropBoxSizing,
	CssPropHeight, CssPropDisplay, CssPropMono, CssPropFlex, CssPropFlexRow,
	CssPropFlexWrap, CssPropJustify, CssPropMarginLeftAuto, CssPropMarginRightAuto,
	CssPropListStyle, CssPropBorder, CssPropRowGap, CssPropColumnGap
};

static void parseDeclaration(const std::string& prop, const std::string& value, Style& out)
{
	std::string cleaned = strTrim(value);
	const std::string lower = toLower(cleaned);
	const size_t importantPos = lower.find("!important");
	const bool important = importantPos != std::string::npos;
	if (important) cleaned = strTrim(cleaned.substr(0, importantPos));

	Style parsed;
	parseDeclarationValues(prop, cleaned, parsed);
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
	size_t i = 0;
	while (i < decls.size())
	{
		size_t semi = decls.find(';', i);
		std::string part = decls.substr(i, (semi == std::string::npos) ? std::string::npos : semi - i);
		size_t colon = part.find(':');
		if (colon != std::string::npos)
			parseDeclaration(part.substr(0, colon), part.substr(colon + 1), out);
		if (semi == std::string::npos) break;
		i = semi + 1;
	}
}

//-----------------------------------------------------------------------------
// Parsing et correspondance des selecteurs avances
//-----------------------------------------------------------------------------
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
				size_t eq = inner.find('=');
				CssAttrSelector a;
				if (eq != std::string::npos)
				{
					a.name = toLower(strTrim(inner.substr(0, eq)));
					std::string val = strTrim(inner.substr(eq + 1));
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
			else if (pseudo == ":hover") { comp.hover = true; spec += 10; }
			else if (pseudo == ":focus") { comp.focus = true; spec += 10; }
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
		if (a.hasValue && std::string(val) != a.value) return false;
	}

	if (sel.firstChild)
	{
		int parent = node.parent;
		if (parent >= 0 && (size_t)parent < doc.nodes.size())
		{
			const Node& pNode = doc.nodes[parent];
			int firstElem = -1;
			for (int c : pNode.children)
			{
				if (c >= 0 && (size_t)c < doc.nodes.size() && doc.nodes[c].isElement())
				{
					firstElem = c;
					break;
				}
			}
			if (firstElem != nodeIdx) return false;
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
		if (comb == '>')
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
	// Les combinateurs de freres ne sont pas implementees : ignorer la regle
	// au complet plutot que de la decouper en selecteurs descendants faux.
	if (selector.find('+') != std::string::npos || selector.find('~') != std::string::npos) return;

	// Decoupe en composants selon les combinateurs (' ' ou '>')
	struct RawToken { std::string text; char comb; };
	std::vector<RawToken> rawTokens;
	size_t pos = 0;
	while (pos < selector.size())
	{
		while (pos < selector.size() && (selector[pos] == ' ' || selector[pos] == '\t')) pos++;
		if (pos >= selector.size()) break;

		char comb = 0;
		if (selector[pos] == '>')
		{
			comb = '>';
			pos++;
			while (pos < selector.size() && (selector[pos] == ' ' || selector[pos] == '\t')) pos++;
		}
		else if (!rawTokens.empty())
		{
			comb = ' ';
		}

		size_t next = pos;
		while (next < selector.size() && selector[next] != ' ' && selector[next] != '\t' && selector[next] != '>')
			next++;
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

	if (rule.tag.empty() && rule.cls.empty() && rule.id.empty() && target.attrs.empty() && !target.firstChild)
		return;

	// Parse declarations
	size_t k = 0;
	while (k < body.size())
	{
		size_t semi = body.find(';', k);
		std::string part = body.substr(k, (semi == std::string::npos) ? std::string::npos : semi - k);
		size_t colon = part.find(':');
		if (colon != std::string::npos)
			parseDeclaration(part.substr(0, colon), part.substr(colon + 1), rule.style);
		if (semi == std::string::npos) break;
		k = semi + 1;
	}
	rules_.push_back(rule);
}

static int mediaWidth(const std::string& query, size_t valuePos)
{
	char* end = nullptr;
	const float value = strtof(query.c_str() + valuePos, &end);
	if (end == query.c_str() + valuePos || value <= 0.0f) return 0;
	const std::string unit = toLower(strTrim(std::string(end)));
	float pixels = value;
	if (unit.find("rem") == 0 || unit.find("em") == 0) pixels *= 16.0f;
	else if (unit.find("pt") == 0) pixels *= 4.0f / 3.0f;
	else if (unit.find("vw") == 0) pixels *= (float)NW_PAGE_WIDTH / 100.0f;
	return (int)pixels;
}

static bool evalMediaQuery(const std::string& query)
{
	std::string q = toLower(query);
	if (q.find("print") != std::string::npos && q.find("screen") == std::string::npos)
		return false;

	size_t mw = q.find("max-width");
	if (mw != std::string::npos)
	{
		size_t colon = q.find(':', mw);
		if (colon != std::string::npos)
		{
			int maxW = mediaWidth(q, colon + 1);
			if (maxW > 0 && NW_PAGE_WIDTH > maxW) return false;
		}
	}

	size_t minw = q.find("min-width");
	if (minw != std::string::npos)
	{
		size_t colon = q.find(':', minw);
		if (colon != std::string::npos)
		{
			int minW = mediaWidth(q, colon + 1);
			if (minW > 0 && NW_PAGE_WIDTH < minW) return false;
		}
	}
	return true;
}

void CssEngine::parseStylesheet(const std::string& css, int sourceOrder)
{
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
			if (evalMediaQuery(selector))
			{
				// Evaluer le bloc imbrique avec nos 400px de largeur
				parseStylesheet(body);
			}
		}
		else if (strStartsWith(selector, "@supports") || strStartsWith(selector, "@layer"))
		{
			// Parser directement le contenu du bloc
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
					sheet += ch.text;
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
	Style out;
	out.color = parent.color;
	out.colorSet = parent.colorSet;
	out.fontSize = parent.fontSize;
	out.bold = parent.bold;
	out.italic = parent.italic;
	out.align = parent.align;
	out.mono = parent.mono;
	if (nodeIdx < 0 || (size_t)nodeIdx >= doc.nodes.size()) return out;
	const Node& node = doc.nodes[nodeIdx];
	if (!node.isElement()) return out;

	int bestSpec[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	int bestOrder[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	bool bestImportant[sizeof(kCssProperties) / sizeof(kCssProperties[0])];
	for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
	{
		bestSpec[p] = -1;
		bestOrder[p] = -1;
		bestImportant[p] = false;
	}

	for (const auto& r : rules_)
	{
		if (matchRule(doc, nodeIdx, r))
		{
			for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
			{
				const uint64_t property = kCssProperties[p];
				if (r.style.declared & property)
				{
					const bool important = (r.style.important & property) != 0;
					if ((important && !bestImportant[p]) ||
						(important == bestImportant[p] &&
						 (r.specificity > bestSpec[p] ||
						  (r.specificity == bestSpec[p] && r.order >= bestOrder[p]))))
					{
						bestSpec[p] = r.specificity;
						bestOrder[p] = r.order;
						bestImportant[p] = important;
						applyProperty(out, r.style, property);
					}
				}
			}
		}
	}

	// 2. style inline (priorite superieure a toute regle de feuille de style)
	const char* inlineStyle = node.getAttr("style");
	if (inlineStyle)
	{
		Style inlineRules;
		parseInlineStyle(inlineStyle, inlineRules);
		for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
			if (inlineRules.declared & kCssProperties[p])
			{
				const uint64_t property = kCssProperties[p];
				const bool important = (inlineRules.important & property) != 0;
				if (!bestImportant[p] || important)
					applyProperty(out, inlineRules, property);
			}
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
	}
	{
		Style s;
		parseInlineStyle("color: #00ff00; font-size: 20px; font-weight: bold;", s);
		if (s.fontSize != 20) CSS_SELF_FAIL();
		if (!s.bold) CSS_SELF_FAIL();
		if (!s.colorSet) CSS_SELF_FAIL();
	}
	{
		CssEngine css;
		css.parseStylesheet("p { font-size: 18px; } a { font-size: 24px; }");
		if (css.rules().size() != 2) CSS_SELF_FAIL();
		else if (css.rules()[0].tag != "p" || css.rules()[0].style.fontSize != 18 ||
				 css.rules()[1].tag != "a" || css.rules()[1].style.fontSize != 24) CSS_SELF_FAIL();
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
		for (int i = 0; i < 1024; i++)
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
		const int node = doc.createElement(-1, "p");
		CssEngine css;
		css.parseStylesheet("@media screen and (max-width: 48em), print { p { color: red; } } "
			"@media (min-width: 48em) { p { color: blue; } }");
		Style resolved = css.resolve(doc, node, defaultStyle());
		if (css.rules().size() != 1 || resolved.color != 0xFF0000FF)
			CSS_SELF_FAIL();
	}
	{
		Style s;
		parseInlineStyle("display: inline; list-style: none; border: 2px solid #123456;", s);
		if (!s.displayInline || !s.listStyleNone || s.borderWidth != 2 ||
			s.borderColor != 0xFF563412)
			CSS_SELF_FAIL();
	}
#undef CSS_SELF_FAIL
	return fails;
}
