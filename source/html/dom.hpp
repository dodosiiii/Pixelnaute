#pragma once
//=============================================================================
// dom.hpp - Arbre DOM leger et optimise memoire
//
// Les noeuds vivent dans un vecteur (arene) : aucun pointeur, index seulement.
// Cela evite toute allocation par noeud et rend le parcours tres rapide.
//=============================================================================
#include "../common.hpp"
#include <algorithm>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class NodeType
{
	Document,   // racine
	Element,    // <tag>
	Text,       // texte (entites deja decodees)
	Comment,    // <!-- ... -->
	RawText,    // contenu brut (script, style, pre)
};

struct Attribute
{
	std::string name;
	std::string value;
};

// Identifiant compact des balises HTML. Les noms sont partages dans une
// table statique au lieu d'etre stockes dans chaque noeud DOM.
class TagId
{
public:
	TagId() = default;
	TagId(const char* name) { set(name ? std::string(name) : std::string()); }
	TagId(const std::string& name) { set(name); }
	TagId& operator=(const std::string& name) { set(name); return *this; }
	TagId& operator=(const char* name) { set(name ? std::string(name) : std::string()); return *this; }

	operator const std::string&() const { return name(value_); }
	const char* c_str() const { return name(value_).c_str(); }
	char operator[](size_t index) const
	{
		const std::string& value = name(value_);
		return index < value.size() ? value[index] : '\0';
	}
	bool operator==(TagId other) const { return value_ == other.value_; }
	bool operator!=(TagId other) const { return value_ != other.value_; }
	bool operator==(const std::string& other) const { return matches(other); }
	bool operator!=(const std::string& other) const { return !matches(other); }
	bool operator==(const char* other) const { return other && matches(std::string(other)); }
	bool operator!=(const char* other) const { return !other || !matches(std::string(other)); }
	friend bool operator==(const std::string& lhs, TagId rhs) { return rhs.matches(lhs); }
	friend bool operator!=(const std::string& lhs, TagId rhs) { return !rhs.matches(lhs); }
	friend bool operator==(const char* lhs, TagId rhs) { return lhs && rhs.matches(std::string(lhs)); }
	friend bool operator!=(const char* lhs, TagId rhs) { return !lhs || !rhs.matches(std::string(lhs)); }

private:
	uint16_t value_ = 0;
	struct Registry
	{
		std::deque<std::string> names;
		std::unordered_map<std::string, uint16_t> ids;

		Registry()
		{
			static const char* const builtins[] = {
				"", "a", "abbr", "address", "area", "article", "aside", "audio", "b", "base", "blockquote",
				"body", "br", "button", "canvas", "center", "cite", "code", "col", "dd", "del", "details", "dialog",
				"div", "dl", "dt", "em", "embed", "fieldset", "figcaption", "figure", "footer", "form",
				"h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr", "html", "i",
				"iframe", "img", "input", "ins", "kbd", "label", "li", "link", "main", "meta", "nav", "ol",
				"object", "option", "p", "picture", "pre", "q", "s", "samp", "script", "section",
				"select", "small", "source", "span", "strong", "style", "sub", "summary", "sup", "svg",
				"table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "time", "title",
				"tr", "u", "ul", "var", "video", "wbr", "xmp"
			};
			for (const char* name : builtins)
			{
				const uint16_t id = (uint16_t)names.size();
				names.push_back(name);
				ids.emplace(name, id);
			}
		}
	};

	static Registry& registry()
	{
		static Registry value;
		return value;
	}

	static const std::string& name(uint16_t id)
	{
		const Registry& value = registry();
		return id < value.names.size() ? value.names[id] : value.names[0];
	}

	void set(const std::string& name)
	{
		value_ = 0;
		if (name.empty()) return;
		if (name.size() > 128) return;
		Registry& values = registry();
		const auto found = values.ids.find(name);
		if (found != values.ids.end())
		{
			value_ = found->second;
			return;
		}
		if (values.names.size() >= 4096 ||
			values.names.size() >= (size_t)0xFFFFu)
			return;
		value_ = (uint16_t)values.names.size();
		values.names.push_back(name);
		values.ids.emplace(name, value_);
	}
	bool matches(const std::string& name) const
	{
		return value_ != 0 && TagId::name(value_) == name;
	}
};
static_assert(sizeof(TagId) == 2, "TagId doit rester compact");

struct Node
{
	NodeType type = NodeType::Element;
	TagId tag;                    // identifiant compact, balises partagees
	uint32_t textOffset = 0;      // contenu dans l'arene texte du Document
	uint32_t textLength = 0;
	std::vector<Attribute> attrs;
	int parent = -1;
	std::vector<int> children;

	// Cache de layout (rempli par le moteur de rendu)
	int layoutLine = -1;

	bool isElement() const { return type == NodeType::Element; }
	bool hasAttr(const char* name) const
	{
		for (const auto& a : attrs)
			if (a.name == name) return true;
		return false;
	}
	const char* getAttr(const char* name) const
	{
		for (const auto& a : attrs)
			if (a.name == name) return a.value.c_str();
		return nullptr;
	}
};

//=============================================================================
// Document - arene de noeuds
//=============================================================================
class Document
{
public:
	std::vector<Node> nodes;
	int root = -1;
	uint64_t mutationVersion = 0;
	int hoveredNode = -1;
	int focusedNode = -1;

	void clear()
	{
		nodes.clear();
		textArena_.clear();
		root = -1;
		hoveredNode = -1;
		focusedNode = -1;
		++mutationVersion;
	}

	bool empty() const { return root < 0 || nodes.empty(); }

	int addNode(Node&& n)
	{
		nodes.push_back(std::move(n));
		++mutationVersion;
		return (int)nodes.size() - 1;
	}

	bool insertChild(int parent, int child, int before = -1)
	{
		if (parent < 0 || child < 0 || (size_t)parent >= nodes.size() ||
			(size_t)child >= nodes.size() || parent == child) return false;
		if (!nodes[(size_t)parent].isElement() && nodes[(size_t)parent].type != NodeType::Document)
			return false;
		for (int ancestor = parent; ancestor >= 0 && (size_t)ancestor < nodes.size();
			ancestor = nodes[(size_t)ancestor].parent)
			if (ancestor == child) return false;
		auto& destination = nodes[(size_t)parent].children;
		auto beforeIt = before < 0 ? destination.end() :
			std::find(destination.begin(), destination.end(), before);
		if (before >= 0 && beforeIt == destination.end()) return false;
		if (before == child) return true;
		int insertionIndex = (int)(beforeIt - destination.begin());
		const int oldParent = nodes[(size_t)child].parent;
		if (oldParent >= 0 && (size_t)oldParent < nodes.size())
		{
			auto& oldSiblings = nodes[(size_t)oldParent].children;
			auto oldIt = std::find(oldSiblings.begin(), oldSiblings.end(), child);
			if (oldIt != oldSiblings.end())
			{
				const int oldIndex = (int)(oldIt - oldSiblings.begin());
				oldSiblings.erase(oldIt);
				if (oldParent == parent && oldIndex < insertionIndex) --insertionIndex;
			}
		}
		if (insertionIndex < 0 || (size_t)insertionIndex > destination.size())
			insertionIndex = (int)destination.size();
		destination.insert(destination.begin() + insertionIndex, child);
		nodes[(size_t)child].parent = parent;
		++mutationVersion;
		return true;
	}

	bool detachNode(int child)
	{
		if (child < 0 || (size_t)child >= nodes.size()) return false;
		const int parent = nodes[(size_t)child].parent;
		if (parent < 0 || (size_t)parent >= nodes.size()) return false;
		auto& siblings = nodes[(size_t)parent].children;
		auto it = std::find(siblings.begin(), siblings.end(), child);
		if (it == siblings.end()) return false;
		siblings.erase(it);
		nodes[(size_t)child].parent = -1;
		++mutationVersion;
		return true;
	}

	void clearChildren(int parent)
	{
		if (parent < 0 || (size_t)parent >= nodes.size()) return;
		for (int child : nodes[(size_t)parent].children)
			if (child >= 0 && (size_t)child < nodes.size()) nodes[(size_t)child].parent = -1;
		if (!nodes[(size_t)parent].children.empty())
		{
			nodes[(size_t)parent].children.clear();
			++mutationVersion;
		}
	}

	// Cree un element avec tag + attrs, attaché au parent (index).
	int createElement(int parent, const std::string& tag)
	{
		Node n;
		n.type = NodeType::Element;
		n.tag = tag;
		int idx = addNode(std::move(n));
		if (parent >= 0)
		{
			nodes[idx].parent = parent;
			nodes[parent].children.push_back(idx);
		}
		return idx;
	}

	int createText(int parent, const std::string& text)
	{
		return createText(parent, std::string(text));
	}

	int createText(int parent, std::string&& text)
	{
		Node n;
		n.type = NodeType::Text;
		if (!appendText(n, text)) return -1;
		int idx = addNode(std::move(n));
		if (parent >= 0)
		{
			nodes[idx].parent = parent;
			nodes[parent].children.push_back(idx);
		}
		return idx;
	}

	// Le texte des noeuds est contigu et indexe par offset : les longues pages
	// n'allouent plus une std::string distincte pour chaque fragment de texte.
	std::string_view nodeTextView(int idx) const
	{
		if (idx < 0 || (size_t)idx >= nodes.size()) return {};
		const Node& node = nodes[(size_t)idx];
		if ((size_t)node.textOffset > textArena_.size() ||
			node.textLength > textArena_.size() - (size_t)node.textOffset)
			return {};
		return std::string_view(textArena_.data() + node.textOffset, node.textLength);
	}

	std::string nodeText(int idx) const
	{
		const std::string_view value = nodeTextView(idx);
		if (value.empty()) return std::string();
		return std::string(value.data(), value.size());
	}

	bool setNodeText(int idx, const std::string& text)
	{
		if (idx < 0 || (size_t)idx >= nodes.size()) return false;
		const size_t limit = 4u * 1024u * 1024u;
		if (text.size() > limit) return false;
		if (text.size() <= limit - textArena_.size())
		{
			Node& node = nodes[(size_t)idx];
			node.textOffset = (uint32_t)textArena_.size();
			node.textLength = (uint32_t)text.size();
			textArena_ += text;
			++mutationVersion;
			return true;
		}

		// Les scripts peuvent remplacer plusieurs fois le même Text. Compacter
		// avant de refuser évite que les anciennes valeurs gonflent l'arene.
		size_t total = text.size();
		for (size_t i = 0; i < nodes.size(); ++i)
			if ((int)i != idx) total += nodes[i].textLength;
		if (total > limit) return false;
		std::string compact;
		compact.reserve(total);
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			const Node& node = nodes[i];
			const std::string_view value = (int)i == idx
				? std::string_view(text) : nodeTextView((int)i);
			compact += value;
		}
		size_t offset = 0;
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			Node& node = nodes[i];
			node.textOffset = (uint32_t)offset;
			if ((int)i == idx) node.textLength = (uint32_t)text.size();
			offset += node.textLength;
		}
		textArena_.swap(compact);
		++mutationVersion;
		return true;
	}

	void clearNodeText(int idx)
	{
		if (idx < 0 || (size_t)idx >= nodes.size()) return;
		setNodeText(idx, std::string());
	}

	void setAttr(int idx, const std::string& name, const std::string& value)
	{
		if (idx < 0 || (size_t)idx >= nodes.size()) return;
		for (auto& a : nodes[idx].attrs)
		{
			if (a.name == name) { if (a.value != value) { a.value = value; ++mutationVersion; } return; }
		}
		nodes[idx].attrs.push_back({ name, value });
		++mutationVersion;
	}

	void removeAttr(int idx, const std::string& name)
	{
		if (idx < 0 || (size_t)idx >= nodes.size()) return;
		auto& attrs = nodes[(size_t)idx].attrs;
		const size_t oldSize = attrs.size();
		attrs.erase(std::remove_if(attrs.begin(), attrs.end(),
			[&](const Attribute& attr) { return attr.name == name; }), attrs.end());
		if (attrs.size() != oldSize) ++mutationVersion;
	}

	// Texte brut concatene de tous les descendants (utilise pour title, alt...)
	std::string textOf(int idx) const
	{
		std::string out;
		if (idx < 0) return out;
		std::vector<int> pending;
		pending.push_back(idx);
		while (!pending.empty())
		{
			const int current = pending.back();
			pending.pop_back();
			const Node& n = nodes[current];
			if (n.type == NodeType::Text || n.type == NodeType::RawText)
			{
				const std::string_view value = nodeTextView(current);
				if (!value.empty()) out.append(value.data(), value.size());
				continue;
			}
			for (auto child = n.children.rbegin(); child != n.children.rend(); ++child)
				pending.push_back(*child);
		}
		return out;
	}

	bool appendText(Node& node, const std::string& text)
	{
		const size_t limit = 4u * 1024u * 1024u;
		if (text.size() > limit - textArena_.size()) return false;
		node.textOffset = (uint32_t)textArena_.size();
		node.textLength = (uint32_t)text.size();
		textArena_ += text;
		return true;
	}

	std::string textArena_;

	// Premier noeud descendant correspondant a un tag (parcours en profondeur)
	int firstByTag(int idx, const char* tag) const
	{
		if (idx < 0) return -1;
		std::vector<int> pending;
		pending.push_back(idx);
		while (!pending.empty())
		{
			const int current = pending.back();
			pending.pop_back();
			const Node& n = nodes[current];
			if (n.isElement() && n.tag == tag) return current;
			for (auto child = n.children.rbegin(); child != n.children.rend(); ++child)
				pending.push_back(*child);
		}
		return -1;
	}

	// Titre de la page : <title> ou nom de domaine par defaut.
	std::string title(int rootIdx = -1) const
	{
		int r = rootIdx >= 0 ? rootIdx : root;
		int t = firstByTag(r, "title");
		if (t >= 0)
		{
			std::string s = strTrim(textOf(t));
			if (!s.empty()) return s;
		}
		return "Pixelnaute";
	}


	void dump(int idx, int depth) const; // debug: impression sur console

private:
};
