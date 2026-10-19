#pragma once
//=============================================================================
// layout.hpp - Mise en page du DOM -> PageModel
//=============================================================================
#include "../common.hpp"
#include "../html/dom.hpp"
#include "../css/css.hpp"
#include "../settings/settings.hpp"
#include "bitmap.hpp"
#include <string>
#include <vector>

// Rect cliquable (lien ou bouton).
struct LinkBox
{
	int x, y, w, h;
	std::string url;
	int node = -1;
};

// Champ de formulaire (mode d'edition géré par l'application).
enum FieldKind
{
	FieldText = 0,
	FieldPassword,
	FieldTextarea,
	FieldSelect,
	FieldNumber,
	FieldSearch,
	FieldEmail,
	FieldCheckbox,
	FieldRadio,
};

struct FormField
{
	int x = 0, y = 0, w = 0, h = 0;
	int kind = FieldText;
	std::string name;
	std::string value;
	int node = -1;      // index DOM de l'element <input>/<textarea>/<select>
	int formId = -1;    // proprietaire (voir FormSubmit)
	bool checked = false;
	bool disabled = false;
};

struct FormSubmit
{
	int x = 0, y = 0, w = 0, h = 0;
	std::string action;   // URL cible
	std::string method;   // get / post
	int node = -1;
	int formId = -1;
	std::vector<int> fields; // indexes FormField associes
	std::string label;
	std::string name;
	std::string value;
};

struct ImageBox
{
	int x = 0, y = 0, w = 0, h = 0;
	std::string src;
	std::string alt;
};

//=============================================================================
// Resultat complet d'une mise en page
//=============================================================================
struct PageModel
{
	Bitmap bmp;                       // rendu RGB565 (largeur 400)
	int height = 0;                   // hauteur du contenu
	bool truncated = false;           // la mise en page a atteint sa limite de hauteur
	std::vector<LinkBox> links;
	std::vector<FormField> fields;
	std::vector<FormSubmit> submits;
	std::vector<ImageBox> images;
	bool jsUsed = false;              // <script> present sur la page
	std::string csrfToken;            // jeton CSRF de la page (ex: meta csrf-token)
	std::string csrfParam;            // nom du parametre CSRF (ex: meta csrf-param)
	int errorCode = 0;                // non zero = page d'erreur interne
	std::string errorText;

	PageModel() = default;
	PageModel(const PageModel&) = delete;
	PageModel& operator=(const PageModel&) = delete;
	PageModel(PageModel&&) = default;
	PageModel& operator=(PageModel&&) = default;

	void clear()
	{
		bmp.destroy();
		height = 0;
		truncated = false;
		links.clear();
		fields.clear();
		submits.clear();
		images.clear();
		jsUsed = false;
		csrfToken.clear();
		csrfParam.clear();
		errorCode = 0;
		errorText.clear();
	}
};

//=============================================================================
// PageLayout : Document + CssEngine + parametres -> PageModel
//=============================================================================
class PageLayout
{
public:
	PageLayout(const Document& doc, const CssEngine& css, const WebSettings& settings);

	// Convertit le document en PageModel. Retourne false en cas de limite memoire.
	bool render(PageModel& out, const std::string& pageUrl);

private:
	const Document* doc_;
	const CssEngine* css_;
	const WebSettings* set_;
	PageModel* out_;
	Bitmap* target_ = nullptr;       // surface de dessin courante (voir out_->bmp)

	int W_ = NW_PAGE_WIDTH;          // largeur page
	// Echelle de base du texte de page.
	//
	// La largeur logique d'une page est NW_PAGE_WIDTH (400 px), affichee 1:1
	// sur l'ecran du haut. Une echelle de 0,85 donnait un texte trop large
	// pour cet ecran : les mots longs debordaient et les lignes se coupaient
	// mal. 0,55 place le texte courant autour de 13 px, soit la taille
	// habituelle sur un ecran de cette largeur.
	float baseScale_ = 0.55f;
	int x0_ = 4;                     // marge bloc courante
	int x1_ = NW_PAGE_WIDTH - 4;     // bord droit du bloc courant
	int ix_ = 0, iy_ = 0;            // curseur inline
	int lineMaxH_ = 0;               // hauteur max de la ligne courante
	int by_ = 0;                     // position verticale courante
	int formCounter_ = 0;
	int curFormId_ = -1;
	std::string curFormAction_;
	std::string curFormMethod_;
	std::string pageUrl_;
	bool linkActive_ = false;
	std::string linkHref_;
	int linkNode_ = -1;
	int linkX0_ = 0, linkY0_ = 0;
	int renderDepth_ = 0;
	uint32_t defaultTextColor_ = 0xFF202020;
	bool pendingSpace_ = false;

	void renderNode(int idx, const Style& st);
	void renderInline(int idx, const Style& st);
	void newline(int height);
	void flushLink();
	void addLinkBox(int x0, int y0, int x1, int y1, const std::string& url, int node = -1);
	void drawTextWrapped(const std::string& text, const Style& st, bool mono);
	void resolveAndRender(int idx, const Style& parentSt);
};
