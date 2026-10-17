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
	if (strStartsWith(s, "rgb(") || strStartsWith(s, "rgba("))
	{
		size_t start = strStartsWith(s, "rgba(") ? 5 : 4;
		int r = 0, g = 0, b = 0;
		std::string inner = s.substr(start);
		for (char& ch : inner) if (ch == ',' || ch == ')') ch = ' ';
		if (sscanf(inner.c_str(), "%d %d %d", &r, &g, &b) >= 3)
		{
			if (r < 0) r = 0;
			if (r > 255) r = 255;
			if (g < 0) g = 0;
			if (g > 255) g = 255;
			if (b < 0) b = 0;
			if (b > 255) b = 255;
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
// Parse d'une longueur CSS (px, em, rem, pt ou sans unite)
//-----------------------------------------------------------------------------
static int parseCssLength(const std::string& token, int base = 16)
{
	std::string t = strTrim(token);
	if (t.empty() || t == "auto") return 0;
	char* end = nullptr;
	float val = strtof(t.c_str(), &end);
	if (end == t.c_str()) return 0;
	std::string unit = toLower(strTrim(std::string(end)));
	if (unit == "em" || unit == "rem") return (int)(val * (float)base);
	if (unit == "pt") return (int)(val * 4.0f / 3.0f);
	return (int)val;
}

//-----------------------------------------------------------------------------
// Parse d'une declaration "prop: value"
//-----------------------------------------------------------------------------
static void parseDeclaration(const std::string& prop, const std::string& value, Style& out)
{
	std::string p = toLower(strTrim(prop));
	std::string v = toLower(strTrim(value));

	// Ignorer / extraire !important pour ne pas corrompre la valeur
	size_t impPos = v.find("!important");
	if (impPos != std::string::npos)
		v = strTrim(v.substr(0, impPos));

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
		else px = parseCssLength(v, 16);
		if (px > 0)
		{
			out.fontSize = px;
			out.declared |= CssPropFontSize;
		}
		return;
	}
	if (p == "font-weight")
	{
		out.bold = (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900");
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
			out.displayNone = true; out.block = false;
			out.declared |= CssPropDisplay;
		}
		else if (v == "block")
		{
			out.displayNone = false; out.block = true;
			out.declared |= CssPropDisplay;
		}
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
			out.declared |= CssPropMarginTop | CssPropMarginRight |
				CssPropMarginBottom | CssPropMarginLeft;
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
		else if (p == "margin-left") { out.marginLeft = m; out.declared |= CssPropMarginLeft; }
		else { out.marginRight = m; out.declared |= CssPropMarginRight; }
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
}

static void applyProperty(Style& out, const Style& source, uint32_t property)
{
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
	case CssPropHeight: out.height = source.height; break;
	case CssPropDisplay:
		out.displayNone = source.displayNone;
		out.block = source.block;
		break;
	case CssPropMono: out.mono = source.mono; break;
	}
	out.declared |= property;
}

static const uint32_t kCssProperties[] =
{
	CssPropColor, CssPropBackground, CssPropFontSize, CssPropBold, CssPropItalic,
	CssPropUnderline, CssPropAlign, CssPropMarginTop, CssPropMarginRight,
	CssPropMarginBottom, CssPropMarginLeft, CssPropPaddingTop,
	CssPropPaddingRight, CssPropPaddingBottom, CssPropPaddingLeft,
	CssPropWidth, CssPropHeight, CssPropDisplay, CssPropMono
};

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
			while (j < token.size() && (isalnum((unsigned char)token[j]) || token[j] == '-' || token[j] == ':'))
				j++;
			std::string pseudo = toLower(token.substr(i, j - i));
			if (pseudo == ":first-child")
			{
				comp.firstChild = true;
				spec += 10;
			}
			// Les pseudo-classes de survol/focus (:hover, :focus, ::before, etc.)
			// sont ignorees gracieusement sans invalider le reste du selecteur.
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
	rule.order = (int)rules_.size();

	// Construire la chaine de droite a gauche (la cible en premier index 0)
	for (int k = (int)rawTokens.size() - 1; k >= 0; k--)
	{
		CssCompoundSelector comp;
		parseCompound(rawTokens[k].text, comp, rule.specificity);
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

static bool evalMediaQuery(const std::string& query)
{
	std::string q = toLower(query);
	if (q.find("print") != std::string::npos) return false;

	size_t mw = q.find("max-width");
	if (mw != std::string::npos)
	{
		size_t colon = q.find(':', mw);
		if (colon != std::string::npos)
		{
			int maxW = atoi(q.c_str() + colon + 1);
			if (maxW > 0 && NW_PAGE_WIDTH > maxW) return false;
		}
	}

	size_t minw = q.find("min-width");
	if (minw != std::string::npos)
	{
		size_t colon = q.find(':', minw);
		if (colon != std::string::npos)
		{
			int minW = atoi(q.c_str() + colon + 1);
			if (minW > 0 && NW_PAGE_WIDTH < minW) return false;
		}
	}
	return true;
}

void CssEngine::parseStylesheet(const std::string& css)
{
	if (stylesheetBytes_ >= MaxStylesheetBytes) return;
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
	for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
	{
		bestSpec[p] = -1;
		bestOrder[p] = -1;
	}

	for (const auto& r : rules_)
	{
		if (matchRule(doc, nodeIdx, r))
		{
			for (size_t p = 0; p < sizeof(kCssProperties) / sizeof(kCssProperties[0]); ++p)
			{
				const uint32_t property = kCssProperties[p];
				if (r.style.declared & property)
				{
					if (r.specificity > bestSpec[p] || (r.specificity == bestSpec[p] && r.order >= bestOrder[p]))
					{
						bestSpec[p] = r.specificity;
						bestOrder[p] = r.order;
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
				applyProperty(out, inlineRules, kCssProperties[p]);
	}

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
			fails++;

		const int stylesheetNode = doc.createElement(-1, "p");
		doc.setAttr(stylesheetNode, "class", "target");
		doc.setAttr(stylesheetNode, "id", "target");
		Style stylesheetStyle = css.resolve(doc, stylesheetNode, defaultStyle());
		if (stylesheetStyle.color != 0xFF00A5FF || !stylesheetStyle.displayNone ||
			stylesheetStyle.block)
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
	{
		CssEngine css;
		std::string sheet = "/*";
		sheet.append(CssEngine::MaxStylesheetBytes, 'x');
		sheet += "*/ p { color: red; }";
		css.parseStylesheet(sheet);
		if (css.stylesheetBytes() != CssEngine::MaxStylesheetBytes ||
			!css.rules().empty())
			fails++;
		css.parseStylesheet("p { color: red; }");
		if (css.stylesheetBytes() != CssEngine::MaxStylesheetBytes ||
			!css.rules().empty())
			fails++;
		css.clear();
		const std::string smallSheet = "p { color: red; }";
		css.parseStylesheet(smallSheet);
		if (css.stylesheetBytes() != smallSheet.size() || css.rules().size() != 1)
			fails++;
	}
	{
		Style s;
		parseInlineStyle("color: red !important; font-size: 2em; margin: 10px 20px; padding: 5px 10px 15px 20px; font-weight: normal; text-decoration: none;", s);
		if (s.color != 0xFF0000FF || !s.colorSet) fails++;
		if (s.fontSize != 32) fails++;
		if (s.marginTop != 10 || s.marginRight != 20 || s.marginBottom != 10 || s.marginLeft != 20) fails++;
		if (s.paddingTop != 5 || s.paddingRight != 10 || s.paddingBottom != 15 || s.paddingLeft != 20) fails++;
		if (s.bold) fails++;
		if (s.underline) fails++;
	}
	return fails;
}