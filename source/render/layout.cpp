#include "layout.hpp"
#include "text.hpp"
#include "../html/parser.hpp"
#include <math.h>

//--------------------------------------------------------------------------
// Utilitaires
//--------------------------------------------------------------------------
static int attrInt(const Node& n, const char* name, int def)
{
	const char* v = n.getAttr(name);
	if (!v || !*v) return def;
	return atoi(v);
}

static int uaMarginBottom(const std::string& tag)
{
	if (tag == "p" || tag == "div" || tag == "ul" || tag == "ol" || tag == "blockquote" ||
		tag == "table" || tag == "form" || tag == "pre" || tag == "h1" || tag == "h2" ||
		tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6" || tag == "hr")
		return 6;
	return 0;
}

static bool isBlockTag(const std::string& t)
{
	static const char* blocks[] =
	{
		"div","p","h1","h2","h3","h4","h5","h6","ul","ol","li","hr","blockquote","pre",
		"table","tr","td","th","form","address","article","section","nav","header","footer",
		"aside","figure","fieldset","center","body","dl","dt","dd","select","textarea",
		nullptr
	};
	for (int i = 0; blocks[i]; i++)
		if (t == blocks[i]) return true;
	return false;
}

static bool isHeadTag(const std::string& t)
{
	return t == "head" || t == "title" || t == "meta" || t == "link" || t == "base" ||
		t == "style" || t == "script";
}

// Fusion d'une URL relative avec l'URL de base de la page (utilise urlJoin dans common.hpp).
static inline std::string joinUrl(const std::string& base, const std::string& ref)
{
	return urlJoin(base, ref);
}


//--------------------------------------------------------------------------
PageLayout::PageLayout(const Document& doc, const CssEngine& css, const WebSettings& settings)
	: doc_(&doc), css_(&css), set_(&settings)
{
}

//--------------------------------------------------------------------------
// Avance la position verticale du bloc (ferme la ligne en cours).
//--------------------------------------------------------------------------
void PageLayout::newline(int height)
{
	if (by_ < iy_ + lineMaxH_) by_ = iy_ + lineMaxH_;
	iy_ = by_;
	ix_ = x0_;
	lineMaxH_ = 0;
	if (height > 0) by_ += height;
}

void PageLayout::flushLink()
{
}

//--------------------------------------------------------------------------
// Sauvegarde et restauration d'une zone de la page.
//
// Sert a peindre le fond d'un bloc APRES que son contenu a ete dessine (seule
// facon de connaitre sa hauteur) sans masquer ce contenu : on sauvegarde la
// zone, on peint le fond, puis on restaure le contenu par-dessus.
//
// La surface restauree est bornee : au-dela, le fond est simplement peint
// avant le contenu, ce qui reste correct visuellement.
//--------------------------------------------------------------------------
static const size_t kMaxSavedPixels = 400u * 1200u;

void PageLayout::saveRegion(int x, int y, int w, int h)
{
	regionSave_.clear();
	if (!target_ || !target_->px || w <= 0 || h <= 0) return;
	if ((size_t)w * (size_t)h > kMaxSavedPixels) return;

	// Bornes utiles, pour ne pas allouer plus que necessaire.
	const int x1 = (x + w > target_->w) ? target_->w : x + w;
	const int y1 = (y + h > target_->h) ? target_->h : y + h;
	const int x0 = x < 0 ? 0 : x;
	const int y0 = y < 0 ? 0 : y;
	if (x1 <= x0 || y1 <= y0) return;

	regionSave_.reserve((size_t)(x1 - x0) * (size_t)(y1 - y0));
	for (int row = y0; row < y1; row++)
	{
		const uint16_t* src = target_->px + (size_t)row * target_->w + x0;
		regionSave_.insert(regionSave_.end(), src, src + (x1 - x0));
	}
}

void PageLayout::restoreRegion(int x, int y, int w, int h)
{
	if (regionSave_.empty() || !target_ || !target_->px) return;
	const int x1 = (x + w > target_->w) ? target_->w : x + w;
	const int y1 = (y + h > target_->h) ? target_->h : y + h;
	const int x0 = x < 0 ? 0 : x;
	const int y0 = y < 0 ? 0 : y;
	const int rowWidth = x1 - x0;
	if (rowWidth <= 0) { regionSave_.clear(); return; }

	size_t index = 0;
	for (int row = y0; row < y1; row++)
	{
		uint16_t* dst = target_->px + (size_t)row * target_->w + x0;
		for (int col = 0; col < rowWidth; col++)
		{
			if (index >= regionSave_.size()) { regionSave_.clear(); return; }
			const uint16_t saved = regionSave_[index++];
			if (saved != pageBg565_) dst[col] = saved;
		}
	}
	regionSave_.clear();
}

void PageLayout::addLinkBox(int x0, int y0, int x1, int y1, const std::string& url)
{
	if (x1 <= x0 || y1 <= y0) return;
	if ((int)out_->links.size() >= NW_MAX_LINKS) return;
	out_->links.push_back({ x0, y0, x1 - x0, y1 - y0, url });
}

//--------------------------------------------------------------------------
// Dessine le texte en le coupant aux mots.
//--------------------------------------------------------------------------
void PageLayout::drawTextWrapped(const std::string& text, const Style& st, bool mono)
{
	TextEngine& te = TextEngine::instance();
	if (!te.ready()) return;

	float scale = baseScale_ * st.scale();
	if (mono) scale *= 0.92f;
	uint32_t col = st.colorSet ? st.color : defaultTextColor_;
	bool bold = st.bold;
	int pitch = te.linePitch(scale);

	std::string line;
	float lineW = 0.0f;

	auto drawSeg = [&](const std::string& s, float w)
	{
		const int bx0 = ix_;
		const int by0 = iy_;
		te.drawLine((*target_), s, ix_, iy_, scale, col, bold,
			linkActive_ || st.underline);
		const int bx1 = ix_ + (int)ceilf(w);
		// Un lien "display:block" (une tuile, un bouton) doit etre cliquable
		// sur toute sa surface, pas seulement sur la largeur du texte.
		if (linkActive_)
		{
			const int right = st.block ? std::min(x1_, NW_PAGE_WIDTH - 2) : std::min(bx1, NW_PAGE_WIDTH - 2);
			addLinkBox(bx0, by0, right, iy_ + pitch, linkHref_);
		}
		ix_ = bx1;
		if (pitch > lineMaxH_) lineMaxH_ = pitch;
	};

	auto placeSegment = [&](const std::string& segment)
	{
		std::string cand = line.empty() ? segment : line + " " + segment;
		float wc = te.measure(cand, scale);
		if (ix_ + wc > x1_ && (!line.empty() || ix_ > x0_))
		{
			if (!line.empty()) drawSeg(line, lineW);
			newline(0);
			line = segment;
			lineW = te.measure(segment, scale);
		}
		else
		{
			line = cand;
			lineW = wc;
		}
	};

	int maxAvail = x1_ - x0_;
	if (maxAvail < 40) maxAvail = 40;
	size_t pos = 0;
	while (pos < text.size())
	{
		while (pos < text.size() &&
			(text[pos] == ' ' || text[pos] == '\t' ||
			 text[pos] == '\n' || text[pos] == '\r'))
			++pos;
		if (pos >= text.size()) break;

		size_t end = pos;
		while (end < text.size() && text[end] != ' ' && text[end] != '\t' &&
			text[end] != '\n' && text[end] != '\r')
			++end;
		const std::string word = text.substr(pos, end - pos);
		pos = end;

		if (te.measure(word, scale) <= maxAvail)
		{
			placeSegment(word);
			continue;
		}

		std::string chunk;
		size_t offset = 0;
		while (offset < word.size())
		{
			uint32_t cp;
			int n = utf8Decode(word.c_str() + offset, cp);
			if (n <= 0) break;
			const std::string ch = word.substr(offset, (size_t)n);
			offset += (size_t)n;
			if (!chunk.empty() && te.measure(chunk + ch, scale) > maxAvail - 4)
			{
				placeSegment(chunk);
				chunk.clear();
			}
			chunk += ch;
		}
		if (!chunk.empty()) placeSegment(chunk);
	}
	if (!line.empty()) drawSeg(line, lineW);
}

//--------------------------------------------------------------------------
// Dessine un champ de formulaire.
//--------------------------------------------------------------------------
static int fieldHeight(int kind)
{
	return kind == FieldTextarea ? 36 : 20;
}

void PageLayout::renderNode(int idx, const Style& st)
{
	if (idx < 0) return;
	const Node& node = doc_->nodes[idx];
	if (node.type == NodeType::Comment) return;

	// Un element se comporte comme un bloc s'il s'agit d'une balise de bloc
	// ou si son style declare display:block.
	const bool block = node.isElement() && (isBlockTag(node.tag) || st.block);
	const bool isForm = node.isElement() && node.tag == "form";
	const int previousFormId = curFormId_;
	const std::string previousFormAction = isForm ? curFormAction_ : "";
	const std::string previousFormMethod = isForm ? curFormMethod_ : "";

	if (node.isElement())
	{
		const std::string& tag = node.tag;

		if (tag == "script")
		{
			out_->jsUsed = true;
			return;
		}
		if (isHeadTag(tag)) return;

		if (tag == "br")
		{
			newline(0);
			// interligne supplementaire si aucun contenu sur la ligne
			if (lineMaxH_ == 0) by_ += TextEngine::instance().linePitch(baseScale_) / 2;
			iy_ = by_;
			ix_ = x0_;
			return;
		}

		if (tag == "img")
		{
			int w = attrInt(node, "width", 100);
			int h = attrInt(node, "height", 70);
			if (w > x1_ - x0_) w = x1_ - x0_;
			if (h > 160) h = 160;
			if (ix_ + w > x1_) newline(0);
			// case d'image (decode en V0.2)
			(*target_).addRect(ix_, iy_, w, h, 0xFF776666, 1);
			(*target_).fillRect(ix_ + 1, iy_ + 1, w - 2, h - 2, 0xFF463A3A);
			std::string alt = node.getAttr("alt") ? node.getAttr("alt") : "";
			if (!alt.empty())
			{
				TextEngine& te = TextEngine::instance();
				te.drawLine((*target_), alt, ix_ + 3, iy_ + 2, 0.6f, 0xFFA89A9A, false, false);
			}
			const char* src = node.getAttr("src");
			// L'adresse est resolue ici : le magasin d'images doit recevoir
			// une adresse absolue.
			out_->images.push_back({ ix_, iy_, w, h,
				src && *src ? joinUrl(pageUrl_, src) : "", alt });
			if (ix_ + w + 6 > x1_) newline(0);
			else ix_ += w + 6;
			if (h > lineMaxH_) lineMaxH_ = h;
			return;
		}

		if (tag == "hr")
		{
			newline(0);
			int y = iy_ + 2;
			(*target_).fillRect(x0_, y, x1_ - x0_, 1, 0xFF968A8A);
			by_ = iy_ = y + 5;
			ix_ = x0_;
			return;
		}

		if (tag == "input" || tag == "textarea" || tag == "select" || tag == "button")
		{
			const char* type = node.getAttr("type");
			const char* name = node.getAttr("name");
			int kind = FieldText;
			bool isSubmit = false;
			if (tag == "textarea") kind = FieldTextarea;
			else if (tag == "select") kind = FieldSelect;
			else if (tag == "button") isSubmit = true;
			else if (type)
			{
				std::string t = toLower(type);
				if (t == "submit" || t == "button" || t == "image") isSubmit = true;
				else if (t == "password") kind = FieldPassword;
				else if (t == "number") kind = FieldNumber;
				else if (t == "search") kind = FieldSearch;
				else if (t == "email") kind = FieldEmail;
			}

			if (tag == "input" && type && toLower(type) == "hidden")
			{
				if (curFormId_ >= 0)
				{
					FormField ff;
					ff.name = name ? name : "";
					ff.value = node.getAttr("value") ? node.getAttr("value") : "";
					ff.node = idx;
					ff.formId = curFormId_;
					out_->fields.push_back(ff);
				}
				return;
			}

			int w = attrInt(node, "size", 20) * 8;
			if (w > x1_ - x0_) w = x1_ - x0_;
			int h = fieldHeight(kind);

			if (isSubmit)
			{
				newline(0);
				std::string label;
				if (tag == "button")
				{
					label = strTrim(doc_->textOf(idx));
					if (label.empty() && node.getAttr("value")) label = node.getAttr("value");
				}
				else
				{
					label = node.getAttr("value") ? node.getAttr("value") : (node.getAttr("alt") ? node.getAttr("alt") : "");
				}
				if (label.empty()) label = "OK";
				int bw = (int)TextEngine::instance().measure(label, baseScale_ * 0.9f) + 16;
				if (bw < 48) bw = 48;
				(*target_).fillRect(ix_, iy_, bw, 22, 0xFF9E5A2E);
				(*target_).addRect(ix_, iy_, bw, 22, 0xFFE4B89A, 1);
				TextEngine::instance().drawLine((*target_), label, ix_ + 8, iy_ + 3, baseScale_ * 0.9f, C_WHITE, true, false);
				FormSubmit fs;
				fs.x = ix_; fs.y = iy_; fs.w = bw; fs.h = 22;
				fs.node = idx; fs.formId = curFormId_;
				fs.label = label;
				fs.method = curFormMethod_;
				const char* fsa = node.getAttr("formaction");
				fs.action = fsa && *fsa ? fsa : curFormAction_;
				out_->submits.push_back(fs);
				ix_ += bw + 8;
				if (22 > lineMaxH_) lineMaxH_ = 22;
				return;
			}

			newline(0);
			(*target_).fillRect(ix_, iy_, w, h, 0xFFF4F4F4);
			(*target_).addRect(ix_, iy_, w, h, 0xFF968A8A, 1);
			std::string val = node.getAttr("value") ? node.getAttr("value") : "";
			if (tag == "select")
			{
				// Chercher l'option sélectionnée ou la première option
				std::string optText;
				for (int childIdx : node.children)
				{
					const Node& cn = doc_->nodes[childIdx];
					if (cn.isElement() && cn.tag == "option")
					{
						std::string text = strTrim(doc_->textOf(childIdx));
						if (optText.empty() || cn.hasAttr("selected"))
						{
							optText = text;
							val = cn.getAttr("value") ? cn.getAttr("value") : text;
							if (cn.hasAttr("selected")) break;
						}
					}
				}
				if (!optText.empty())
				{
					TextEngine& te = TextEngine::instance();
					std::string shown = optText.size() > 18 ? optText.substr(0, 17) + "..." : optText;
					te.drawLine((*target_), shown + " v", ix_ + 4, iy_ + 3, 0.65f, 0xFF2C2222, false, false);
				}
			}
			else if (type && (toLower(type) == "checkbox" || toLower(type) == "radio"))
			{
				bool checked = node.hasAttr("checked");
				int sz = 14;
				w = sz; h = sz;
				(*target_).fillRect(ix_, iy_ + 2, sz, sz, 0xFFFFFFFF);
				(*target_).addRect(ix_, iy_ + 2, sz, sz, 0xFF968A8A, 1);
				if (checked)
				{
					(*target_).fillRect(ix_ + 3, iy_ + 5, sz - 6, sz - 6, 0xFF9E5A2E);
				}
			}
			else if (!val.empty())
			{
				TextEngine& te = TextEngine::instance();
				std::string shown = val.size() > 18 ? val.substr(0, 17) + "..." : val;
				te.drawLine((*target_), shown, ix_ + 3, iy_ + 3, 0.7f, 0xFF2C2222, false, false);
			}
			FormField ff;
			ff.x = ix_; ff.y = iy_; ff.w = w; ff.h = h;
			ff.kind = kind;
			ff.name = name ? name : "";
			ff.value = val;
			ff.node = idx;
			ff.formId = curFormId_;
			out_->fields.push_back(ff);
			ix_ += w + 8;
			if (h > lineMaxH_) lineMaxH_ = h;
			return;
		}

		//------------------------------------------------------------------------
		// Conteneurs avec fond ou retrait (cartes, bandeaux, encadres).
		//
		// C'est le mecanisme de base d'une page web moderne : une <div> avec une
		// couleur de fond et du padding. Le contenu est rendu DIRECTEMENT dans la
		// page (coordonnees absolues) et seul le rectangle de fond est enregistre
		// ici : il sera peint apres coup, quand la hauteur du bloc sera connue.
		// Peindre le fond d'abord obligerait a un tampon temporaire et a deux
		// systemes de coordonnees, source d'erreurs.
		//------------------------------------------------------------------------
		const bool isContainer =
			(tag == "div") || (tag == "section") || (tag == "article") ||
			(tag == "header") || (tag == "footer") || (tag == "nav") ||
			(tag == "aside") || (tag == "a" && st.block);
		if (isContainer && (st.bgSet || st.paddingLeft > 0 || st.paddingRight > 0 ||
			st.width > 0 || st.minWidth > 0 || st.maxWidth > 0 || st.flex ||
			st.marginLeftAuto || st.marginRightAuto))
		{
			newline(0);
			if (st.marginTop > 0) by_ += st.marginTop;

			// Un lien en bloc doit garder l'apparence d'un lien (couleur,
			// soulignement) et rester cliquable sur toute sa surface.
			const bool isBlockLink = (tag == "a");
			Style contentStyle = st;
			if (isBlockLink)
			{
				contentStyle.color = C_LINK;
				contentStyle.colorSet = true;
				contentStyle.underline = true;
			}

			const int savedX0c = x0_, savedX1c = x1_;
			const int availableWidth = std::max(1, savedX1c - savedX0c);
			int containerWidth = st.width > 0 ? st.width : availableWidth;
			if (st.maxWidth > 0) containerWidth = std::min(containerWidth, st.maxWidth);
			if (st.minWidth > 0) containerWidth = std::max(containerWidth, st.minWidth);
			if (st.borderBox && st.width > 0)
				containerWidth = std::max(1, containerWidth - st.paddingLeft - st.paddingRight);
			containerWidth = std::min(containerWidth, availableWidth);
			if (st.marginLeftAuto && st.marginRightAuto && containerWidth < availableWidth)
				x0_ = savedX0c + (availableWidth - containerWidth) / 2;
			else if (st.marginLeftAuto && containerWidth < availableWidth)
				x0_ = savedX1c - containerWidth;
			x1_ = std::min(savedX1c, x0_ + containerWidth);
			// Bornes du fond : toute la largeur disponible, padding compris.
			const int bgX0 = x0_ + st.paddingLeft;
			const int bgX1 = x1_ - st.paddingRight;
			const int bgY0 = by_;
			// La hauteur du bloc n'est pas connue avant d'avoir rendu ses
			// enfants : elle est mesuree plus bas, puis le fond est peint sous
			// le contenu (voir saveRegion/restoreRegion).
			const bool hasBlockBackground = st.bgSet;

			if (st.paddingLeft > 0) x0_ += st.paddingLeft;
			if (st.paddingRight > 0) x1_ -= st.paddingRight;
			if (x0_ > NW_PAGE_WIDTH - 40) x0_ = NW_PAGE_WIDTH - 40;
			if (x1_ < x0_ + 30) x1_ = x0_ + 30;
			if (x1_ > NW_PAGE_WIDTH - 2) x1_ = NW_PAGE_WIDTH - 2;
			by_ += st.paddingTop;
			iy_ = by_;
			ix_ = x0_;

			// Rendu du contenu. Pour un lien, on active la zone cliquable
			// pendant le rendu des enfants, exactement comme pour un lien en
			// ligne, afin que le texte soit cliquable sur toute la largeur.
			{
				const bool prevActive = linkActive_;
				const std::string prevHref = linkHref_;
				if (isBlockLink)
				{
					const char* href = node.getAttr("href");
					if (href && *href)
					{
						linkActive_ = true;
						linkHref_ = joinUrl(pageUrl_, href);
					}
				}
				if (st.flex && st.flexRow && !st.flexWrap && node.children.size() > 1)
				{
					const int flexX0 = x0_, flexX1 = x1_;
					const int flexY = by_;
					const int flexWidth = flexX1 - flexX0;
					std::vector<int> itemWidths;
					itemWidths.reserve(node.children.size());
					int totalItemsWidth = 0;
					for (int child : node.children)
					{
						const Style itemStyle = css_->resolve(*doc_, child, contentStyle);
						int itemWidth = itemStyle.width;
						if (itemWidth <= 0)
						{
						const std::string label = strTrim(doc_->textOf(child));
						itemWidth = label.empty() ? 48 : (int)TextEngine::instance().measure(label, baseScale_ * itemStyle.scale());
						itemWidth += itemStyle.paddingLeft + itemStyle.paddingRight + 8;
					}
					itemWidth = std::max(24, std::min(itemWidth, flexWidth));
					itemWidths.push_back(itemWidth);
					totalItemsWidth += itemWidth;
				}
				int gap = 0;
				int cursor = flexX0;
				if (st.justifyContent == 1)
					cursor += std::max(0, (flexWidth - totalItemsWidth) / 2);
				else if (st.justifyContent == 2 && node.children.size() > 1)
					gap = std::max(0, (flexWidth - totalItemsWidth) / ((int)node.children.size() - 1));
					int maxBottom = flexY;
					for (size_t i = 0; i < node.children.size(); ++i)
					{
						x0_ = cursor;
						x1_ = std::min(flexX1, cursor + itemWidths[i]);
						ix_ = x0_; iy_ = by_ = flexY; lineMaxH_ = 0;
						resolveAndRender(node.children[i], contentStyle);
						if (by_ > maxBottom) maxBottom = by_;
						cursor = x1_ + gap;
					}
					x0_ = flexX0; x1_ = flexX1; by_ = maxBottom; iy_ = by_; ix_ = x0_;
				}
				else for (int c : node.children) resolveAndRender(c, contentStyle);
				linkActive_ = prevActive;
				linkHref_ = prevHref;
			}
			newline(0);

			if (by_ < bgY0 + 8) by_ = bgY0 + 8;   // bloc vide : garder le fond
			// Interrupteur de diagnostic : NW_NO_BLOCK_BG=1 desactive la
			// peinture des fonds de blocs, pour isoler son effet.
			if (hasBlockBackground)
#ifdef NW_NO_BLOCK_BG
			{
				// desactive volontairement
			}
#else
			{
				// Le fond est peint maintenant, donc APRES le contenu : sans
				// precaution il masquerait le texte. On sauvegarde la zone, on
				// peint le fond, puis on restaure le contenu par-dessus.
				const int bgHeight = by_ - bgY0;
				saveRegion(bgX0, bgY0, bgX1 - bgX0, bgHeight);
				if (!regionSave_.empty())
				{
					(*target_).fillRect(bgX0, bgY0, bgX1 - bgX0, bgHeight, st.bg);
					restoreRegion(bgX0, bgY0, bgX1 - bgX0, bgHeight);
				}
			}
#endif

			x0_ = savedX0c; x1_ = savedX1c;
			by_ += st.paddingBottom + st.marginBottom + uaMarginBottom(node.tag);
			iy_ = by_;
			ix_ = x0_;
			return;
		}

		// --- balises structurelles ---
		if (tag == "form")
		{
			curFormId_ = ++formCounter_;
			const char* fa = node.getAttr("action");
			curFormAction_ = fa && *fa ? fa : "";
			const char* fm = node.getAttr("method");
			curFormMethod_ = fm && *fm ? toLower(fm) : "get";
		}
		if (tag == "ul" || tag == "ol")
		{
			int savedX0 = x0_, savedX1 = x1_, savedIx = ix_;
			newline(0);
			x0_ += 14; x1_ -= 4; ix_ = x0_;
			int listItem = 0;
			for (int c : node.children)
			{
				const Node& cn = doc_->nodes[c];
				if (cn.isElement() && cn.tag == "li")
				{
					listItem++;
					newline(0);
					std::string marker = (tag == "ol") ? itos(listItem) + "." : "-";
					TextEngine& te = TextEngine::instance();
					float mw = te.measure(marker + " ", baseScale_);
					te.drawLine((*target_), marker + " ", x0_ - 14, iy_, baseScale_, 0xFFD4C9C9, false, false);
					ix_ = x0_ - 14 + (int)mw;
					resolveAndRender(c, st);
					(void)mw;
				}
				else resolveAndRender(c, st);
			}
			x0_ = savedX0; x1_ = savedX1; ix_ = savedIx;
			newline(0);
			return;
		}

		if (tag == "table")
		{
			newline(0);
			for (int c : node.children) resolveAndRender(c, st);
			newline(0);
			return;
		}
		if (tag == "tbody" || tag == "thead" || tag == "tfoot")
		{
			for (int c : node.children) resolveAndRender(c, st);
			return;
		}
		if (tag == "tr")
		{
			newline(0);
			std::vector<int> cells;
			for (int c : node.children)
			{
				if (c >= 0 && (size_t)c < doc_->nodes.size())
				{
					const std::string& ct = doc_->nodes[c].tag;
					if (ct == "td" || ct == "th") cells.push_back(c);
				}
			}
			if (cells.empty())
			{
				for (int c : node.children) resolveAndRender(c, st);
				newline(0);
				return;
			}
			const int numCells = (int)cells.size();
			const int totalW = x1_ - x0_;
			const int colW = std::max(20, totalW / numCells);
			const int savedX0 = x0_;
			const int savedX1 = x1_;
			const int rowY = iy_;
			int maxRowBottom = rowY + 14;

			for (int i = 0; i < numCells; i++)
			{
				int cellIdx = cells[i];
				x0_ = savedX0 + i * colW;
				x1_ = (i == numCells - 1) ? savedX1 : (x0_ + colW - 2);
				ix_ = x0_;
				iy_ = rowY;
				by_ = rowY;
				lineMaxH_ = 0;
				resolveAndRender(cellIdx, st);
				if (by_ > maxRowBottom) maxRowBottom = by_;
			}
			x0_ = savedX0;
			x1_ = savedX1;
			by_ = maxRowBottom;
			iy_ = by_;
			ix_ = x0_;
			newline(0);
			return;
		}
		if (tag == "td" || tag == "th")
		{
			Style cellSt = st;
			if (tag == "th") cellSt.bold = true;
			for (int c : node.children) resolveAndRender(c, cellSt);
			return;
		}

		if (tag == "blockquote")
		{
			int savedX0 = x0_, savedX1 = x1_, savedIx = ix_;
			newline(0);
			x0_ += 12; x1_ -= 4; ix_ = x0_;
			(*target_).vline(x0_ - 8, iy_, iy_ + 2, 0xFF968A8A);
			for (int c : node.children) resolveAndRender(c, st);
			x0_ = savedX0; x1_ = savedX1; ix_ = savedIx;
			newline(0);
			return;
		}

		if (tag == "pre")
		{
			newline(0);
			int savedX0 = x0_, savedX1 = x1_;
			x0_ += 6; x1_ -= 6; ix_ = x0_;
			TextEngine& te = TextEngine::instance();
			float scale = baseScale_ * 0.9f;
			int pitch = te.linePitch(scale);
			for (int c : node.children)
			{
				if (doc_->nodes[c].type == NodeType::Text || doc_->nodes[c].type == NodeType::RawText)
				{
					const std::string& raw = doc_->nodes[c].text;
					size_t start = 0;
					while (start < raw.size())
					{
						size_t nl = raw.find('\n', start);
						std::string lineStr = (nl == std::string::npos) ? raw.substr(start) : raw.substr(start, nl - start);
						if (!lineStr.empty() && lineStr.back() == '\r') lineStr.pop_back();
						te.drawLine((*target_), lineStr, ix_, iy_, scale, 0xFFDACFCF, false, false);
						by_ = iy_ += pitch;
						ix_ = x0_;
						if (nl == std::string::npos) break;
						start = nl + 1;
					}
				}
				else
				{
					resolveAndRender(c, st);
				}
			}
			x0_ = savedX0; x1_ = savedX1;
			newline(0);
			return;
		}

		if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6")
		{
			newline(2);
			Style hs = st;
			hs.bold = true;
			switch (tag[1])
			{
				case '1': hs.fontSize = 22; break;
				case '2': hs.fontSize = 19; break;
				case '3': hs.fontSize = 17; break;
				default:  hs.fontSize = 15; break;
			}
			for (int c : node.children) resolveAndRender(c, hs);
			newline(0);
			by_ += 2;
			return;
		}
	}

	// --- noeuds de texte ---
	if (node.type == NodeType::Text)
	{
		drawTextWrapped(node.text, st, false);
		return;
	}

	// --- element generique ---
	if (block)
	{
		newline(0);
		if (st.paddingTop > 0) by_ += st.paddingTop;
		if (st.marginTop > 0) by_ += st.marginTop;
		iy_ = by_; ix_ = x0_;

		for (int c : node.children) resolveAndRender(c, st);
		newline(0);
		by_ += uaMarginBottom(node.tag) + st.marginBottom + st.paddingBottom;
		iy_ = by_;
		if (isForm)
		{
			curFormId_ = previousFormId;
			curFormAction_ = previousFormAction;
			curFormMethod_ = previousFormMethod;
		}
		return;
	}

	// inline : <a>, <b>, <i>, <span>...
	if (node.isElement() && node.tag == "a")
	{
		const char* href = node.getAttr("href");
		bool prevActive = linkActive_;
		std::string prevHref = linkHref_;
		Style as = st;
		as.color = C_LINK; as.colorSet = true; as.underline = true;
		if (href && *href)
		{
			linkActive_ = true;
			linkHref_ = joinUrl(pageUrl_, href);
		}
		for (int c : node.children) resolveAndRender(c, as);
		linkActive_ = prevActive;
		linkHref_ = prevHref;
		return;
	}

	// autre element inline : passe les enfants
	for (int c : node.children) resolveAndRender(c, st);
}

//--------------------------------------------------------------------------
// Resout le style (CSS + heritage) puis rend le sous-arbre.
//--------------------------------------------------------------------------
void PageLayout::resolveAndRender(int idx, const Style& parentSt)
{
	if (idx < 0) return;
	if (renderDepth_ >= 256) return;
	++renderDepth_;
	struct DepthGuard
	{
		int& depth;
		~DepthGuard() { --depth; }
	} guard{ renderDepth_ };

	const Node& node = doc_->nodes[idx];
	if (node.isElement())
	{
		if (isHeadTag(node.tag)) return;
		if (node.hasAttr("hidden")) return;
		const char* aria = node.getAttr("aria-hidden");
		if (aria && (std::string(aria) == "true" || std::string(aria) == "1")) return;
		const char* cls = node.getAttr("class");
		if (cls)
		{
			std::string cl = toLower(cls);
			if (cl.find("sr-only") != std::string::npos ||
				cl.find("visually-hidden") != std::string::npos ||
				cl.find("skip-link") != std::string::npos)
				return;
		}
		if (node.tag == "dialog" && !node.hasAttr("open")) return;
	}
	if (node.type == NodeType::Comment) return;

	Style st = css_->resolve(*doc_, idx, parentSt);
	if (st.displayNone) return;
	// Les liens en ligne prennent un chemin raccourci (couleur de lien,
	// soulignement, zone cliquable). Un lien declare display:block doit au
	// contraire passer par le rendu de bloc, sinon il perd son fond, son
	// padding et sa largeur : c'est ce qui empechait les tuiles de la page
	// d'accueil de s'afficher.
	if (node.isElement() && node.tag == "a" && !st.block)
	{
		Style as = st;
		as.color = C_LINK; as.colorSet = true; as.underline = true;
		const char* href = node.getAttr("href");
		bool prev = linkActive_;
		std::string prevHref = linkHref_;
		if (href && *href) { linkActive_ = true; linkHref_ = joinUrl(pageUrl_, href); }
		for (int c : node.children) resolveAndRender(c, as);
		linkActive_ = prev;
		linkHref_ = prevHref;
		return;
	}
	renderNode(idx, st);
}

//--------------------------------------------------------------------------
// Rendu complet du document.
//--------------------------------------------------------------------------
bool PageLayout::render(PageModel& out, const std::string& pageUrl)
{
	pageUrl_ = pageUrl;
	out_ = &out;
	out.clear();
	if (doc_->empty())
	{
		out.errorCode = 1;
		out.errorText = "Page vide";
		return false;
	}

	int cap = set_->maxPageHeight > 0 ? set_->maxPageHeight : NW_MAX_PAGE_HEIGHT;
	while (!out.bmp.create(NW_PAGE_WIDTH, cap))
	{
		if (cap <= 1200)
		{
			out.errorCode = 2;
			out.errorText = "Memoire insuffisante pour le rendu";
			return false;
		}
		cap = (cap * 2) / 3;
		if (cap < 1200) cap = 1200;
	}
	// Surface de dessin par defaut : la page elle-meme.
	target_ = &out.bmp;
	TextEngine& te = TextEngine::instance();
	if (!te.ready()) te.init();

	// corps de la page : <body> ou <html> ou racine
	int body = doc_->firstByTag(doc_->root, "body");
	if (body < 0) body = doc_->firstByTag(doc_->root, "html");
	if (body < 0) body = doc_->root;

	const bool isInternal = strStartsWith(toLower(pageUrl), "nw://") || strStartsWith(toLower(pageUrl), "nw:/");
	uint32_t defaultBg = isInternal ? 0xFF1A1317 : 0xFFFFFFFF; // Fond sombre prune interne ou blanc web standard
	uint32_t defaultText = isInternal ? C_TEXT : 0xFF202020; // Texte clair interne ou sombre standard

	Style rootSt = defaultStyle();
	rootSt.color = defaultText;
	rootSt.colorSet = true;

	// Peindre le fond du body/html sur toute la page et heriter la couleur
	int htmlNode = doc_->firstByTag(doc_->root, "html");
	if (htmlNode >= 0)
	{
		Style htmlSt = css_->resolve(*doc_, htmlNode, rootSt);
		if (htmlSt.bgSet) defaultBg = htmlSt.bg;
		if (htmlSt.colorSet) { defaultText = htmlSt.color; rootSt.color = defaultText; }
	}
	if (body >= 0 && body != htmlNode)
	{
		Style bodySt = css_->resolve(*doc_, body, rootSt);
		if (bodySt.bgSet) defaultBg = bodySt.bg;
		if (bodySt.colorSet) { defaultText = bodySt.color; rootSt.color = defaultText; }
	}

	// Si le fond est sombre et que la couleur de texte n'a pas ete explicitement definie
	if (!rootSt.colorSet || (defaultBg != 0xFFFFFFFF && defaultText == 0xFF202020))
	{
		uint32_t r = defaultBg & 0xFF;
		uint32_t g = (defaultBg >> 8) & 0xFF;
		uint32_t b = (defaultBg >> 16) & 0xFF;
		int lum = (int)(r * 299 + g * 587 + b * 114) / 1000;
		defaultText = lum < 128 ? C_TEXT : 0xFF202020;
		rootSt.color = defaultText;
		rootSt.colorSet = true;
	}

	defaultTextColor_ = defaultText;
	(*target_).clear(defaultBg);
	pageBg565_ = target_->px ? target_->px[0] : 0xFFFF;

	x0_ = 4; x1_ = NW_PAGE_WIDTH - 4;
	ix_ = x0_; iy_ = 0; by_ = 0; lineMaxH_ = 0;
	renderDepth_ = 0;
	formCounter_ = 0; curFormId_ = -1; curFormAction_.clear(); curFormMethod_.clear();
	linkActive_ = false; linkHref_.clear();

	resolveAndRender(body, rootSt);

	// fin : ferme la derniere ligne
	if (by_ < iy_ + lineMaxH_) by_ = iy_ + lineMaxH_;

	out.height = by_ + 2;
	if (out.height > cap) out.height = cap;

	// associe les champs aux formulaires
	for (auto& fs : out.submits)
	{
		for (const auto& f : out.fields)
			if (f.formId == fs.formId) fs.fields.push_back((int)(&f - out.fields.data()));
	}
	return true;
}
