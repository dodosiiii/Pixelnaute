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
	{ "indigo", 0x4B0082 }, { "darkgray", 0xA9A9A9 }, { "lightgray", 0xD3D3D3 },
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
		if (s.size() == 7)
		{
			unsigned v;
			if (sscanf(s.c_str() + 1, "%6x", &v) != 1) return false;
			out = 0xFF000000 | ((v & 0xFF) << 16) | (v & 0xFF00) | ((v & 0xFF0000) >> 16);
			return true;
		}
		return false;
	}
	if (strStartsWith(s, "rgb("))
	{
		int r, g, b;
		if (sscanf(s.c_str() + 4, "%d,%d,%d", &r, &g, &b) == 3)
		{
			if (r < 0) r = 0; if (r > 255) r = 255;
			if (g < 0) g = 0; if (g > 255) g = 255;
			if (b < 0) b = 0; if (b > 255) b = 255;
			out = 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
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
// Parse d'une declaration "prop: value"
//-----------------------------------------------------------------------------
static void parseDeclaration(const std::string& prop, const std::string& value, Style& out)
{
	std::string p = toLower(strTrim(prop));
	std::string v = toLower(strTrim(value));

	if (p == "color" || p == "text-color")
	{
		uint32_t c;
		if (cssParseColor(v, c)) { out.color = c; out.colorSet = true; }
		return;
	}
	if (p == "background" || p == "background-color" || p == "bgcolor")
	{
		uint32_t c;
		if (cssParseColor(v, c)) { out.bg = c; out.bgSet = true; }
		return;
	}
	if (p == "font-size")
	{
		if (strEndsWith(v, "px"))
		{
			int px = atoi(v.c_str());
			if (px > 0) out.fontSize = px;
		}
		return;
	}
	if (p == "font-weight")
	{
		out.bold = (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900");
		return;
	}
	if (p == "font-style") { out.italic = (v == "italic" || v == "oblique"); return; }
	if (p == "text-decoration")
	{
		out.underline = (v.find("underline") != std::string::npos);
		return;
	}
	if (p == "text-align")
	{
		if (v == "center") out.align = 1;
		else if (v == "right") out.align = 2;
		else out.align = 0;
		return;
	}
	if (p == "display")
	{
		if (v == "none") out.displayNone = true;
		else if (v == "block") out.block = true;
		return;
	}
	if (p == "margin" || p == "padding")
	{
		int m[4] = { 0, 0, 0, 0 };
		int n = sscanf(v.c_str(), "%d %d %d %d", &m[0], &m[1], &m[2], &m[3]);
		if (n == 1) m[1] = m[2] = m[3] = m[0];
		else if (n == 2) { m[2] = m[0]; m[3] = m[1]; }
		else if (n == 3) m[3] = m[1];
		if (p == "margin")
		{
			out.marginTop = m[0]; out.marginRight = m[1]; out.marginBottom = m[2]; out.marginLeft = m[3];
		}
		else
		{
			out.paddingTop = m[0]; out.paddingRight = m[1]; out.paddingBottom = m[2]; out.paddingLeft = m[3];
		}
		return;
	}
	if (p == "margin-top" || p == "margin-bottom" || p == "margin-left" || p == "margin-right")
	{
		int m = atoi(v.c_str());
		if (p == "margin-top") out.marginTop = m;
		else if (p == "margin-bottom") out.marginBottom = m;
		else if (p == "margin-left") out.marginLeft = m;
		else out.marginRight = m;
		return;
	}
	if (p == "padding-top" || p == "padding-bottom" || p == "padding-left" || p == "padding-right")
	{
		int m = atoi(v.c_str());
		if (p == "padding-top") out.paddingTop = m;
		else if (p == "padding-bottom") out.paddingBottom = m;
		else if (p == "padding-left") out.paddingLeft = m;
		else out.paddingRight = m;
		return;
	}
	if (p == "width") { int w = atoi(v.c_str()); if (w > 0) out.width = w; return; }
	if (p == "height") { int h = atoi(v.c_str()); if (h > 0) out.height = h; return; }
	if (p == "font-family")
	{
		if (v.find("mono") != std::string::npos || v.find("courier") != std::string::npos)
			out.mono = true;
		return;
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

	CssRule rule;
	// selecteur : [tag][.classe][#id] (ignore les selecteurs complexes)
	size_t i = 0;
	bool hasTag = false;
	while (i < selector.size())
	{
		char c = selector[i];
		if (c == '.' || c == '#' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '*' || c == '_')
		{
			if (c == '.')
			{
				size_t j = i + 1;
				std::string cls;
				while (j < selector.size() && (isalnum((unsigned char)selector[j]) || selector[j] == '-' || selector[j] == '_'))
					cls += selector[j++];
				rule.cls = cls;
				rule.specificity += 10;
				i = j;
			}
			else if (c == '#')
			{
				size_t j = i + 1;
				std::string id;
				while (j < selector.size() && (isalnum((unsigned char)selector[j]) || selector[j] == '-' || selector[j] == '_'))
					id += selector[j++];
				rule.id = id;
				rule.specificity += 100;
				i = j;
			}
			else if (c == '*') { hasTag = true; i++; }
			else
			{
				size_t j = i;
				std::string tag;
				while (j < selector.size() && (isalnum((unsigned char)selector[j]) || selector[j] == '-'))
					tag += (char)(selector[j] >= 'A' && selector[j] <= 'Z' ? selector[j] + 32 : selector[j]), j++;
				rule.tag = tag;
				hasTag = true;
				rule.specificity += 1;
				i = j;
			}
		}
		else i++;
	}
	if (!hasTag && rule.cls.empty() && rule.id.empty()) return; // selecteur ignore

	// parse declarations
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

void CssEngine::parseStylesheet(const std::string& css)
{
	// Supprime les commentaires /* ... */
	std::string clean;
	clean.reserve(css.size());
	size_t i = 0;
	while (i < css.size())
	{
		if (css.compare(i, 2, "/*") == 0)
		{
			size_t e = css.find("*/", i + 2);
			i = (e == std::string::npos) ? css.size() : e + 2;
		}
		else clean += css[i++];
	}

	size_t pos = 0;
	while (pos < clean.size() && rules_.size() < MaxRules)
	{
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

		if (selector.empty() || selector[0] != '@')
		{
			const std::string body = clean.substr(brace + 1, endBrace - brace - 2);
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
			// contenu brut = enfants Text/RawText concatenes
			for (int c : n.children)
			{
				const Node& ch = doc.nodes[c];
				if (ch.type == NodeType::RawText || ch.type == NodeType::Text)
				{
					if (!ch.text.empty())
						css.parseStylesheet(ch.text);
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Resolution : applique les regles du parent vers le noeud,
// puis le style inline du noeud.
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
	if (nodeIdx < 0) return out;
	const Node& node = doc.nodes[nodeIdx];
	if (!node.isElement()) return out;

	// Rules: parent le plus lointain d'abord? On re-resout depuis le haut.
	// (appelle recurse du haut vers le bas via parentStyle fourni)
	// 1. regles du document pour ce tag/classe/id
	int bestSpec = -1;
	for (const auto& r : rules_)
	{
		bool match = true;
		if (!r.tag.empty() && r.tag != "*" && r.tag != node.tag) match = false;
		if (match && !r.cls.empty())
		{
			const char* cls = node.getAttr("class");
			if (!cls) match = false;
			else
			{
				// "a b c" contains r.cls
				std::string all = cls;
				bool found = false;
				size_t p = 0;
				while (p <= all.size())
				{
					size_t sp = all.find_first_of(" \t", p);
					std::string one = all.substr(p, (sp == std::string::npos) ? std::string::npos : sp - p);
					if (one == r.cls) { found = true; break; }
					if (sp == std::string::npos) break;
					p = sp + 1;
				}
				if (!found) match = false;
			}
		}
		if (match && !r.id.empty())
		{
			const char* id = node.getAttr("id");
			if (!id || std::string(id) != r.id) match = false;
		}
		if (match && r.specificity >= bestSpec)
		{
			bestSpec = r.specificity;
			// applique (les regles plus specifiques ecrasent)
			const Style& rs = r.style;
			if (rs.colorSet) { out.color = rs.color; out.colorSet = true; }
			if (rs.bgSet) { out.bg = rs.bg; out.bgSet = true; }
			if (rs.fontSize) out.fontSize = rs.fontSize;
			if (rs.bold) out.bold = true;
			if (rs.italic) out.italic = true;
			if (rs.underline) out.underline = true;
			out.align = rs.align;
			out.marginTop = rs.marginTop; out.marginBottom = rs.marginBottom;
			out.marginLeft = rs.marginLeft; out.marginRight = rs.marginRight;
			out.paddingTop = rs.paddingTop; out.paddingBottom = rs.paddingBottom;
			out.paddingLeft = rs.paddingLeft; out.paddingRight = rs.paddingRight;
			out.width = rs.width; out.height = rs.height;
			if (rs.displayNone) out.displayNone = true;
			if (rs.block) out.block = true;
			if (rs.mono) out.mono = true;
		}
	}

	// 2. style inline
	const char* inlineStyle = node.getAttr("style");
	if (inlineStyle) parseInlineStyle(inlineStyle, out);

	return out;
}

//-----------------------------------------------------------------------------
// Tests unitaires
//-----------------------------------------------------------------------------
int cssSelfTest()
{
	int fails = 0;
	{
		uint32_t c;
		if (!cssParseColor("#ff0000", c) || (c & 0xFFFFFF) != 0x0000FF) fails++;
		if (!cssParseColor("blue", c) || (c & 0xFFFFFF) != 0xFF0000) fails++;
		if (!cssParseColor("rgb(1,2,3)", c) || (c & 0xFFFFFF) != 0x030201) fails++;
	}
	{
		Style s;
		parseInlineStyle("color: #00ff00; font-size: 20px; font-weight: bold;", s);
		if (s.fontSize != 20) fails++;
		if (!s.bold) fails++;
		if (!s.colorSet) fails++;
	}
	{
		CssEngine css;
		css.parseStylesheet("p { font-size: 18px; } a { font-size: 24px; }");
		if (css.rules().size() != 2) fails++;
		else if (css.rules()[0].tag != "p" || css.rules()[0].style.fontSize != 18 ||
				 css.rules()[1].tag != "a" || css.rules()[1].style.fontSize != 24) fails++;
	}
	{
		CssEngine css;
		css.parseStylesheet(".first, h2 { color: red; } "
			"@media screen and (max-width: 400px) { p { color: blue; } "
			".nested { padding: 8px; } }");
		if (css.rules().size() != 2) fails++;
		else if (css.rules()[0].cls != "first" || css.rules()[1].tag != "h2")
			fails++;
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
			fails++;
	}
	{
		CssEngine css;
		std::string sheet;
		for (int i = 0; i < 1024; i++)
			sheet += "p { color: red; }";
		css.parseStylesheet(sheet);
		if (css.rules().size() != CssEngine::MaxRules) fails++;
	}
	return fails;
}