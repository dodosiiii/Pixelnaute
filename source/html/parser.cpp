#include "parser.hpp"
#include "html_entities.hpp"
#include <stdio.h>
#include <string_view>

static bool isHtmlSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

//-----------------------------------------------------------------------------
// Decode les references numeriques et nommees, avec la regle de protection
// contre les prefixe sans point-virgule ambigus dans les attributs.
//-----------------------------------------------------------------------------
size_t htmlDecodeEntity(const std::string& s, size_t pos, std::string& out, bool inAttribute)
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
		bool outOfRange = false;
		while (i < n)
		{
			char c = s[i];
			int d;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + (c - 'a');
			else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + (c - 'A');
			else break;
			if (!outOfRange)
			{
				if (val > (0x10FFFFu - (uint32_t)d) / (uint32_t)base)
					outOfRange = true;
				else
					val = val * (uint32_t)base + (uint32_t)d;
			}
			any = true;
			i++;
		}
		if (any)
		{
			bool hasSemi = (i < n && s[i] == ';');
			if (hasSemi) i++;
			static const uint16_t c1Replacements[32] = {
				0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021,
				0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
				0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
				0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178
			};
			if (outOfRange || val == 0 || val > 0x10FFFF ||
				(val >= 0xD800 && val <= 0xDFFF))
				val = 0xFFFD;
			else if (val >= 0x80 && val <= 0x9F)
				val = c1Replacements[val - 0x80];
			utf8Append(out, val);
			return i;
		}
	}

	// WHATWG entity names are sorted for a bounded longest-prefix lookup.
	const size_t nameStart = pos + 1;
	size_t maxLength = std::min((size_t)32, n - nameStart);
	while (maxLength > 0)
	{
		const std::string_view candidate(s.data() + nameStart, maxLength);
		const HtmlNamedEntity* first = kHtmlNamedEntities;
		const HtmlNamedEntity* last = kHtmlNamedEntities +
			(sizeof(kHtmlNamedEntities) / sizeof(kHtmlNamedEntities[0]));
		const HtmlNamedEntity* found = std::lower_bound(first, last, candidate,
			[](const HtmlNamedEntity& entity, std::string_view value) {
				return std::string_view(entity.name) < value;
			});
		if (found != last && std::string_view(found->name) == candidate)
		{
			const size_t afterName = nameStart + maxLength;
			const bool hasSemicolon = afterName < n && s[afterName] == ';';
			const char next = afterName < n ? s[afterName] : '\0';
			const bool attributeAmbiguity = inAttribute &&
				((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
				 (next >= '0' && next <= '9') || next == '=');
			if (hasSemicolon || (found->legacy && !attributeAmbiguity))
			{
				out += found->value;
				return afterName + (hasSemicolon ? 1 : 0);
			}
		}
		--maxLength;
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
		tag == "col" || tag == "area" || tag == "base" || tag == "track" ||
		tag == "param";
}

//-----------------------------------------------------------------------------
// Regles de fermeture implicite (HTML5 tolérant)
//-----------------------------------------------------------------------------
static bool isBlockStart(const std::string& tag)
{
	static const char* blocks[] =
	{
		"address", "article", "aside", "blockquote", "body", "center", "dd", "div",
		"details", "dialog", "dir", "dl", "dt", "fieldset", "figcaption", "figure",
		"footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup",
		"hr", "listing", "main", "menu", "nav", "ol", "p", "pre", "search",
		"section", "table", "ul", nullptr
	};
	for (int i = 0; blocks[i]; ++i)
		if (tag == blocks[i]) return true;
	return false;
}

static bool isHeadingTag(const std::string& tag)
{
	return tag == "h1" || tag == "h2" || tag == "h3" ||
		tag == "h4" || tag == "h5" || tag == "h6";
}

static bool autoCloses(const std::string& open, const std::string& newTag)
{
	if (open == "p") return isBlockStart(newTag);
	if (open == "li") return newTag == "li";
	if (open == "tr") return newTag == "tr";
	if (open == "td" || open == "th")
		return newTag == "tr" || newTag == "td" || newTag == "th";
	if (open == "dt" || open == "dd") return newTag == "dt" || newTag == "dd";
	if (open == "option") return newTag == "option" || newTag == "optgroup";
	if (open == "optgroup") return newTag == "optgroup";
	if (open == "rt" || open == "rp") return newTag == "rt" || newTag == "rp";
	return false;
}

static void closeHeadingInScope(Document& doc, std::vector<int>& stack)
{
	for (int i = (int)stack.size() - 1; i >= 1; --i)
	{
		const std::string& tag = doc.nodes[(size_t)stack[(size_t)i]].tag;
		if (isHeadingTag(tag))
		{
			stack.resize((size_t)i);
			return;
		}
		if (tag == "button" || tag == "html" || tag == "table" ||
			tag == "td" || tag == "th" || tag == "template")
			return;
	}
}

static int tableContext(const Document& doc, const std::vector<int>& stack)
{
	int table = -1;
	for (size_t i = stack.size(); i > 0; --i)
	{
		const std::string& tag = doc.nodes[(size_t)stack[i - 1]].tag;
		if (tag == "td" || tag == "th") return -1;
		if (tag == "table") { table = (int)i - 1; break; }
	}
	return table;
}

static bool tableStructureTag(const std::string& tag)
{
	return tag == "caption" || tag == "col" || tag == "colgroup" ||
		tag == "tbody" || tag == "tfoot" || tag == "thead" ||
		tag == "tr" || tag == "td" || tag == "th" || tag == "style" ||
		tag == "script" || tag == "template";
}

static void closeInScope(Document& doc, std::vector<int>& stack,
	const char* target, const char* boundaryA = nullptr,
	const char* boundaryB = nullptr)
{
	for (int i = (int)stack.size() - 1; i >= 1; --i)
	{
		const std::string& tag = doc.nodes[(size_t)stack[(size_t)i]].tag;
		if (tag == target)
		{
			stack.resize((size_t)i);
			return;
		}
		if ((boundaryA && tag == boundaryA) ||
			(boundaryB && tag == boundaryB))
			return;
	}
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
	return end < html.size() && (html[end] == '>' || html[end] == '/' ||
		isHtmlSpace(html[end]));
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
	return tag == "svg" || tag == "template" ||
		tag == "iframe" || tag == "object" || tag == "embed";
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
	Node documentNode;
	documentNode.type = NodeType::Document;
	int root = doc.addNode(std::move(documentNode));
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
			const int tableIndex = tableContext(doc, stack);
			const int tableNode = tableIndex >= 0
				? stack[(size_t)tableIndex] : -1;
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
				if (!onlyWs && cur == htmlNode && bodyNode < 0)
				{
					bodyNode = doc.createElement(htmlNode, "body");
					stack.resize(2);
					stack.push_back(bodyNode);
					cur = bodyNode;
				}
				bool previousInline = false;
				const auto& siblings = doc.nodes[(size_t)cur].children;
				if (!siblings.empty())
				{
					const Node& previous = doc.nodes[(size_t)siblings.back()];
					previousInline = previous.type == NodeType::Text ||
						(previous.isElement() && !isBlockStart(previous.tag));
				}
				if (onlyWs && (tableIndex >= 0 || !previousInline))
				{
					textBuf.clear();
					return;
				}
				if (onlyWs) textBuf = " ";
			}
			if (tableIndex >= 0 && tableNode >= 0)
			{
				const int tableParent = doc.nodes[(size_t)tableNode].parent;
				if (tableParent >= 0)
				{
					const int textNode = doc.createText(tableParent,
						std::move(textBuf));
					doc.insertChild(tableParent, textNode, tableNode);
				}
				else
					doc.createText(cur, std::move(textBuf));
			}
			else
				doc.createText(cur, std::move(textBuf));
			textBuf.clear();
		}
	};

	enum ParseState { ST_DATA, ST_RAW, ST_RCDATA, ST_PLAINTEXT, ST_COMMENT };
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
					while (j < n && html[j] != '>' && html[j] != '<' &&
						!isHtmlSpace(html[j]))
						tag += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
					while (j < n && html[j] != '>') j++;
					i = j < n ? j + 1 : n;
					if (tag.empty()) continue;
					if (tag == "body" || tag == "html") continue;
					for (int k = (int)stack.size() - 1; k >= 1; k--)
						if (doc.nodes[stack[k]].tag == tag) { stack.resize(k); break; }
					continue;
				}

				// Balise ouvrante
				size_t j = i + 1;
				std::string tag;
				while (j < n && html[j] != '>' && html[j] != '/' &&
					!isHtmlSpace(html[j]) && html[j] != '=')
					tag += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
				if (tag.empty()) { textBuf += '<'; i++; continue; }
				flushText();

				bool selfClosing = false;
				// Attributs
				std::vector<Attribute> attrs;
				while (j < n && html[j] != '>')
				{
					while (j < n && isHtmlSpace(html[j])) j++;
					if (j >= n || html[j] == '>') break;
					if (html[j] == '/' && j + 1 < n && html[j + 1] == '>')
					{
						selfClosing = true;
						j++;
						break;
					}
					std::string aname;
					while (j < n && html[j] != '=' && html[j] != '>' &&
						!isHtmlSpace(html[j]))
						aname += (char)(html[j] >= 'A' && html[j] <= 'Z' ? html[j] + 32 : html[j]), j++;
					while (j < n && isHtmlSpace(html[j])) j++;
					std::string aval;
					if (j < n && html[j] == '=')
					{
						j++;
						while (j < n && isHtmlSpace(html[j])) j++;
						char q = 0;
						if (j < n && (html[j] == '"' || html[j] == '\'')) { q = html[j]; j++; }
						while (j < n)
						{
							if (q && html[j] == q) break;
							if (!q && (html[j] == '>' || isHtmlSpace(html[j])))
								break;
							if (html[j] == '&') { j = htmlDecodeEntity(html, j, aval, true); continue; }
							aval += html[j++];
						}
						if (q && j < n && html[j] == q) j++;
					}
					if (!aname.empty())
					{
						bool duplicate = false;
						for (const Attribute& attr : attrs)
							if (attr.name == aname) { duplicate = true; break; }
						if (!duplicate) attrs.push_back({ aname, aval });
					}
				}
				i = j < n ? j + 1 : n;

				// Le layout filtre les elements masques afin que le DOM reste
				// modifiable par JavaScript.
				if (isIgnoredSubtree(tag))
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

				// Fermer les elements dont l'ouverture implicite est courante en HTML.
				if (tag == "li")
					closeInScope(doc, stack, "li", "ul", "ol");
				else if (tag == "dt" || tag == "dd")
				{
					closeInScope(doc, stack, "dt", "dl");
					closeInScope(doc, stack, "dd", "dl");
				}
				else if (tag == "option")
					closeInScope(doc, stack, "option", "select", "datalist");
				else if (tag == "optgroup")
				{
					closeInScope(doc, stack, "option", "select", "datalist");
					closeInScope(doc, stack, "optgroup", "select", "datalist");
				}
				else if (tag == "tr")
				{
					closeInScope(doc, stack, "td", "table");
					closeInScope(doc, stack, "th", "table");
					closeInScope(doc, stack, "tr", "table");
				}
				else if (tag == "td" || tag == "th")
				{
					closeInScope(doc, stack, "td", "tr", "table");
					closeInScope(doc, stack, "th", "tr", "table");
				}
				else if (tag == "button")
					closeInScope(doc, stack, "button");
				else if (tag == "a")
					closeInScope(doc, stack, "a");
				else if (tag == "form")
					closeInScope(doc, stack, "form");
				if (isHeadingTag(tag))
					closeHeadingInScope(doc, stack);

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
				if (tag == "li")
				{
					for (int k = (int)stack.size() - 1; k >= 1; --k)
					{
						const std::string& stag = doc.nodes[stack[k]].tag;
						if (stag == "li")
						{
							stack.resize(k);
							break;
						}
						if (stag == "ul" || stag == "ol") break;
					}
				}
				while (stack.size() > 1 && autoCloses(doc.nodes[stack.back()].tag, tag))
					stack.pop_back();

				int tableIndex = tableContext(doc, stack);
				if (tableIndex >= 0 && (tag == "tr" || tag == "td" || tag == "th"))
				{
					int tableNode = stack[(size_t)tableIndex];
					bool hasSection = false;
					bool hasRow = false;
					for (size_t k = (size_t)tableIndex + 1; k < stack.size(); ++k)
					{
						const std::string& openTag =
							doc.nodes[(size_t)stack[k]].tag;
						if (openTag == "tbody" || openTag == "thead" ||
							openTag == "tfoot")
							hasSection = true;
						if (openTag == "tr") hasRow = true;
					}
					if (!hasSection)
					{
						stack.resize((size_t)tableIndex + 1);
						const int section = doc.createElement(tableNode, "tbody");
						stack.push_back(section);
					}
					if ((tag == "td" || tag == "th") && !hasRow)
					{
						const int row = doc.createElement(stack.back(), "tr");
						stack.push_back(row);
					}
					tableIndex = tableContext(doc, stack);
				}

				// Gestion structurelle html/head/body
				int parentForNew = stack.back();
				int structuralNode = -1;
				bool insertBeforeTable = false;
				int fosterTable = -1;
				if (tableIndex >= 0 && !tableStructureTag(tag))
				{
					fosterTable = stack[(size_t)tableIndex];
					parentForNew = doc.nodes[(size_t)fosterTable].parent;
					insertBeforeTable = parentForNew >= 0;
				}
				if (tag == "head" || tag == "body" || tag == "html")
				{
					if (tag == "html")
					{
						structuralNode = htmlNode;
					}
					else if (tag == "head")
					{
						if (headNode < 0) headNode = doc.createElement(htmlNode, "head");
						structuralNode = headNode;
						if (bodyNode < 0 &&
							std::find(stack.begin(), stack.end(), headNode) == stack.end())
						{
							stack.resize(2);
							stack.push_back(headNode);
						}
					}
					else if (tag == "body")
					{
						if (bodyNode < 0) bodyNode = doc.createElement(htmlNode, "body");
						structuralNode = bodyNode;
						if (std::find(stack.begin(), stack.end(), bodyNode) == stack.end())
						{
							stack.resize(2);
							stack.push_back(bodyNode);
						}
					}
				}
				else
				{
					// title/meta/link/style/script avant le body : head implicite
					bool headish = (tag == "title" || tag == "meta" || tag == "link" ||
						tag == "base" || tag == "style" || tag == "script");
					if (headish && bodyNode < 0)
					{
						if (headNode < 0) headNode = doc.createElement(htmlNode, "head");
						parentForNew = headNode;
					}
					else if (!headish && bodyNode < 0)
					{
						if (headNode >= 0)
						{
							for (size_t k = 1; k < stack.size(); ++k)
								if (stack[k] == headNode)
								{
									stack.resize(k);
									break;
								}
						}
						if (bodyNode < 0) bodyNode = doc.createElement(htmlNode, "body");
						stack.resize(2);
						stack.push_back(bodyNode);
						parentForNew = bodyNode;
					}
				}

				int node = structuralNode >= 0
					? structuralNode : doc.createElement(parentForNew, tag);
				if (insertBeforeTable && structuralNode < 0)
					doc.insertChild(parentForNew, node, fosterTable);
				for (const auto& a : attrs) doc.setAttr(node, a.name, a.value);
				if (structuralNode >= 0) continue;
				if (isVoidTag(tag)) continue;

				stack.push_back(node);
				if (isRawTag(tag)) { state = ST_RAW; rawTag = tag; }
				else if (tag == "title" || tag == "textarea")
				{
					state = ST_RCDATA;
					rawTag = tag;
				}
				else if (tag == "plaintext")
					state = ST_PLAINTEXT;
				continue;
			}

			// Texte
			if (c == '&') { i = htmlDecodeEntity(html, i, textBuf, false); continue; }
			textBuf += c;
			i++;
		}
		else if (state == ST_PLAINTEXT)
		{
			const int cur = stack.back();
			if (cur != root)
				doc.createText(cur, html.substr(i));
			i = n;
			state = ST_DATA;
		}
		else if (state == ST_RAW || state == ST_RCDATA)
		{
			size_t p = findRawClose(html, i, rawTag);
			int cur = stack.back();
			if (cur != root)
			{
				const size_t end = p == std::string::npos ? n : p;
				if (state == ST_RAW)
					doc.createText(cur, html.substr(i, end - i));
				else
				{
					std::string value;
					size_t at = i;
					while (at < end)
					{
						if (html[at] == '&')
							at = htmlDecodeEntity(html, at, value);
						else
							value += html[at++];
					}
					if (rawTag == "textarea" && !value.empty() &&
						value[0] == '\n')
						value.erase(0, 1);
					else if (rawTag == "textarea" && value.size() >= 2 &&
						value[0] == '\r' && value[1] == '\n')
						value.erase(0, 2);
					doc.createText(cur, std::move(value));
				}
			}
			if (p == std::string::npos) { i = n; state = ST_DATA; }
			else
			{
				i = p + rawTag.size() + 2;
				while (i < n && html[i] != '>') ++i;
				if (i < n) ++i;
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
		const char* invalidEntities[] = {
			"&#4294967330;", "&#999999999999999999999999999999999999;",
			"&#x110000;", "&#xffffffffffffffffffffffffffffffff;"
		};
		for (const char* entity : invalidEntities)
		{
			const std::string input(entity);
			std::string out;
			const size_t end = htmlDecodeEntity(input, 0, out);
			if (end != input.size() || out != "\xEF\xBF\xBD")
				HTML_SELF_FAIL();
		}
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
		int script = doc.firstByTag(doc.root, "script");
		if (body < 0 || script < 0 || doc.textOf(script) != "large script text" ||
			doc.textOf(body) != "large script textvisible") HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<script>hidden</SCRIPT ><p>shown</p>", doc);
		int script = doc.firstByTag(doc.root, "script");
		if (script < 0 || doc.textOf(script) != "hidden" ||
			doc.textOf(doc.root).find("shown") == std::string::npos) HTML_SELF_FAIL();
	}
	{
		// Une balise fermante de texte brut peut contenir des attributs ignores.
		Document doc;
		htmlParse("<script>hidden</script data-extra='x'><p>shown</p>", doc);
		const int script = doc.firstByTag(doc.root, "script");
		const int paragraph = doc.firstByTag(doc.root, "p");
		if (script < 0 || doc.textOf(script) != "hidden" || paragraph < 0 ||
			doc.textOf(paragraph) != "shown")
			HTML_SELF_FAIL();
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
			"<picture><img src='kept.png'><source src='kept.jpg'></picture>"
			"<div hidden><b>hidden</b></div>"
			"<div aria-hidden='true'><b>aria hidden</b></div>"
			"<div style='display: none'><b>style hidden</b></div><p>visible</p></body>", doc);
		static const char* ignored[] = {
			"iframe", "object", "embed"
		};
		for (const char* tag : ignored)
			if (doc.firstByTag(doc.root, tag) >= 0)
			{
				printf("Parseur: balise masquee conservee: %s\n", tag);
				HTML_SELF_FAIL();
			}
		static const char* fallbackTags[] = { "canvas", "video", "audio" };
		for (size_t i = 0; i < sizeof(fallbackTags) / sizeof(fallbackTags[0]); ++i)
		{
			const int fallback = doc.firstByTag(doc.root, fallbackTags[i]);
			if (fallback < 0 ||
				doc.textOf(fallback).find(fallbackTags[i]) == std::string::npos)
				HTML_SELF_FAIL();
		}
		int body = doc.firstByTag(doc.root, "body");
		int hidden = doc.firstByTag(doc.root, "div");
		int image = doc.firstByTag(doc.root, "img");
		if (body < 0 || doc.textOf(body).find("visible") == std::string::npos ||
			hidden < 0 || !doc.nodes[hidden].hasAttr("hidden") || image < 0)
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
	{
		// Test '<' isole dans le texte
		Document doc;
		htmlParse("<p>a < b</p>", doc);
		int p = doc.firstByTag(doc.root, "p");
		if (p < 0 || doc.textOf(p) != "a < b") HTML_SELF_FAIL();
	}
	{
		// plaintext consomme le reste du document sans analyser les balises ni entites.
		Document doc;
		htmlParse("<plaintext><b>&amp;</b></plaintext><p>reste</p>", doc);
		const int plaintext = doc.firstByTag(doc.root, "plaintext");
		if (plaintext < 0 ||
			doc.textOf(plaintext) != "<b>&amp;</b></plaintext><p>reste</p>" ||
			doc.firstByTag(plaintext, "b") >= 0 ||
			doc.firstByTag(plaintext, "p") >= 0)
			HTML_SELF_FAIL();
	}
	{
		// Test attribut non-quote avec slash
		Document doc;
		htmlParse("<a href=/chemin>lien</a>", doc);
		int a = doc.firstByTag(doc.root, "a");
		if (a < 0) HTML_SELF_FAIL();
		else {
			const char* h = doc.nodes[a].getAttr("href");
			if (!h || std::string(h) != "/chemin") HTML_SELF_FAIL();
		}
	}
	{
		// Les doublons gardent la première valeur; '/' peut appartenir au nom.
		Document doc;
		htmlParse("<div DATA-value=first data-value=second a/b=c></div>", doc);
		const int div = doc.firstByTag(doc.root, "div");
		if (div < 0 ||
			std::string(doc.nodes[(size_t)div].getAttr("data-value") ?
				doc.nodes[(size_t)div].getAttr("data-value") : "") != "first" ||
			std::string(doc.nodes[(size_t)div].getAttr("a/b") ?
				doc.nodes[(size_t)div].getAttr("a/b") : "") != "c")
			HTML_SELF_FAIL();
	}
	{
		// Un slash avant '>' reste dans une valeur d'attribut non citee.
		Document doc;
		htmlParse("<div data-path=/assets/logo.svg/>suite</div>", doc);
		const int div = doc.firstByTag(doc.root, "div");
		const char* path = div >= 0
			? doc.nodes[(size_t)div].getAttr("data-path") : nullptr;
		if (!path || std::string(path) != "/assets/logo.svg/" ||
			doc.textOf(div) != "suite")
			HTML_SELF_FAIL();
	}
	{
		// Test entites sans point-virgule et protection des faux-positifs
		Document doc;
		htmlParse("<p>&region= &lesson &copy 2026</p>", doc);
		int p = doc.firstByTag(doc.root, "p");
		if (p < 0 || doc.textOf(p) != "\xc2\xaeion= &lesson \xc2\xa9 2026") HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<p>&NotEqualTilde; &CounterClockwiseContourIntegral; "
			"&eacute; &#128;</p>", doc);
		const int p = doc.firstByTag(doc.root, "p");
		const std::string expected =
			"\342\211\202\314\270 \342\210\263 \303\251 \342\202\254";
		if (p < 0 || doc.textOf(p) != expected) HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<x-card data-kind=demo><x-label>custom</x-label></x-card>", doc);
		const int card = doc.firstByTag(doc.root, "x-card");
		const int label = doc.firstByTag(doc.root, "x-label");
		if (card < 0 || label < 0 || doc.nodes[(size_t)label].parent != card ||
			doc.nodes[(size_t)card].getAttr("data-kind") == nullptr)
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<p><span>alpha</span> <span>beta</span></p>", doc);
		const int p = doc.firstByTag(doc.root, "p");
		if (p < 0 || doc.nodes[(size_t)p].children.size() != 3 ||
			doc.nodes[(size_t)doc.nodes[(size_t)p].children[1]].type != NodeType::Text ||
			doc.nodeText((int)doc.nodes[(size_t)p].children[1]) != " ")
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<title>A &NotEqualTilde; title</title><body>"
			"<textarea>\nA &amp; <b>literal</textarea></body>", doc);
		const int title = doc.firstByTag(doc.root, "title");
		const int textarea = doc.firstByTag(doc.root, "textarea");
		if (title < 0 || doc.textOf(title) !=
			"A \342\211\202\314\270 title" || textarea < 0 ||
			doc.textOf(textarea) != "A & <b>literal" ||
			doc.firstByTag(textarea, "b") >= 0)
			HTML_SELF_FAIL();
	}
	{
		// Test li auto-closing avec balise inline non-fermee
		Document doc;
		htmlParse("<ul><li><a href='#'><b>one<li>two</ul>", doc);
		int ul = doc.firstByTag(doc.root, "ul");
		if (ul < 0 || doc.nodes[ul].children.size() != 2) HTML_SELF_FAIL();
	}
	{
		// Un nouveau titre ferme le titre précédent, même avec un enfant inline ouvert.
		Document doc;
		htmlParse("<section><h1><span>Premier<h2>Second</section>", doc);
		const int section = doc.firstByTag(doc.root, "section");
		const int first = doc.firstByTag(doc.root, "h1");
		const int second = doc.firstByTag(doc.root, "h2");
		const int span = doc.firstByTag(doc.root, "span");
		if (section < 0 || first < 0 || second < 0 || span < 0 ||
			doc.nodes[(size_t)first].parent != section ||
			doc.nodes[(size_t)second].parent != section ||
			doc.nodes[(size_t)span].parent != first ||
			doc.textOf(first) != "Premier" || doc.textOf(second) != "Second")
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<select><option>one<option>two</select>", doc);
		const int select = doc.firstByTag(doc.root, "select");
		if (select < 0 || doc.nodes[(size_t)select].children.size() != 2 ||
			doc.nodes[(size_t)doc.nodes[(size_t)select].children[0]].tag != "option" ||
			doc.nodes[(size_t)doc.nodes[(size_t)select].children[1]].tag != "option")
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<body><table>loose<tr><td>A<td>B<tr><td>C</table></body>", doc);
		const int table = doc.firstByTag(doc.root, "table");
		const int body = doc.firstByTag(doc.root, "body");
		if (table < 0 || body < 0 ||
			doc.nodes[(size_t)body].children.size() < 2 ||
			doc.nodes[(size_t)doc.nodes[(size_t)body].children[0]].type != NodeType::Text ||
			doc.textOf(doc.nodes[(size_t)body].children[0]) != "loose")
		{
			printf("Table foster: table=%d body=%d children=%zu text0=%s\n",
				table, body, body >= 0 ? doc.nodes[(size_t)body].children.size() : 0,
				body >= 0 && !doc.nodes[(size_t)body].children.empty()
					? doc.textOf(doc.nodes[(size_t)body].children[0]).c_str() : "(none)");
			HTML_SELF_FAIL();
		}
		else
		{
			const Node& tableNode = doc.nodes[(size_t)table];
			const int section = tableNode.children.empty() ? -1 :
				tableNode.children[0];
			if (section < 0 || doc.nodes[(size_t)section].tag != "tbody" ||
				doc.nodes[(size_t)section].children.size() != 2)
				HTML_SELF_FAIL();
			else
			{
				const int firstRow = doc.nodes[(size_t)section].children[0];
				if (doc.nodes[(size_t)firstRow].tag != "tr" ||
					doc.nodes[(size_t)firstRow].children.size() != 2 ||
					doc.nodes[(size_t)doc.nodes[(size_t)firstRow].children[0]].tag != "td" ||
					doc.nodes[(size_t)doc.nodes[(size_t)firstRow].children[1]].tag != "td")
					HTML_SELF_FAIL();
			}
		}
	}
	{
		// Test contenu apres </body> conserve dans body
		Document doc;
		htmlParse("<body><p>one</p></body><p>two</p>", doc);
		const int body = doc.firstByTag(doc.root, "body");
		if (body < 0 || doc.nodes[(size_t)body].children.size() != 2 ||
			doc.nodes[(size_t)doc.nodes[(size_t)body].children[0]].tag == "body")
			HTML_SELF_FAIL();
	}
	{
		Document doc;
		htmlParse("<div/>one<span>two</span>", doc);
		const int div = doc.firstByTag(doc.root, "div");
		const int span = doc.firstByTag(doc.root, "span");
		if (div < 0 || span < 0 ||
			doc.textOf(div) != "onetwo" ||
			doc.nodes[(size_t)span].parent != div)
			HTML_SELF_FAIL();
	}
#undef HTML_SELF_FAIL
	return fails;
}
