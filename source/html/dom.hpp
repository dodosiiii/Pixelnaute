#pragma once
//=============================================================================
// dom.hpp - Arbre DOM leger et optimise memoire
//
// Les noeuds vivent dans un vecteur (arene) : aucun pointeur, index seulement.
// Cela evite toute allocation par noeud et rend le parcours tres rapide.
//=============================================================================
#include "../common.hpp"
#include <string>
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

struct Node
{
	NodeType type = NodeType::Element;
	std::string tag;             // minuscules pour un Element
	std::string text;            // contenu pour Text/RawText/Comment
	std::vector<Attribute> attrs;
	int parent = -1;
	std::vector<int> children;

	// Cache de layout (rempli par le moteur de rendu)
	int layoutLine = -1;

	bool isElement() const { return type == NodeType::Element; }
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

	void clear()
	{
		nodes.clear();
		root = -1;
	}

	bool empty() const { return root < 0 || nodes.empty(); }

	int addNode(Node&& n)
	{
		nodes.push_back(std::move(n));
		return (int)nodes.size() - 1;
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
		Node n;
		n.type = NodeType::Text;
		n.text = text;
		int idx = addNode(std::move(n));
		if (parent >= 0)
		{
			nodes[idx].parent = parent;
			nodes[parent].children.push_back(idx);
		}
		return idx;
	}

	void setAttr(int idx, const std::string& name, const std::string& value)
	{
		for (auto& a : nodes[idx].attrs)
		{
			if (a.name == name) { a.value = value; return; }
		}
		nodes[idx].attrs.push_back({ name, value });
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
				out += n.text;
				continue;
			}
			for (auto child = n.children.rbegin(); child != n.children.rend(); ++child)
				pending.push_back(*child);
		}
		return out;
	}

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
