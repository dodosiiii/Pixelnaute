#include "dom.hpp"

void Document::dump(int idx, int depth) const
{
	if (idx < 0) return;
	const Node& n = nodes[idx];
	for (int i = 0; i < depth; i++) printf("  ");
	switch (n.type)
	{
		case NodeType::Document: printf("[document]\n"); break;
		case NodeType::Element:
			printf("<%s", n.tag.c_str());
			for (const auto& a : n.attrs)
				printf(" %s=\"%s\"", a.name.c_str(), a.value.c_str());
			printf(">\n");
			break;
		case NodeType::Text:
		{
			std::string t = strTrim(nodeText(idx));
			if (t.size() > 60) t = t.substr(0, 60) + "...";
			printf("\"%s\"\n", t.c_str());
			return;
		}
		case NodeType::RawText:
			printf("[raw %u bytes]\n", n.textLength);
			return;
		case NodeType::Comment:
			return;
	}
	for (int c : n.children) dump(c, depth + 1);
}
