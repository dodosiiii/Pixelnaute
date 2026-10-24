#pragma once
//=============================================================================
// css.hpp - Style leve (CSS) et moteur de resolution
//=============================================================================
#include "../common.hpp"
#include "../html/dom.hpp"
#include <string>
#include <unordered_map>
#include <vector>

enum CssProperty : uint64_t
{
	CssPropColor       = 1ULL << 0,
	CssPropBackground  = 1ULL << 1,
	CssPropFontSize    = 1ULL << 2,
	CssPropBold        = 1ULL << 3,
	CssPropItalic      = 1ULL << 4,
	CssPropUnderline   = 1ULL << 5,
	CssPropAlign       = 1ULL << 6,
	CssPropMarginTop   = 1ULL << 7,
	CssPropMarginRight = 1ULL << 8,
	CssPropMarginBottom= 1ULL << 9,
	CssPropMarginLeft  = 1ULL << 10,
	CssPropPaddingTop  = 1ULL << 11,
	CssPropPaddingRight= 1ULL << 12,
	CssPropPaddingBottom=1ULL << 13,
	CssPropPaddingLeft = 1ULL << 14,
	CssPropLetterSpacing = 1ULL << 28,
	CssPropWidth       = 1ULL << 15,
	CssPropHeight      = 1ULL << 16,
	CssPropDisplay     = 1ULL << 17,
	CssPropMono        = 1ULL << 18,
	CssPropMinWidth    = 1ULL << 19,
	CssPropMaxWidth    = 1ULL << 20,
	CssPropBoxSizing   = 1ULL << 21,
	CssPropFlex        = 1ULL << 22,
	CssPropFlexRow     = 1ULL << 23,
	CssPropFlexWrap    = 1ULL << 24,
	CssPropJustify     = 1ULL << 25,
	CssPropMarginLeftAuto  = 1ULL << 26,
	CssPropMarginRightAuto = 1ULL << 27,
	CssPropListStyle       = 1ULL << 29,
	CssPropBorder          = 1ULL << 30,
	CssPropRowGap          = 1ULL << 31,
	CssPropColumnGap       = 1ULL << 32,
	CssPropLineHeight      = 1ULL << 33,
	CssPropBorderRadius    = 1ULL << 34,
	CssPropBoxShadow       = 1ULL << 35,
	CssPropGridColumns     = 1ULL << 36,
	CssPropGridColumnSpan  = 1ULL << 37,
	CssPropGrid            = 1ULL << 38,
	CssPropMinHeight       = 1ULL << 39,
	CssPropMaxHeight       = 1ULL << 40,
	CssPropBorderTopWidth  = 1ULL << 41,
	CssPropBorderTopColor  = 1ULL << 42,
	CssPropBorderTopStyle  = 1ULL << 43,
	CssPropBorderRightWidth = 1ULL << 44,
	CssPropBorderRightColor = 1ULL << 45,
	CssPropBorderRightStyle = 1ULL << 46,
	CssPropBorderBottomWidth = 1ULL << 47,
	CssPropBorderBottomColor = 1ULL << 48,
	CssPropBorderBottomStyle = 1ULL << 49,
	CssPropBorderLeftWidth = 1ULL << 50,
	CssPropBorderLeftColor = 1ULL << 51,
	CssPropBorderLeftStyle = 1ULL << 52,
	CssPropWhiteSpace = 1ULL << 53,
	CssPropTextTransform = 1ULL << 54,
	CssPropFlexGrow = 1ULL << 55,
	CssPropOverflowX = 1ULL << 56,
	CssPropOverflowY = 1ULL << 57,
	CssPropPosition = 1ULL << 58,
	CssPropLeft = 1ULL << 59,
	CssPropRight = 1ULL << 60,
	CssPropTop = 1ULL << 61,
	CssPropBottom         = 1ULL << 62,
	CssPropVerticalAlign  = 1ULL << 63
};

struct CssGridTrack
{
	int fixedWidth = 0;
	int minWidth = 0;
	int fraction = 1;
	bool autoRepeat = false;
	bool autoFit = false;
};

struct CssCustomProperty
{
	std::string name;
	std::string value;
	bool important = false;
};

enum CssBorderStyle
{
	CssBorderSolid,
	CssBorderDashed,
	CssBorderDotted,
	CssBorderNone
};

enum CssWhiteSpace
{
	CssWhiteSpaceNormal,
	CssWhiteSpaceNoWrap,
	CssWhiteSpacePre,
	CssWhiteSpacePreLine,
	CssWhiteSpacePreWrap
};

enum CssTextTransform
{
	CssTextTransformNone,
	CssTextTransformUppercase,
	CssTextTransformLowercase,
	CssTextTransformCapitalize
};

enum CssImageFit
{
	CssImageFitFill,
	CssImageFitContain,
	CssImageFitCover
};

struct CssBorderSide
{
	int width = 0;
	uint32_t color = 0xFF968A8A;
	CssBorderStyle style = CssBorderSolid;
};

struct CssDeclaration
{
	std::string property;
	std::string value;
};

// Style calcule pour un noeud.
struct Style
{
	uint64_t declared = 0;
	uint64_t important = 0;
	bool  colorSet   = false;
	uint32_t color   = 0xE8E8EC;
	bool  bgSet      = false;
	uint32_t bg      = 0;
	int   fontSize   = 0;     // 0 -> 16px (echelle 1.0)
	bool  bold       = false;
	bool  italic     = false;
	bool  underline  = false;
	CssImageFit imageFit = CssImageFitFill;
	bool imageFitSet = false;
	bool imageFitImportant = false;
	int imagePositionX = 50, imagePositionY = 50;
	bool imagePositionSet = false;
	bool imagePositionImportant = false;
	CssWhiteSpace whiteSpace = CssWhiteSpaceNormal;
	CssTextTransform textTransform = CssTextTransformNone;
	float letterSpacing = 0.0f;
	bool letterSpacingIsEm = false;
	int   align      = 0;     // 0=gauche 1=centre 2=droite
	int   marginTop  = 0, marginBottom = 0, marginLeft = 0, marginRight = 0;
	int   marginTopPercent = 0, marginRightPercent = 0;
	int   marginBottomPercent = 0, marginLeftPercent = 0;
	bool  marginTopIsPercent = false, marginRightIsPercent = false;
	bool  marginBottomIsPercent = false, marginLeftIsPercent = false;
	int   paddingTop = 0, paddingBottom = 0, paddingLeft = 0, paddingRight = 0;
	int   paddingTopPercent = 0, paddingRightPercent = 0;
	int   paddingBottomPercent = 0, paddingLeftPercent = 0;
	bool  paddingTopIsPercent = false, paddingRightIsPercent = false;
	bool  paddingBottomIsPercent = false, paddingLeftIsPercent = false;
	int   width      = 0;     // px fixes (0 = auto)
	int   widthPercent = 0;
	bool  widthIsPercent = false;
	int   minWidth   = 0;
	int   minWidthPercent = 0;
	bool  minWidthIsPercent = false;
	int   maxWidth   = 0;
	int   maxWidthPercent = 0;
	bool  maxWidthIsPercent = false;
	int   height     = 0;
	int   heightPercent = 0;
	bool  heightIsPercent = false;
	int   minHeight  = 0;
	int   minHeightPercent = 0;
	bool  minHeightIsPercent = false;
	int   maxHeight  = 0;
	int   maxHeightPercent = 0;
	bool  maxHeightIsPercent = false;
	bool  displayNone = false;
	bool  overflowXClip = false, overflowYClip = false;
	bool  positionRelative = false;
	int   positionLeft = 0, positionRight = 0;
	int   positionTop = 0, positionBottom = 0;
	int   positionLeftPercent = 0, positionRightPercent = 0;
	int   positionTopPercent = 0, positionBottomPercent = 0;
	bool  positionLeftIsPercent = false, positionRightIsPercent = false;
	bool  positionTopIsPercent = false, positionBottomIsPercent = false;
	bool  block       = false;  // display:block explicite
	bool  displayInline = false;
	bool  displayInlineBlock = false;
	bool  displayTable = false;
	bool  displayTableRow = false;
	bool  displayTableCell = false;
	bool  verticalAlignMiddle = false;
	bool  positionAbsolute = false;
	bool  positionFixed = false;
	bool  floatRight = false;
	bool  mono        = false;  // <pre>/<code>
	bool  grid        = false;
	std::vector<CssGridTrack> gridColumns;
	int   gridColumnSpan = 1;
	bool  listStyleNone = false;
	int   borderWidth = 0;
	uint32_t borderColor = 0xFF968A8A;
	CssBorderStyle borderStyle = CssBorderSolid;
	CssBorderSide borderTop, borderRight, borderBottom, borderLeft;
	int   rowGap = 0, columnGap = 0;
	int   lineHeight = 0;
	bool  lineHeightRelative = false;
	bool  lineHeightUnitless = false;
	uint32_t gradientColors[8] = {};
	int gradientStops[8] = {};
	int gradientAngle = 180;
	int gradientCount = 0;
	bool gradientRadial = false;
	int borderRadius = 0;
	int shadowX = 0, shadowY = 0, shadowBlur = 0, shadowSpread = 0;
	uint32_t shadowColor = 0;
	bool boxShadowSet = false;
	bool  marginLeftAuto = false, marginRightAuto = false;
	bool  borderBox = false;
	bool  flex = false, flexRow = true, flexWrap = false, flexReverse = false;
	int   justifyContent = 0; // start, center, space-between, end, around, evenly
	int   flexGrow = 0; // poids multiplie par 100
	std::vector<CssCustomProperty> customProperties;

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
	char operation = '=';
};

struct CssCompoundSelector
{
	std::string tag;
	std::vector<std::string> classes;
	std::string id;
	std::vector<CssAttrSelector> attrs;
	bool firstChild = false;
	bool lastChild = false;
	bool onlyChild = false;
	bool nthChildSet = false;
	int nthChildA = 0;
	int nthChildB = 0;
	bool nthOfTypeSet = false;
	int nthOfTypeA = 0;
	int nthOfTypeB = 0;
	bool hover = false;
	bool focus = false;
	bool root = false;
	bool checked = false;
	bool disabled = false;
	std::string notSelector;
	bool unsupported = false;
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
	std::vector<CssDeclaration> declarations;
};

//=============================================================================
// CssEngine
// Parse des blocs <style> et calcule les styles appliques a chaque noeud.
//=============================================================================
class CssEngine
{
public:
	void setViewportWidth(int width);
	void clear()
	{
		rules_.clear();
		rulesByTag_.clear();
		rulesByClass_.clear();
		rulesById_.clear();
		fallbackRules_.clear();
		universalRules_.clear();
		stylesheetBytes_ = 0;
		sourceOrder_ = -1;
		sourceRuleOrder_ = 0;
	}

	// Analyse une feuille CSS entiere.
	void parseStylesheet(const std::string& css, int sourceOrder = -1);
	static const size_t MaxRules = 1536;
	static const size_t MaxStylesheetBytes = 128 * 1024;

	// Recupère la liste des regles (pour debug).
	const std::vector<CssRule>& rules() const { return rules_; }
	size_t stylesheetBytes() const { return stylesheetBytes_; }

	// Applique toutes les regles matchant le noeud.
	Style resolve(const Document& doc, int nodeIdx, const Style& parent) const;

private:
	int viewportWidth_ = NW_PAGE_WIDTH;
	std::vector<CssRule> rules_;
	std::unordered_map<std::string, std::vector<size_t>> rulesByTag_;
	std::unordered_map<std::string, std::vector<size_t>> rulesByClass_;
	std::unordered_map<std::string, std::vector<size_t>> rulesById_;
	std::vector<size_t> fallbackRules_;
	std::vector<size_t> universalRules_;
	size_t stylesheetBytes_ = 0;
	int sourceOrder_ = -1;
	int sourceRuleOrder_ = 0;

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
