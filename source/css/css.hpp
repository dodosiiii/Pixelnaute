#pragma once
//=============================================================================
// css.hpp - Style leve (CSS) et moteur de resolution
//=============================================================================
#include "../common.hpp"
#include "../html/dom.hpp"
#include <string>
#include <vector>

enum CssProperty : uint32_t
{
	CssPropColor       = 1u << 0,
	CssPropBackground  = 1u << 1,
	CssPropFontSize    = 1u << 2,
	CssPropBold        = 1u << 3,
	CssPropItalic      = 1u << 4,
	CssPropUnderline   = 1u << 5,
	CssPropAlign       = 1u << 6,
	CssPropMarginTop   = 1u << 7,
	CssPropMarginRight = 1u << 8,
	CssPropMarginBottom= 1u << 9,
	CssPropMarginLeft  = 1u << 10,
	CssPropPaddingTop  = 1u << 11,
	CssPropPaddingRight= 1u << 12,
	CssPropPaddingBottom=1u << 13,
	CssPropPaddingLeft = 1u << 14,
	CssPropWidth       = 1u << 15,
	CssPropHeight      = 1u << 16,
	CssPropDisplay     = 1u << 17,
	CssPropMono        = 1u << 18
};

// Style calcule pour un noeud.
struct Style
{
	uint32_t declared = 0;
	bool  colorSet   = false;
	uint32_t color   = 0xE8E8EC;
	bool  bgSet      = false;
	uint32_t bg      = 0;
	int   fontSize   = 0;     // 0 -> 16px (echelle 1.0)
	bool  bold       = false;
	bool  italic     = false;
	bool  underline  = false;
	int   align      = 0;     // 0=gauche 1=centre 2=droite
	int   marginTop  = 0, marginBottom = 0, marginLeft = 0, marginRight = 0;
	int   paddingTop = 0, paddingBottom = 0, paddingLeft = 0, paddingRight = 0;
	int   width      = 0;     // px fixes (0 = auto)
	int   height     = 0;
	bool  displayNone = false;
	bool  block       = false;  // display:block explicite
	bool  mono        = false;  // <pre>/<code>

	float scale() const {                                  // echelle de fonte
		int f = fontSize > 0 ? fontSize : 16;
		return f / 16.0f;
	}
};

struct CssAttrSelector
{
	std::string name;
	std::string value;
	bool hasValue = false;
};

struct CssCompoundSelector
{
	std::string tag;
	std::vector<std::string> classes;
	std::string id;
	std::vector<CssAttrSelector> attrs;
	bool firstChild = false;
	char combinator = 0; // ' ' = descendant, '>' = child, 0 = target
};

// Une regle CSS : selecteur + declarations.
struct CssRule
{
	int specificity = 0;
	int order = 0;
	std::vector<CssCompoundSelector> selectorChain;
	std::string tag;       // tag cible (compatibilite)
	std::string cls;       // premiere classe (compatibilite)
	std::string id;        // id (compatibilite)
	Style style;
};

//=============================================================================
// CssEngine
// Parse des blocs <style> et calcule les styles appliques a chaque noeud.
//=============================================================================
class CssEngine
{
public:
	void clear() { rules_.clear(); stylesheetBytes_ = 0; }

	// Analyse une feuille CSS entiere.
	void parseStylesheet(const std::string& css);
	static const size_t MaxRules = 512;
	static const size_t MaxStylesheetBytes = 128 * 1024;

	// Recupère la liste des regles (pour debug).
	const std::vector<CssRule>& rules() const { return rules_; }
	size_t stylesheetBytes() const { return stylesheetBytes_; }

	// Applique toutes les regles matchant le noeud.
	Style resolve(const Document& doc, int nodeIdx, const Style& parent) const;

private:
	std::vector<CssRule> rules_;
	size_t stylesheetBytes_ = 0;

	// Parse une rule "selector { decls }"
	void parseRule(const std::string& text);
};

// Frontiere utilitaire : Style par defaut (UA).
Style defaultStyle();

// Collecte les blocs <style> du document et les ajoute au moteur.
void collectStyleBlocks(const Document& doc, CssEngine& css);

// Analyse une valeur `style=""` inline.
void parseInlineStyle(const std::string& decls, Style& out);

// Couleur CSS (hex / rgb() / nom).
bool cssParseColor(const std::string& str, uint32_t& out);

// Tests unitaires (compilables sur PC).
int cssSelfTest();