#pragma once
//=============================================================================
// css.hpp - Style leve (CSS) et moteur de resolution
//=============================================================================
#include "../common.hpp"
#include "../html/dom.hpp"
#include <string>
#include <vector>

// Style calcule pour un noeud.
struct Style
{
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

// Une regle CSS : selecteur + declarations.
struct CssRule
{
	int specificity = 0;
	std::string tag;       // peut etre vide ("*" / ".class" / "#id")
	std::string cls;       // classe
	std::string id;        // id
	Style style;
};

//=============================================================================
// CssEngine
// Parse des blocs <style> (selecteurs simples) et calcule les styles
// appliques a chaque noeud (l'heritage parent -> enfant est gere par le
// moteur de mise en page via Style::inheritFrom()).
//=============================================================================
class CssEngine
{
public:
	void clear() { rules_.clear(); }

	// Analyse une feuille CSS entiere.
	void parseStylesheet(const std::string& css);
	static const size_t MaxRules = 512;

	// Recupère la liste des regles (pour debug).
	const std::vector<CssRule>& rules() const { return rules_; }

	// Applique toutes les regles mat offrant le tag/classe/id.
	// parentStyle sert d'heritage.
	Style resolve(const Document& doc, int nodeIdx, const Style& parent) const;

private:
	std::vector<CssRule> rules_;

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