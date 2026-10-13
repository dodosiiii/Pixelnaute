#include "parser.hpp"

//-----------------------------------------------------------------------------
// Entites nommées supportees (les plus courantes)
//-----------------------------------------------------------------------------
struct NamedEntity { const char* name; uint32_t cp; };

static const NamedEntity kEntities[] =
{
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
	{ "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 },
	{ "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "bull", 0x2022 },
	{ "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
	{ "laquo", 0x00AB }, { "raquo", 0x00BB }, { "deg", 0x00B0 }, { "plusmn", 0x00B1 },
	{ "times", 0x00D7 }, { "divide", 0x00F7 }, { "middot", 0x00B7 }, { "frac12", 0x00BD },
	{ "frac14", 0x00BC }, { "frac34", 0x00BE }, { "para", 0x00B6 }, { "sect", 0x00A7 },
	{ "euro", 0x20AC }, { "pound", 0x00A3 }, { "yen", 0x00A5 }, { "cent", 0x00A2 },
	{ "agrave", 0x00E0 }, { "aacute", 0x00E1 }, { "acirc", 0x00E2 }, { "atilde", 0x00E3 },
	{ "auml", 0x00E4 }, { "aring", 0x00E5 }, { "aelig", 0x00E6 }, { "ccedil", 0x00E7 },
	{ "egrave", 0x00E8 }, { "eacute", 0x00E9 }, { "ecirc", 0x00EA }, { "euml", 0x00EB },
	{ "igrave", 0x00EC }, { "iacute", 0x00ED }, { "icirc", 0x00EE }, { "iuml", 0x00EF },
	{ "ntilde", 0x00F1 }, { "ograve", 0x00F2 }, { "oacute", 0x00F3 }, { "ocirc", 0x00F4 },
	{ "otilde", 0x00F5 }, { "ouml", 0x00F6 }, { "oslash", 0x00F8 }, { "ugrave", 0x00F9 },
	{ "uacute", 0x00FA }, { "ucirc", 0x00FB }, { "uuml", 0x00FC }, { "yuml", 0x00FF },
	{ "szlig", 0x00DF }, { "eacute*", 0 }, // garde-fou (ne matche jamais)
	{ nullptr, 0 }
};

//-----------------------------------------------------------------------------
// Decodage des entites a partir de 'pos' (pos doit pointer sur le '&')
//-----------------------------------------------------------------------------
size_t htmlDecodeEntity(const std::string& s, size_t pos, std::string& out)
{
	size_t n = s.size();
	if (pos >= n || s[pos] != '&') return pos + 1;

	// '&#NNN;' ou '&#xHH;'
	if (pos + 1 < n && s[pos + 1] == '#')
	{
		size_t i = pos + 2;
		int base = 10;
		if (i < n && (s[i] == 'x' || s[i] == 'X')) { base = 16; i++; }
		uint32_t val = 0;
		bool any = false;
		while (i < n)
		{
			char c = s[i];
			int d;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + (c - 'a');
			else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + (c - 'A');
			else break;
			val = val * (uint32_t)base + (uint32_t)d;
			any = true;
			i++;
		}
		if (any)
		{
			if (i < n && s[i] == ';') i++;
			if (val == 0 || val > 0x10FFFF) val = 0xFFFD;
			utf8Append(out, val);
			return i;
		}
	}

	// Entite nommee : '&...;'
	for (int k = 0; kEntities[k].name; k++)
	{
		const char* name = kEntities[k].name;
		size_t len = strlen(name);
		if (name[0] == '*') break;
		if (pos + 1 + len < n && s.compare(pos + 1, len, name) == 0)
		{
			if (s[pos + 1 + len] == ';')
			{
				utf8Append(out, kEntities[k].cp);
				return pos + 1 + len + 1;
			}
			if (len >= 2) // sans point-virgule (pratique courante)
			{
				utf8Append(out, kEntities[k].cp);
				return pos + 1 + len;
			}
		}
	}

	out += '&';
	return pos + 1;
}

//-----------------------------------------------------------------------------
// Elements a contenu brut (pas de balises interpretées à l'interieur)
//-----------------------------------------------------------------------------
static bool isRawTag(const std::string& tag)
{
	return tag == "script" || tag == "style" || tag == "textarea" || tag == "pre" ||
		tag == "title" || tag == "xmp";
}

//-----------------------------------------------------------------------------
// Elements vides (void)
//-----------------------------------------------------------------------------
static bool isVoidTag(const std::string& tag)
{
	return tag == "br" || tag == "hr" || tag == "img" || tag == "input" ||
		tag == "meta" || tag == "link" || tag == "source" || tag == "wbr" ||
		tag == "col" || tag == "area" || tag == "base";
}

//-----------------------------------------------------------------------------
// Regles de fermeture implicite (HTML5 tolérant)
//-----------------------------------------------------------------------------
static bool autoCloses(const std::string& open, const std::string& newTag)
{
	if (open == "p") return true;
	if (open == "li") return newTag == "li";
	if (open == "tr" || open == "td" || open == "th") return newTag == "tr" || newTag == "td" || newTag == "th";
	if (open == "dt" || open == "dd") return newTag == "dt" || newTag == "dd";
	return false;
}

//-----------------------------------------------------------------------------
// Parser principal
//-----------------------------------------------------------------------------
bool htmlParse(const std::string& html, Document& doc)
{
	doc.clear();
	int root = doc.addNode(Node{ NodeType::Document, "", "", {}, -1, {} });
	doc.root = root;

	// Noeud <html> implicite
	int htmlNode = doc.createElement(root, "html");
	int headNode = -1;
	int bodyNode = -1;

	size_t i = 0;
	const size_t n = html.size();

	// Supprime le BOM
	if (n >= 3 && (unsigned char)html[0] == 0xEF && (unsigned char)html[1] == 0xBB && (unsigned char)html[2] == 0xBF)
		i = 3;

	std::vector<int> stack;
	stack.push_back(root);
	stack.push_back(htmlNode);

	std::string textBuf;
	auto flushText = [&]()
	{
		if (!textBuf.empty())
		{
			int cur = stack.back();
			doc.createText(cur, textBuf);
			textBuf.clear();
		}
	};

	enum ParseState { ST_DATA, ST_RAW, ST_COMMENT };
	ParseState state = ST_DATA;
	std::string rawTag;

	while (i < n)
	{
		if (doc.nodes.size() >= NW_MAX_DOM_NODES)
		{
			doc.clear();
			return false;
		}

		if (state == ST_DATA)
		{
			char c = html[i];
			if (c == '<')
			{
				if (html.compare(i, 4, "<!--") == 0) { state = ST_COMMENT; i += 4; continue; }
				if (html.compare(i, 9, "<!doctype") == 0 || html.compare(i, 2, "<!") == 0)
				{
					size_t j = html.find('>', i);
					i = (j == std::string::npos) ? n : j + 1;
					continue;
				}
				if (html.compare(i, 2, "</") == 0)
				{
					// Balise fermante
					flushText();
					size_t j = i + 2;
					std::string tag;
					while (j < n && html[j] != '>' && html[j] != '<' && html[j] != ' ' && html[j] != '\t' && html[j] != '\n')
						tag += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
					while (j < n && html[j] != '>') j++;
					i = j < n ? j + 1 : n;
					if (tag.empty()) continue;
					for (int k = (int)stack.size() - 1; k >= 1; k--)
						if (doc.nodes[stack[k]].tag == tag) { stack.resize(k); break; }
					continue;
				}

				// Balise ouvrante
				size_t j = i + 1;
				std::string tag;
				while (j < n && html[j] != '>' && html[j] != '/' && html[j] > ' ' && html[j] != '=')
					tag += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
				if (tag.empty()) { i++; continue; }
				flushText();

				// Attributs
				std::vector<Attribute> attrs;
				while (j < n && html[j] != '>')
				{
					while (j < n && (html[j] == ' ' || html[j] == '\t' || html[j] == '\n' || html[j] == '\r')) j++;
					if (j >= n || html[j] == '>') break;
					if (html[j] == '/') { j++; break; }
					std::string aname;
					while (j < n && html[j] != '=' && html[j] != '>' && html[j] > ' ' && html[j] != '/')
						aname += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
					while (j < n && (html[j] == ' ' || html[j] == '\t')) j++;
					std::string aval;
					if (j < n && html[j] == '=')
					{
						j++;
						while (j < n && (html[j] == ' ' || html[j] == '\t')) j++;
						char q = 0;
						if (j < n && (html[j] == '"' || html[j] == '\'')) { q = html[j]; j++; }
						while (j < n)
						{
							if (q && html[j] == q) break;
							if (!q && (html[j] == '>' || html[j] == ' ' || html[j] == '\t')) break;
							if (html[j] == '&') { j = htmlDecodeEntity(html, j, aval); continue; }
							aval += html[j++];
						}
						if (q && j < n && html[j] == q) j++;
					}
					if (!aname.empty()) attrs.push_back({ aname, aval });
				}
				i = j < n ? j + 1 : n;

				// Gestion structurelle html/head/body
				int parentForNew = stack.back();
				if (tag == "head" || tag == "body" || tag == "html")
				{
					if (tag == "html") { parentForNew = htmlNode; }
					else if (tag == "head")
					{
						if (headNode < 0) headNode = doc.createElement(htmlNode, "head");
						parentForNew = headNode;
						stack.resize(1);
						stack.push_back(headNode);
					}
					else if (tag == "body")
					{
						if (bodyNode < 0) bodyNode = doc.createElement(htmlNode, "body");
						parentForNew = bodyNode;
						stack.resize(1);
						stack.push_back(bodyNode);
					}
				}
				else
				{
					// title/meta/link/style/script avant le body : head implicite
					bool headish = (tag == "title" || tag == "meta" || tag == "link" ||
						tag == "base" || tag == "style" || tag == "script");
					if (headish && bodyNode < 0 && stack.size() <= 2)
					{
						if (headNode < 0) headNode = doc.createElement(htmlNode, "head");
						parentForNew = headNode;
					}
				}

				int node = doc.createElement(parentForNew, tag);
				for (const auto& a : attrs) doc.setAttr(node, a.name, a.value);
				if (isVoidTag(tag)) continue;

				// Fermeture implicite des blocs precedents
				while (stack.size() > 1)
				{
					int top = stack.back();
					if (doc.nodes[top].tag == tag && doc.nodes[top].tag == "p") { stack.pop_back(); continue; }
					if (autoCloses(doc.nodes[top].tag, tag)) { stack.pop_back(); continue; }
					break;
				}

				stack.push_back(node);
				if (isRawTag(tag)) { state = ST_RAW; rawTag = tag; }
				continue;
			}

			// Texte
			if (c == '&') { i = htmlDecodeEntity(html, i, textBuf); continue; }
			textBuf += c;
			i++;
		}
		else if (state == ST_RAW)
		{
			std::string close = "</" + rawTag + ">";
			size_t p = html.find(close, i);
			int cur = stack.back();
			if (cur != root && rawTag != "script")
			{
				const size_t end = p == std::string::npos ? n : p;
				doc.createText(cur, html.substr(i, end - i));
			}
			if (p == std::string::npos) { i = n; state = ST_DATA; }
			else
			{
				i = p + close.size();
				for (int k = (int)stack.size() - 1; k >= 1; k--)
					if (doc.nodes[stack[k]].tag == rawTag) { stack.resize(k); break; }
				state = ST_DATA;
			}
		}
		else // ST_COMMENT
		{
			size_t p = html.find("-->", i);
			i = (p == std::string::npos) ? n : p + 3;
			state = ST_DATA;
		}
	}
	flushText();
	return doc.nodes.size() > 1;
}

//--------------------------------------------------------------------------
// Test unitaire du parser (+ quelques entites)
//--------------------------------------------------------------------------
int htmlSelfTest()
{
	int fails = 0;
	{
		Document doc;
		bool ok = htmlParse(
			"<html><head><title>Test</title></head>"
			"<body><p>Salut &amp; &eacute; &#65;<a href=\"https://x.org\">lien</a></p>"
			"<img src=\"a.png\"><br>fin</body></html>", doc);
		if (!ok) fails++;
		if (doc.title() != "Test") fails++;
		if (doc.nodes.size() < 10) fails++;
	}
	{
		Document doc;
		htmlParse("<div> x </div>", doc);
		if (doc.empty()) fails++;
	}
	{
		std::string out;
		std::string s = "a&#x41;b";
		(void)htmlDecodeEntity(s, 1, out);
		if (out != "aAb") fails++;
	}
	{
		Document doc;
		htmlParse("<p>a<p>b", doc); // auto-closing p
		int pCount = 0;
		for (const auto& nd : doc.nodes)
			if (nd.isElement() && nd.tag == "p") pCount++;
		if (pCount != 2) fails++;
	}
	{
		Document doc;
		htmlParse("<html><body><script>large script text</script><p>visible</p></body></html>", doc);
		int body = doc.firstByTag(doc.root, "body");
		if (body < 0 || doc.textOf(body) != "visible") fails++;
	}
	{
		std::string html;
		for (int i = 0; i <= NW_MAX_DOM_NODES; i++)
			html += "<i></i>";
		Document doc;
		if (htmlParse(html, doc) || !doc.empty()) fails++;
	}
	{
		std::string html = "<div>";
		for (int i = 0; i < 1000; i++) html += "<section>";
		html += "<title>Deep title</title>";
		for (int i = 0; i < 1001; i++) html += "</section>";
		html += "</div>";
		Document doc;
		if (!htmlParse(html, doc) || doc.title() != "Deep title") fails++;
	}
	return fails;
}