#include "parser.hpp"
#include <stdio.h>

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
	{ "szlig", 0x00DF }, { "rarr", 0x2192 }, { "larr", 0x2190 }, { "uarr", 0x2191 },
	{ "darr", 0x2193 }, { "hearts", 0x2665 }, { "thinsp", 0x2009 }, { "ensp", 0x2002 },
	{ "emsp", 0x2003 }, { "check", 0x2713 }, { "le", 0x2264 }, { "ge", 0x2265 },
	{ "ne", 0x2260 }, { "asymp", 0x2248 }, { "infin", 0x221E },
	{ nullptr, 0 }
};

//-----------------------------------------------------------------------------
// Decodage des entites a partir de 'pos' (pos doit pointer sur le '&')
// Si requireSemicolon est vrai (cas des attributs), n'accepte que les entites
// terminees par un point-virgule afin de ne pas corrompre les URLs (&section= etc)
//-----------------------------------------------------------------------------
size_t htmlDecodeEntity(const std::string& s, size_t pos, std::string& out, bool requireSemicolon)
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
			bool hasSemi = (i < n && s[i] == ';');
			if (hasSemi) i++;
			if (requireSemicolon && !hasSemi)
			{
				out += '&';
				return pos + 1;
			}
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
		if (pos + 1 + len < n && s.compare(pos + 1, len, name) == 0)
		{
			if (s[pos + 1 + len] == ';')
			{
				utf8Append(out, kEntities[k].cp);
				return pos + 1 + len + 1;
			}
			if (!requireSemicolon && len >= 2) // sans point-virgule (uniquement hors attributs)
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
// Note: pre, textarea et title decodent les entites et ne sont plus traites en brut.
//-----------------------------------------------------------------------------
static bool isRawTag(const std::string& tag)
{
	return tag == "script" || tag == "style" || tag == "xmp";
}

//-----------------------------------------------------------------------------
// Elements vides (void)
//-----------------------------------------------------------------------------
static bool isVoidTag(const std::string& tag)
{
	return tag == "br" || tag == "hr" || tag == "img" || tag == "input" ||
		tag == "meta" || tag == "link" || tag == "source" || tag == "embed" || tag == "wbr" ||
		tag == "col" || tag == "area" || tag == "base";
}

//-----------------------------------------------------------------------------
// Regles de fermeture implicite (HTML5 tolérant)
//-----------------------------------------------------------------------------
static bool isBlockStart(const std::string& tag)
{
	static const char* blocks[] =
	{
		"address", "article", "aside", "blockquote", "body", "center", "dd", "div",
		"dl", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
		"h3", "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p",
		"pre", "section", "select", "table", "textarea", "ul", nullptr
	};
	for (int i = 0; blocks[i]; ++i)
		if (tag == blocks[i]) return true;
	return false;
}

static bool autoCloses(const std::string& open, const std::string& newTag)
{
	if (open == "p") return isBlockStart(newTag);
	if (open == "li") return newTag == "li";
	if (open == "tr" || open == "td" || open == "th") return newTag == "tr" || newTag == "td" || newTag == "th";
	if (open == "dt" || open == "dd") return newTag == "dt" || newTag == "dd";
	return false;
}

static bool rawCloseAt(const std::string& html, size_t pos, const std::string& tag)
{
	if (pos + tag.size() + 2 >= html.size() || html[pos] != '<' || html[pos + 1] != '/')
		return false;
	for (size_t i = 0; i < tag.size(); ++i)
	{
		char c = html[pos + 2 + i];
		if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		if (c != tag[i]) return false;
	}
	size_t end = pos + 2 + tag.size();
	while (end < html.size() &&
		(html[end] == ' ' || html[end] == '\t' || html[end] == '\r' || html[end] == '\n'))
		++end;
	return end < html.size() && html[end] == '>';
}

static size_t findRawClose(const std::string& html, size_t start, const std::string& tag)
{
	size_t pos = start;
	while ((pos = html.find('<', pos)) != std::string::npos)
	{
		if (rawCloseAt(html, pos, tag)) return pos;
		++pos;
	}
	return std::string::npos;
}

static bool isIgnoredSubtree(const std::string& tag)
{
	return tag == "script" || tag == "svg" || tag == "template" ||
		tag == "iframe" || tag == "canvas" || tag == "video" || tag == "audio" ||
		tag == "object" || tag == "embed" || tag == "picture" || tag == "source";
}

static bool isHiddenElement(const std::vector<Attribute>& attrs)
{
	for (const auto& attr : attrs)
	{
		if (attr.name == "hidden") return true;
		if (attr.name == "aria-hidden" && toLower(strTrim(attr.value)) == "true") return true;
		if (attr.name == "style")
		{
			std::string compact;
			compact.reserve(attr.value.size());
			for (char c : toLower(attr.value))
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n') compact += c;
			if (compact.find("display:none") != std::string::npos) return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Parser principal
//-----------------------------------------------------------------------------
bool htmlParse(const std::string& html, Document& doc)
{
	doc.clear();
	size_t estimate = html.size() / 40;
	if (estimate < 16) estimate = 16;
	if (estimate > NW_MAX_DOM_NODES) estimate = NW_MAX_DOM_NODES;
	doc.nodes.reserve(estimate);
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
			bool isPre = false;
			for (int sIdx : stack)
			{
				const std::string& t = doc.nodes[sIdx].tag;
				if (t == "pre" || t == "textarea")
				{
					isPre = true;
					break;
				}
			}
			if (!isPre)
			{
				bool onlyWs = true;
				for (char ch : textBuf)
				{
					if ((unsigned char)ch > ' ') { onlyWs = false; break; }
				}
				if (onlyWs)
				{
					textBuf.clear();
					return;
				}
			}
			doc.createText(cur, std::move(textBuf));
			textBuf.clear();
		}
	};

	enum ParseState { ST_DATA, ST_RAW, ST_COMMENT };
	ParseState state = ST_DATA;
	std::string rawTag;

	while (i < n)
	{
		// Keep the DOM accumulated so far; a large page should render partially
		// instead of being discarded as wholly invalid. Reserve room for a
		// pending text node and a structural element created in one iteration.
		if (doc.nodes.size() >= NW_MAX_DOM_NODES - 2)
		{
			i = n;
			break;
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

				bool selfClosing = false;
				// Attributs
				std::vector<Attribute> attrs;
				while (j < n && html[j] != '>')
				{
					while (j < n && (html[j] == ' ' || html[j] == '\t' || html[j] == '\n' || html[j] == '\r')) j++;
					if (j >= n || html[j] == '>') break;
					if (html[j] == '/')
					{
						selfClosing = true;
						j++;
						while (j < n && (html[j] == ' ' || html[j] == '\t' || html[j] == '\n' || html[j] == '\r')) j++;
						break;
					}
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
							if (!q && (html[j] == '>' || html[j] == ' ' || html[j] == '\t' || html[j] == '/')) break;
							if (html[j] == '&') { j = htmlDecodeEntity(html, j, aval, true); continue; }
							aval += html[j++];
						}
						if (q && j < n && html[j] == q) j++;
					}
					if (!aname.empty()) attrs.push_back({ aname, aval });
				}
				while (j < n && html[j] != '>')
				{
					if (html[j] == '/') selfClosing = true;
					j++;
				}
				i = j < n ? j + 1 : n;

				// Sauter les sous-arbres sans rendu ainsi que les elements masques.
				if (isIgnoredSubtree(tag) || isHiddenElement(attrs))
				{
					if (!selfClosing && !isVoidTag(tag))
					{
						size_t p = findRawClose(html, i, tag);
						if (p != std::string::npos)
						{
							i = p + tag.size() + 2;
							while (i < n && html[i] != '>') i++;
							if (i < n) i++;
						}
						else i = n;
					}
					continue;
				}

				// Un nouveau bloc ferme le paragraphe ouvert et ses descendants inline.
				if (isBlockStart(tag))
				{
					for (int k = (int)stack.size() - 1; k >= 1; --k)
					{
						if (doc.nodes[stack[k]].tag == "p")
						{
							stack.resize(k);
							break;
						}
					}
				}
				while (stack.size() > 1 && autoCloses(doc.nodes[stack.back()].tag, tag))
					stack.pop_back();

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
				if (isVoidTag(tag) || selfClosing) continue;

				stack.push_back(node);
				if (isRawTag(tag)) { state = ST_RAW; rawTag = tag; }
				continue;
			}

			// Texte
			if (c == '&') { i = htmlDecodeEntity(html, i, textBuf, false); continue; }
			textBuf += c;
			i++;
		}
		else if (state == ST_RAW)
		{
			size_t p = findRawClose(html, i, rawTag);
			int cur = stack.back();
			if (cur != root && rawTag != "script")
			{
				const size_t end = p == std::string::npos ? n : p;
				doc.createText(cur, html.substr(i, end - i));
			}
			if (p == std::string::npos) { i = n; state = ST_DATA; }
			else
			{
				i = p + rawTag.size() + 2;
				while (i < n &&
					(html[i] == ' ' || html[i] == '\t' || html[i] == '\r' || html[i] == '\n'))
					++i;
				++i;
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
#define HTML_SELF_FAIL() do { fails++; printf("HTML self-test failure at line %d\n", __LINE__); } while (0)
	{
		Document doc;
		bool ok = htmlParse(
			"<html><head><title>Test</title></head>"
			"<body><p>Salut &amp; &eacute; &#65;<a href=\"https://x.org\">lien</a></p>"
			"<img src=\"a.png\"><br>fin</body></html>", doc);
		if (!ok) HTML_SELF_FAIL();
		if (doc.title() != "Test") HTML_SELF_FAIL();
		if (doc.nodes.size() < 10) HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<div> x </div>", doc);
		if (doc.empty()) HTML_SELF_FAIL();
	}
	{
		std::string out = "a";
		std::string s = "a&#x41;b";
		const size_t end = htmlDecodeEntity(s, 1, out);
		out.append(s.substr(end));
		if (out != "aAb") HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<p>a<p>b", doc); // auto-closing p
		int pCount = 0;
		for (const auto& nd : doc.nodes)
			if (nd.isElement() && nd.tag == "p") pCount++;
		if (pCount != 2) HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<p>one<span>two</span><div>three</div>", doc);
		int p = doc.firstByTag(doc.root, "p");
		int span = doc.firstByTag(doc.root, "span");
		int div = doc.firstByTag(doc.root, "div");
		if (p < 0 || span < 0 || div < 0 || doc.nodes[span].parent != p ||
			doc.nodes[div].parent != doc.nodes[p].parent)
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<html><body><script>large script text</script><p>visible</p></body></html>", doc);
		int body = doc.firstByTag(doc.root, "body");
		if (body < 0 || doc.textOf(body) != "visible") HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<script>hidden</SCRIPT ><p>shown</p>", doc);
		if (doc.textOf(doc.root) != "shown") HTML_SELF_FAIL();
	}
	{
		std::string html;
		html = "<p>early text survives</p>";
		for (int i = 0; i < NW_MAX_DOM_NODES; i++)
			html += "<i>x</i>";
		Document doc;
		if (!htmlParse(html, doc) || doc.nodes.size() > NW_MAX_DOM_NODES ||
			doc.textOf(doc.root).find("early text survives") == std::string::npos)
			HTML_SELF_FAIL();
	}
	{
		std::string html = "<div>";
		for (int i = 0; i < 1000; i++) html += "<section>";
		html += "<title>Deep title</title>";
		for (int i = 0; i < 1001; i++) html += "</section>";
		html += "</div>";
		Document doc;
		if (!htmlParse(html, doc) || doc.title() != "Deep title") HTML_SELF_FAIL();
	}
	{
		// Test entites dans attributs : &section=2 ne doit PAS devenir §ion=2
		Document doc;
		htmlParse("<a href=\"/test?a=1&section=2&copy=3&amp;real=1\">link</a>", doc);
		int a = doc.firstByTag(doc.root, "a");
		if (a < 0) HTML_SELF_FAIL();
		else {
			const char* h = doc.nodes[a].getAttr("href");
			if (!h || std::string(h) != "/test?a=1&section=2&copy=3&real=1") HTML_SELF_FAIL();
		}
	}
	{
		// Test self-closing et svg skip
		Document doc;
		htmlParse("<div><svg><path d=\"M0 0\"/><circle r=\"5\"/></svg><p>apres</p></div>", doc);
		int p = doc.firstByTag(doc.root, "p");
		if (p < 0 || doc.textOf(p) != "apres") HTML_SELF_FAIL();
		if (doc.firstByTag(doc.root, "path") >= 0) HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<body><iframe><p>iframe</p></iframe><canvas><b>canvas</b></canvas>"
			"<video><p>video</p></video><audio><p>audio</p></audio>"
			"<object><p>object</p></object><embed src='hidden'>"
			"<picture><img src='hidden.png'><source src='hidden.jpg'></picture>"
			"<div hidden><b>hidden</b></div>"
			"<div aria-hidden='true'><b>aria hidden</b></div>"
			"<div style='display: none'><b>style hidden</b></div><p>visible</p></body>", doc);
		static const char* ignored[] = {
			"iframe", "canvas", "video", "audio", "object", "embed", "picture",
			"source", "img"
		};
		for (const char* tag : ignored)
			if (doc.firstByTag(doc.root, tag) >= 0)
			{
				printf("Parseur: balise masquee conservee: %s\n", tag);
				HTML_SELF_FAIL();
			}
		int body = doc.firstByTag(doc.root, "body");
		if (body < 0 || doc.textOf(body) != "visible")
		{
			printf("Parseur: texte apres elements ignores: [%s]\n",
				body < 0 ? "body manquant" : doc.textOf(body).c_str());
			HTML_SELF_FAIL();
		}
	}
	{
		// Test <pre> avec balises internes et entites
		Document doc;
		htmlParse("<pre><code>&lt;hello&gt;</code></pre>", doc);
		int code = doc.firstByTag(doc.root, "code");
		if (code < 0 || doc.textOf(code) != "<hello>") HTML_SELF_FAIL();
	}
#undef HTML_SELF_FAIL
	return fails;
}
