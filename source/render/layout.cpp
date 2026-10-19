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

static uint32_t compositeColor(uint32_t foreground, uint32_t background)
{
	const unsigned alpha = foreground >> 24;
	if (alpha == 255) return foreground;
	const unsigned inverse = 255 - alpha;
	const unsigned r = (((foreground & 0xFF) * alpha) +
		((background & 0xFF) * inverse)) / 255;
	const unsigned g = ((((foreground >> 8) & 0xFF) * alpha) +
		(((background >> 8) & 0xFF) * inverse)) / 255;
	const unsigned b = ((((foreground >> 16) & 0xFF) * alpha) +
		(((background >> 16) & 0xFF) * inverse)) / 255;
	return 0xFF000000u | (b << 16) | (g << 8) | r;
}

static bool isBlockTag(const std::string& t)
{
	static const char* blocks[] =
	{
		"div","p","h1","h2","h3","h4","h5","h6","ul","ol","li","hr","blockquote","pre",
		"table","tr","td","th","form","address","article","section","nav","header","footer",
		"aside","figure","fieldset","center","body","main","dl","dt","dd","select","textarea",
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
	const int textScale = settings.textScalePercent >= 85 &&
		settings.textScalePercent <= 115 ? settings.textScalePercent : 100;
	baseScale_ *= textScale / 100.0f;
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
	pendingSpace_ = false;
	if (height > 0) by_ += height;
}

void PageLayout::flushLink()
{
}

void PageLayout::addLinkBox(int x0, int y0, int x1, int y1, const std::string& url, int node)
{
	if (x1 <= x0 || y1 <= y0) return;
	if ((int)out_->links.size() >= NW_MAX_LINKS) return;
	out_->links.push_back({ x0, y0, x1 - x0, y1 - y0, url, node });
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
	const int fontSize = st.fontSize > 0 ? st.fontSize : 16;
	const float cssLineHeight = st.lineHeightUnitless
		? fontSize * st.lineHeight / 1000.0f : (float)st.lineHeight;
	int pitch = cssLineHeight > 0.0f
		? std::max(1, (int)ceilf(cssLineHeight * baseScale_))
		: te.linePitch(scale);

	std::string line;
	float lineW = 0.0f;

	auto drawSeg = [&](const std::string& s, float w)
	{
		const int by0 = iy_;
		int drawX = ix_;
		if (st.align == 1)
			drawX = x0_ + std::max(0, (x1_ - x0_ - (int)ceilf(w)) / 2);
		else if (st.align == 2)
			drawX = x1_ - (int)ceilf(w);
		te.drawLine((*target_), s, drawX, iy_, scale, col, bold,
			linkActive_ || st.underline);
		const int bx1 = drawX + (int)ceilf(w);
		// Un lien "display:block" (une tuile, un bouton) doit etre cliquable
		// sur toute sa surface, pas seulement sur la largeur du texte.
		if (linkActive_)
		{
			const int right = st.block ? std::min(x1_, NW_PAGE_WIDTH - 2) : std::min(bx1, NW_PAGE_WIDTH - 2);
			addLinkBox(drawX, by0, right, iy_ + pitch, linkHref_, linkNode_);
		}
		ix_ = std::max(ix_, bx1);
		if (pitch > lineMaxH_) lineMaxH_ = pitch;
	};

	auto placeSegment = [&](const std::string& segment, bool separator)
	{
		std::string cand = line.empty()
			? (separator ? " " + segment : segment)
			: line + " " + segment;
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
	bool sawWhitespace = false;
	bool trailingWhitespace = false;
	while (pos < text.size())
	{
		if (segmented_ && iy_ >= stopAtY_)
		{
			renderStopped_ = true;
			break;
		}
		const size_t whitespaceStart = pos;
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
			text[pos] == '\n' || text[pos] == '\r'))
			++pos;
		sawWhitespace = pos > whitespaceStart;
		if (pos >= text.size()) break;

		size_t end = pos;
		while (end < text.size() && text[end] != ' ' && text[end] != '\t' &&
			text[end] != '\n' && text[end] != '\r')
			++end;
		const std::string word = text.substr(pos, end - pos);
		pos = end;
		bool separator = (sawWhitespace || pendingSpace_) &&
			(ix_ > x0_ || !line.empty());
		pendingSpace_ = false;
		sawWhitespace = false;

		if (te.measure(word, scale) <= maxAvail)
		{
			placeSegment(word, separator);
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
				placeSegment(chunk, separator);
				separator = true;
				chunk.clear();
			}
			chunk += ch;
		}
		if (!chunk.empty()) placeSegment(chunk, separator);
	}
	if (!line.empty()) drawSeg(line, lineW);
	trailingWhitespace = sawWhitespace || (!text.empty() &&
		(text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r'));
	pendingSpace_ = trailingWhitespace && ix_ > x0_;
}

void PageLayout::drawPreformattedText(const std::string& text, const Style& st)
{
	if (text.empty()) return;
	TextEngine& te = TextEngine::instance();
	if (!te.ready()) return;

	const float scale = baseScale_ * st.scale() * (st.mono ? 0.92f : 1.0f);
	const int fontSize = st.fontSize > 0 ? st.fontSize : 16;
	const float cssLineHeight = st.lineHeightUnitless
		? fontSize * st.lineHeight / 1000.0f : (float)st.lineHeight;
	const int pitch = cssLineHeight > 0.0f
		? std::max(1, (int)ceilf(cssLineHeight * baseScale_))
		: te.linePitch(scale);
	const uint32_t color = st.colorSet ? st.color : defaultTextColor_;

	size_t start = 0;
	while (start <= text.size())
	{
		size_t newlinePos = text.find('\n', start);
		std::string line = newlinePos == std::string::npos
			? text.substr(start) : text.substr(start, newlinePos - start);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::string expanded;
		for (char c : line)
		{
			if (c == '\t') expanded += "    ";
			else expanded += c;
		}
		if (!expanded.empty())
		{
			te.drawLine(*target_, expanded, ix_, iy_, scale, color,
				st.bold, st.underline);
			ix_ += (int)ceilf(te.measure(expanded, scale));
			lineMaxH_ = std::max(lineMaxH_, pitch);
		}
		if (newlinePos == std::string::npos) break;
		if (lineMaxH_ < pitch) newline(pitch - lineMaxH_);
		else newline(0);
		start = newlinePos + 1;
	}
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
	const bool block = node.isElement() && !st.displayInline && (isBlockTag(node.tag) || st.block);
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
		if (tag == "noscript" && set_->enableJS) return;
		if (tag == "meta")
		{
			const char* name = node.getAttr("name");
			const char* content = node.getAttr("content");
			if (name && content && *content)
			{
				std::string n = toLower(name);
				if (n == "csrf-token" || n == "csrf_token" || n == "_token" || n == "csrf")
					out_->csrfToken = content;
				else if (n == "csrf-param" || n == "csrf_param")
					out_->csrfParam = content;
			}
			return;
		}
		if (isHeadTag(tag)) return;

		if (tag == "br")
		{
			const int previousLineHeight = lineMaxH_;
			newline(0);
			if (previousLineHeight == 0)
				by_ += TextEngine::instance().linePitch(baseScale_) / 2;
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
				else if (t == "checkbox") kind = FieldCheckbox;
				else if (t == "radio") kind = FieldRadio;
			}

			if (tag == "input" && type && toLower(type) == "hidden")
			{
				FormField ff;
				ff.name = name ? name : "";
				ff.value = node.getAttr("value") ? node.getAttr("value") : "";
				ff.node = idx;
				ff.formId = curFormId_;
				ff.disabled = node.hasAttr("disabled");
				out_->fields.push_back(ff);
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
				fs.name = name ? name : "";
				fs.value = node.getAttr("value") ? node.getAttr("value") : "";
				fs.method = curFormMethod_;
				const char* fsa = node.getAttr("formaction");
				fs.action = fsa && *fsa ? fsa : curFormAction_;

				// Si le bouton est dans un lien <a href="...">, lier la zone au lien
				if (linkActive_)
				{
					fs.action = linkHref_;
					fs.method = "get";
					addLinkBox(ix_, iy_, ix_ + bw, iy_ + 22, linkHref_);
				}
				else if (fs.action.empty())
				{
					// Extraire une URL eventuelle depuis onclick, data-href, href
					const char* href = node.getAttr("href");
					if (!href) href = node.getAttr("data-href");
					if (!href) href = node.getAttr("data-url");
					if (href && *href)
					{
						fs.action = href;
						fs.method = "get";
					}
					else
					{
						const char* oc = node.getAttr("onclick");
						if (oc && *oc)
						{
							std::string soc = oc;
							if (soc.find("history.back") != std::string::npos)
							{
								fs.action = "nw://back";
							}
							else if (soc.find("location.reload") != std::string::npos)
							{
								fs.action = "nw://reload";
							}
							else
							{
								size_t locPos = soc.find("location");
								if (locPos != std::string::npos)
								{
									size_t eqPos = soc.find('=', locPos);
									if (eqPos != std::string::npos)
									{
										size_t q1 = soc.find_first_of("'\"", eqPos);
										if (q1 != std::string::npos)
										{
											size_t q2 = soc.find_first_of("'\"", q1 + 1);
											if (q2 != std::string::npos)
											{
												fs.action = soc.substr(q1 + 1, q2 - q1 - 1);
												fs.method = "get";
											}
										}
									}
								}
							}
						}
					}
				}

				out_->submits.push_back(fs);
				ix_ += bw + 8;
				if (22 > lineMaxH_) lineMaxH_ = 22;
				return;
			}

			newline(0);
			(*target_).fillRect(ix_, iy_, w, h, 0xFFF4F4F4);
			(*target_).addRect(ix_, iy_, w, h, 0xFF968A8A, 1);
			std::string val = node.getAttr("value") ? node.getAttr("value") : "";
			if (tag == "textarea" && val.empty()) val = doc_->textOf(idx);
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
				if (val.empty()) val = "on";
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
				std::string shown = val;
				if (kind == FieldPassword)
				{
					shown.clear();
					for (size_t offset = 0; offset < val.size(); )
					{
						uint32_t cp;
						int bytes = utf8Decode(val.c_str() + offset, cp);
						if (bytes <= 0) break;
						shown += '*';
						offset += (size_t)bytes;
					}
				}
				if (shown.size() > 18) shown = shown.substr(0, 17) + "...";
				te.drawLine((*target_), shown, ix_ + 3, iy_ + 3, 0.7f, 0xFF2C2222, false, false);
			}
			FormField ff;
			ff.x = ix_; ff.y = iy_; ff.w = w; ff.h = h;
			ff.kind = kind;
			ff.name = name ? name : "";
			ff.value = val;
			ff.node = idx;
			ff.formId = curFormId_;
			ff.checked = node.hasAttr("checked");
			ff.disabled = node.hasAttr("disabled");
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
		const bool specialBlock = tag == "ul" || tag == "ol" || tag == "table" ||
			tag == "tr" || tag == "blockquote" ||
			tag == "select" || tag == "textarea";
		if (isForm)
		{
			curFormId_ = ++formCounter_;
			const char* fa = node.getAttr("action");
			curFormAction_ = fa && *fa ? fa : "";
			const char* fm = node.getAttr("method");
			curFormMethod_ = fm && *fm ? toLower(fm) : "get";
		}
		const bool isContainer = (block && !specialBlock) || (tag == "a" && st.block);
		if (isContainer && (st.flex || st.grid || st.bgSet || st.paddingLeft > 0 ||
			st.paddingRight > 0 || st.paddingTop > 0 || st.paddingBottom > 0 ||
			st.width > 0 || st.minWidth > 0 || st.maxWidth > 0 ||
			st.height > 0 || st.minHeight > 0 || st.maxHeight > 0 || st.flex ||
			st.marginLeftAuto || st.marginRightAuto || st.borderWidth > 0 ||
			st.borderRadius > 0 || st.boxShadowSet))
		{
			newline(0);
			if (st.marginTop > 0) by_ += st.marginTop;

			// Un lien en bloc doit garder l'apparence d'un lien (couleur,
			// soulignement) et rester cliquable sur toute sa surface.
			const bool isBlockLink = (tag == "a");
			Style contentStyle = st;
			if (isBlockLink)
			{
				if (!(st.declared & CssPropColor))
				{
					contentStyle.color = C_LINK;
					contentStyle.colorSet = true;
				}
				contentStyle.underline = true;
			}
			const bool heading = tag.size() == 2 && tag[0] == 'h' &&
				tag[1] >= '1' && tag[1] <= '6';
			if (heading)
			{
				if (!(st.declared & CssPropBold)) contentStyle.bold = true;
				if (!(st.declared & CssPropFontSize))
				{
					switch (tag[1])
					{
					case '1': contentStyle.fontSize = 22; break;
					case '2': contentStyle.fontSize = 19; break;
					case '3': contentStyle.fontSize = 17; break;
					default: contentStyle.fontSize = 15; break;
					}
				}
				if (tag == "th" && !(st.declared & CssPropBold)) contentStyle.bold = true;
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
			const int bgX0 = x0_;
			const int bgX1 = x1_;
			const int bgY0 = by_;
			// La hauteur est mesuree lors d'un premier passage. Si le bloc a un
			// fond, son contenu est ensuite redessine par-dessus ce fond.
			const bool hasBlockBackground = st.bgSet || st.boxShadowSet ||
				st.borderWidth > 0;

			if (st.paddingLeft > 0) x0_ += st.paddingLeft;
			if (st.paddingRight > 0) x1_ -= st.paddingRight;
			if (x0_ > NW_PAGE_WIDTH - 40) x0_ = NW_PAGE_WIDTH - 40;
			if (x1_ < x0_ + 30) x1_ = x0_ + 30;
			if (x1_ > NW_PAGE_WIDTH - 2) x1_ = NW_PAGE_WIDTH - 2;
			by_ += st.paddingTop;
			iy_ = by_;
			ix_ = x0_;

			const int contentX0 = x0_, contentX1 = x1_;
			const int contentIx = ix_, contentIy = iy_, contentBy = by_;
			const int contentLineHeight = lineMaxH_;
			const bool contentPendingSpace = pendingSpace_;
			const int formCounterBefore = formCounter_;
			const size_t linksBefore = out_->links.size();
			const size_t fieldsBefore = out_->fields.size();
			const size_t submitsBefore = out_->submits.size();
			const size_t imagesBefore = out_->images.size();
			auto renderChildren = [&]()
			{
				// Pour un lien bloc, rendre aussi les zones cliquables sur la
				// surface complete, comme pour un lien en ligne.
				const bool prevActive = linkActive_;
				const std::string prevHref = linkHref_;
				const int prevLinkNode = linkNode_;
				if (isBlockLink)
				{
					const char* href = node.getAttr("href");
					if (href && *href)
					{
						linkActive_ = true;
						linkHref_ = joinUrl(pageUrl_, href);
						linkNode_ = idx;
					}
				}
				if (st.grid && !node.children.empty())
				{
					const int gridX0 = x0_, gridX1 = x1_;
					const int gridY = by_;
					const int gridWidth = std::max(1, gridX1 - gridX0);
					std::vector<CssGridTrack> tracks;
					const CssGridTrack* autoRepeatTrack = nullptr;
					for (const CssGridTrack& track : st.gridColumns)
					{
						if (track.autoRepeat)
						{
							autoRepeatTrack = &track;
							continue;
						}
						tracks.push_back(track);
					}
					if (autoRepeatTrack)
					{
						int otherWidth = 0;
						for (const CssGridTrack& track : tracks)
							otherWidth += std::max(track.fixedWidth, track.minWidth);
						const int otherGaps = std::max(0,
							(int)tracks.size() - 1) * st.columnGap;
						const int minTrackWidth = std::max(1,
							std::max(autoRepeatTrack->fixedWidth,
								autoRepeatTrack->minWidth));
						const int repeatCount = std::max(1, std::min(12,
							(gridWidth - otherWidth - otherGaps + st.columnGap) /
								(minTrackWidth + st.columnGap)));
						const int visibleCount = autoRepeatTrack->autoFit
							? std::min(repeatCount, (int)node.children.size())
							: repeatCount;
						for (int i = 0; i < visibleCount &&
							tracks.size() < 12; ++i)
							tracks.push_back(*autoRepeatTrack);
					}
					if (tracks.empty()) tracks.push_back({ 0, 1 });

					const int gapsWidth = st.columnGap *
						(std::max(0, (int)tracks.size() - 1));
					const int trackSpace = std::max(0, gridWidth - gapsWidth);
					int fixedWidth = 0, totalFraction = 0;
					for (const CssGridTrack& track : tracks)
					{
						fixedWidth += std::max(track.fixedWidth, track.minWidth);
						totalFraction += std::max(0, track.fraction);
					}
					const int fractionSpace = std::max(0, trackSpace - fixedWidth);
					std::vector<int> widths(tracks.size(), 0);
					int allocated = 0;
					for (size_t i = 0; i < tracks.size(); ++i)
					{
						if (tracks[i].fraction > 0 && totalFraction > 0)
							widths[i] = std::max(tracks[i].fixedWidth,
								tracks[i].minWidth) +
								(int)((long long)fractionSpace *
									tracks[i].fraction / totalFraction);
						else
							widths[i] = std::max(tracks[i].fixedWidth,
								tracks[i].minWidth);
						allocated += widths[i];
					}
					for (size_t i = 0; allocated < trackSpace && i < widths.size(); ++i)
					{
						if (tracks[i].fraction <= 0) continue;
						++widths[i];
						++allocated;
					}

					size_t childIndex = 0;
					int rowY = gridY;
					int maxBottom = gridY;
					while (childIndex < node.children.size())
					{
						const size_t firstChildInRow = childIndex;
						size_t column = 0;
						int rowBottom = rowY;
						while (childIndex < node.children.size())
						{
							const int child = node.children[childIndex];
							const Style itemStyle = css_->resolve(*doc_, child, contentStyle);
							const size_t span = (size_t)std::max(1,
								std::min((int)tracks.size(), itemStyle.gridColumnSpan));
							if (column + span > tracks.size()) break;

							int cellX = gridX0;
							for (size_t i = 0; i < column; ++i)
								cellX += widths[i] + st.columnGap;
							int cellWidth = 0;
							for (size_t i = column; i < column + span; ++i)
								cellWidth += widths[i];
							cellWidth += st.columnGap * (int)(span - 1);

							x0_ = cellX;
							x1_ = std::min(gridX1, cellX + cellWidth);
							ix_ = x0_; iy_ = by_ = rowY; lineMaxH_ = 0;
							resolveAndRender(child, contentStyle);
							rowBottom = std::max(rowBottom,
								std::max(by_, iy_ + lineMaxH_));
							column += span;
							++childIndex;
						}
						if (childIndex < node.children.size() &&
							childIndex == firstChildInRow)
						{
							const int child = node.children[childIndex];
							const Style itemStyle = css_->resolve(*doc_, child, contentStyle);
							const size_t span = (size_t)std::max(1,
								std::min((int)tracks.size(), itemStyle.gridColumnSpan));
							int cellX = gridX0;
							int cellWidth = 0;
							for (size_t i = 0; i < span; ++i)
							{
								cellWidth += widths[i];
								if (i + 1 < span) cellWidth += st.columnGap;
							}
							x0_ = cellX;
							x1_ = std::min(gridX1, cellX + cellWidth);
							ix_ = x0_; iy_ = by_ = rowY; lineMaxH_ = 0;
							resolveAndRender(child, contentStyle);
							rowBottom = std::max(by_, iy_ + lineMaxH_);
							++childIndex;
						}
						maxBottom = std::max(maxBottom, rowBottom);
						if (childIndex < node.children.size())
							rowY = rowBottom + st.rowGap;
					}
					x0_ = gridX0; x1_ = gridX1; by_ = maxBottom;
					iy_ = by_; ix_ = x0_;
				}
				else if (st.flex && st.flexRow && node.children.size() > 1)
				{
					const int flexX0 = x0_, flexX1 = x1_;
					const int flexY = by_;
					const int flexWidth = flexX1 - flexX0;
					std::vector<int> itemWidths;
					itemWidths.reserve(node.children.size());
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
					}
					size_t rowStart = 0;
					int rowY = flexY;
					int maxBottom = flexY;
					while (rowStart < node.children.size())
					{
						size_t rowEnd = rowStart + 1;
						int rowItemsWidth = itemWidths[rowStart];
						if (st.flexWrap)
						{
							while (rowEnd < node.children.size() &&
								rowItemsWidth + st.columnGap + itemWidths[rowEnd] <= flexWidth)
							{
								rowItemsWidth += st.columnGap + itemWidths[rowEnd];
								++rowEnd;
							}
						}
						else
						{
							for (; rowEnd < node.children.size(); ++rowEnd)
								rowItemsWidth += st.columnGap + itemWidths[rowEnd];
						}

						const size_t rowCount = rowEnd - rowStart;
						int extraGap = st.columnGap;
						int cursor = flexX0;
						const int freeSpace = std::max(0, flexWidth - rowItemsWidth);
						if (st.justifyContent == 1)
							cursor += freeSpace / 2;
						else if (st.justifyContent == 2 && rowCount > 1)
							extraGap += freeSpace / (int)(rowCount - 1);
						else if (st.justifyContent == 3)
							cursor += freeSpace;
						else if (st.justifyContent == 4 && rowCount > 0)
						{
							const int distributed = freeSpace / (int)rowCount;
							cursor += distributed / 2;
							extraGap += distributed;
						}
						else if (st.justifyContent == 5)
						{
							const int distributed = freeSpace /
								((int)rowCount + 1);
							cursor += distributed;
							extraGap += distributed;
						}

						int rowBottom = rowY;
						for (size_t i = rowStart; i < rowEnd; ++i)
						{
							x0_ = cursor;
							x1_ = std::min(flexX1, cursor + itemWidths[i]);
							ix_ = x0_; iy_ = by_ = rowY; lineMaxH_ = 0;
							resolveAndRender(node.children[i], contentStyle);
							if (by_ > rowBottom) rowBottom = by_;
							cursor = x1_ + extraGap;
						}
						maxBottom = std::max(maxBottom, rowBottom);
						rowStart = rowEnd;
						if (rowStart < node.children.size())
							rowY = rowBottom + st.rowGap;
					}
					x0_ = flexX0; x1_ = flexX1; by_ = maxBottom; iy_ = by_; ix_ = x0_;
				}
				else if (st.flex && !st.flexRow)
				{
					bool firstItem = true;
					for (int c : node.children)
					{
						if (!firstItem)
						{
							newline(0);
							by_ += st.rowGap;
							iy_ = by_;
							ix_ = x0_;
						}
						resolveAndRender(c, contentStyle);
						newline(0);
						firstItem = false;
					}
				}
				else for (int c : node.children) resolveAndRender(c, contentStyle);
				linkActive_ = prevActive;
				linkHref_ = prevHref;
				linkNode_ = prevLinkNode;
			};

			renderChildren();
			newline(0);

			int desiredContentHeight = st.height;
			if (st.minHeight > 0)
				desiredContentHeight = std::max(desiredContentHeight, st.minHeight);
			if (st.maxHeight > 0 && desiredContentHeight > st.maxHeight)
				desiredContentHeight = st.maxHeight;
			if (desiredContentHeight > 0)
				by_ = std::max(by_, contentIy + desiredContentHeight);
			const int measuredContentBottom = by_;

			if (by_ < bgY0 + 8) by_ = bgY0 + 8;   // bloc vide : garder le fond
#ifdef NW_NO_BLOCK_BG
			(void)hasBlockBackground;
#else
			if (hasBlockBackground)
			{
				const int bgHeight = by_ + st.paddingBottom - bgY0;
				const int radius = std::min(st.borderRadius,
					std::min(bgX1 - bgX0, bgHeight) / 2);
				if (st.boxShadowSet)
					(*target_).shadowRoundedRect(bgX0, bgY0, bgX1 - bgX0,
						bgHeight, radius, st.shadowX, st.shadowY, st.shadowBlur,
						st.shadowSpread, st.shadowColor);
				if (st.gradientCount > 1)
				{
					if (st.gradientRadial)
						(*target_).fillRadialGradientRect(bgX0, bgY0,
							bgX1 - bgX0, bgHeight, st.gradientColors,
							st.gradientStops, st.gradientCount, radius);
					else
						(*target_).fillLinearGradientRect(bgX0, bgY0,
							bgX1 - bgX0, bgHeight, st.gradientColors,
							st.gradientStops, st.gradientCount,
							st.gradientAngle, radius);
				}
				else if (st.bgSet)
				{
					if (radius > 0)
						(*target_).fillRoundedRect(bgX0, bgY0, bgX1 - bgX0,
							bgHeight, radius, st.bg);
					else
						(*target_).blendRect(bgX0, bgY0, bgX1 - bgX0, bgHeight, st.bg);
				}
				if (st.borderWidth > 0)
					(*target_).addRoundedRect(bgX0, bgY0, bgX1 - bgX0,
						bgHeight, radius, st.borderColor,
						std::min(4, st.borderWidth));

				x0_ = contentX0; x1_ = contentX1;
				ix_ = contentIx; iy_ = contentIy; by_ = contentBy;
				lineMaxH_ = contentLineHeight;
				pendingSpace_ = contentPendingSpace;
				out_->links.resize(linksBefore);
				out_->fields.resize(fieldsBefore);
				out_->submits.resize(submitsBefore);
				out_->images.resize(imagesBefore);
				formCounter_ = formCounterBefore;
				const bool stoppedDuringMeasure = renderStopped_;
				renderStopped_ = false;
				renderChildren();
				newline(0);
				renderStopped_ = renderStopped_ || stoppedDuringMeasure;
				by_ = std::max(by_, measuredContentBottom);
				if (by_ < bgY0 + 8) by_ = bgY0 + 8;
			}
#endif

			x0_ = savedX0c; x1_ = savedX1c;
			by_ += st.paddingBottom + st.marginBottom + uaMarginBottom(node.tag);
			iy_ = by_;
			ix_ = x0_;
			if (isForm)
			{
				curFormId_ = previousFormId;
				curFormAction_ = previousFormAction;
				curFormMethod_ = previousFormMethod;
			}
			return;
		}

		// --- balises structurelles ---
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
					std::string marker = st.listStyleNone ? "" :
						((tag == "ol") ? itos(listItem) + "." : "-");
					TextEngine& te = TextEngine::instance();
					float mw = marker.empty() ? 0.0f : te.measure(marker + " ", baseScale_);
					if (!marker.empty())
						te.drawLine((*target_), marker + " ", x0_ - 14, iy_, baseScale_, 0xFFD4C9C9, false, false);
					ix_ = marker.empty() ? x0_ : x0_ - 14 + (int)mw;
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
			for (int c : node.children)
			{
				if (doc_->nodes[c].type == NodeType::Text || doc_->nodes[c].type == NodeType::RawText)
					drawPreformattedText(doc_->nodes[c].text, st);
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
		bool insidePre = false;
		for (int parent = node.parent; parent >= 0 &&
			(size_t)parent < doc_->nodes.size();
			parent = doc_->nodes[parent].parent)
		{
			if (doc_->nodes[parent].isElement() &&
				doc_->nodes[parent].tag == "pre")
			{
				insidePre = true;
				break;
			}
		}
		if (insidePre)
			drawPreformattedText(node.text, st);
		else
			drawTextWrapped(node.text, st, st.mono);
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
		int prevLinkNode = linkNode_;
		Style as = st;
		if (!(st.declared & CssPropColor)) { as.color = C_LINK; as.colorSet = true; }
		as.underline = true;
		if (href && *href)
		{
			linkActive_ = true;
			linkHref_ = joinUrl(pageUrl_, href);
			linkNode_ = idx;
		}
		for (int c : node.children) resolveAndRender(c, as);
		linkActive_ = prevActive;
		linkHref_ = prevHref;
		linkNode_ = prevLinkNode;
		return;
	}

	// autre element inline : passe les enfants
	for (int c : node.children) resolveAndRender(c, st);
	if (isForm)
	{
		curFormId_ = previousFormId;
		curFormAction_ = previousFormAction;
		curFormMethod_ = previousFormMethod;
	}
}

//--------------------------------------------------------------------------
// Resout le style (CSS + heritage) puis rend le sous-arbre.
//--------------------------------------------------------------------------
void PageLayout::resolveAndRender(int idx, const Style& parentSt)
{
	if (renderStopped_) return;
	if (segmented_ && by_ >= stopAtY_)
	{
		renderStopped_ = true;
		return;
	}
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
		if (aria && (toLower(aria) == "true" || std::string(aria) == "1")) return;
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
	if (node.isElement() && node.tag == "code") st.mono = true;
	if (node.isElement() && node.tag == "pre" &&
		!(st.declared & CssPropMono))
		st.mono = true;
	if (idx == pageBackgroundNode_)
	{
		st.bgSet = false;
		st.gradientCount = 0;
	}
	if (st.displayNone) return;
	// Les liens en ligne prennent un chemin raccourci (couleur de lien,
	// soulignement, zone cliquable). Un lien declare display:block doit au
	// contraire passer par le rendu de bloc, sinon il perd son fond, son
	// padding et sa largeur : c'est ce qui empechait les tuiles de la page
	// d'accueil de s'afficher.
	if (node.isElement() && node.tag == "a" && !st.block)
	{
		Style as = st;
		if (!(st.declared & CssPropColor)) { as.color = C_LINK; as.colorSet = true; }
		as.underline = true;
		const char* href = node.getAttr("href");
		bool prev = linkActive_;
		std::string prevHref = linkHref_;
		int prevLinkNode = linkNode_;
		if (href && *href) { linkActive_ = true; linkHref_ = joinUrl(pageUrl_, href); linkNode_ = idx; }
		for (int c : node.children) resolveAndRender(c, as);
		linkActive_ = prev;
		linkHref_ = prevHref;
		linkNode_ = prevLinkNode;
		return;
	}
	renderNode(idx, st);
}

//--------------------------------------------------------------------------
// Rendu complet du document.
//--------------------------------------------------------------------------
bool PageLayout::render(PageModel& out, const std::string& pageUrl)
{
	return renderInternal(out, pageUrl, 0,
		set_->maxPageHeight > 0 ? set_->maxPageHeight : NW_MAX_PAGE_HEIGHT, false);
}

bool PageLayout::renderSegment(PageModel& out, const std::string& pageUrl,
	int segmentIndex, int segmentHeight)
{
	if (segmentIndex < 0 || segmentIndex >= NW_MAX_PAGE_SEGMENTS ||
		segmentHeight <= 0) return false;
	const int segmentOffset = segmentIndex * NW_PAGE_SEGMENT_HEIGHT -
		(segmentIndex > 0 ? NW_PAGE_SEGMENT_OVERLAP : 0);
	segmentHeight = std::min(segmentHeight,
		NW_MAX_PAGE_CONTENT_HEIGHT - segmentOffset);
	return renderInternal(out, pageUrl, segmentIndex, segmentHeight, true);
}

bool PageLayout::renderInternal(PageModel& out, const std::string& pageUrl,
	int segmentIndex, int segmentHeight, bool segmented)
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
	for (const Node& node : doc_->nodes)
		if (node.isElement() && node.tag == "script")
		{
			out.jsUsed = true;
			break;
		}

	int cap = segmentHeight;
	const int segmentOffset = segmented && segmentIndex > 0
		? segmentIndex * NW_PAGE_SEGMENT_HEIGHT - NW_PAGE_SEGMENT_OVERLAP : 0;
	segmented_ = segmented;
	stopAtY_ = segmentOffset + cap;
	renderStopped_ = false;
	out.segmentIndex = segmentIndex;
	out.furthestSegmentIndex = segmentIndex;
	if (segmented && !out.bmp.create(NW_PAGE_WIDTH, cap, segmentOffset))
	{
		out.errorCode = 2;
		out.errorText = "Memoire insuffisante pour le segment";
		return false;
	}
	while (!segmented && !out.bmp.create(NW_PAGE_WIDTH, cap, segmentOffset))
	{
		if (cap <= 1200)
		{
			out.errorCode = 2;
			out.errorText = "Memoire insuffisante pour le rendu";
			return false;
		}
		segmented_ = segmented;
		stopAtY_ = segmentOffset + cap;
		renderStopped_ = false;
		out.segmentIndex = segmentIndex;
		out.furthestSegmentIndex = segmentIndex;
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
	Style pageBackground;
	pageBackgroundNode_ = -1;

	Style rootSt = defaultStyle();
	rootSt.color = defaultText;
	rootSt.colorSet = true;
	Style htmlSt = rootSt;

	// Peindre le fond du body/html sur toute la page et heriter la couleur
	int htmlNode = doc_->firstByTag(doc_->root, "html");
	if (htmlNode >= 0)
	{
		htmlSt = css_->resolve(*doc_, htmlNode, rootSt);
		if (htmlSt.bgSet)
		{
			if (htmlSt.gradientCount <= 1)
				defaultBg = compositeColor(htmlSt.bg, defaultBg);
			pageBackground = htmlSt;
			pageBackgroundNode_ = htmlNode;
		}
		if (htmlSt.colorSet) { defaultText = htmlSt.color; rootSt.color = defaultText; }
		rootSt.customProperties = htmlSt.customProperties;
	}
	if (body >= 0 && body != htmlNode)
	{
		Style bodySt = css_->resolve(*doc_, body, htmlSt);
		if (bodySt.bgSet)
		{
			if (bodySt.gradientCount <= 1)
				defaultBg = compositeColor(bodySt.bg, defaultBg);
			pageBackground = bodySt;
			pageBackgroundNode_ = body;
		}
		if (bodySt.colorSet) { defaultText = bodySt.color; rootSt.color = defaultText; }
		rootSt.customProperties = bodySt.customProperties;
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
	if (pageBackground.gradientCount > 1)
	{
		if (pageBackground.gradientRadial)
			(*target_).fillRadialGradientRect(0, segmentOffset,
				NW_PAGE_WIDTH, cap, pageBackground.gradientColors,
				pageBackground.gradientStops, pageBackground.gradientCount);
		else
			(*target_).fillLinearGradientRect(0, segmentOffset,
				NW_PAGE_WIDTH, cap, pageBackground.gradientColors,
				pageBackground.gradientStops, pageBackground.gradientCount,
				pageBackground.gradientAngle);
	}

	x0_ = 4; x1_ = NW_PAGE_WIDTH - 4;
	ix_ = x0_; iy_ = 0; by_ = 0; lineMaxH_ = 0;
	pendingSpace_ = false;
	renderDepth_ = 0;
	formCounter_ = 0; curFormId_ = -1; curFormAction_.clear(); curFormMethod_.clear();
	linkActive_ = false; linkHref_.clear();

	resolveAndRender(body, rootSt);

	// fin : ferme la derniere ligne
	if (by_ < iy_ + lineMaxH_) by_ = iy_ + lineMaxH_;

	out.height = by_ + 2;
	if (segmented)
	{
		out.truncated = renderStopped_ || out.height > stopAtY_;
		if (out.truncated && out.height < stopAtY_) out.height = stopAtY_;
		if (out.height > NW_MAX_PAGE_CONTENT_HEIGHT)
			out.height = NW_MAX_PAGE_CONTENT_HEIGHT;
	}
	else if (out.height >= cap)
	{
		out.height = cap;
		out.truncated = true;
	}

	// associe les champs aux formulaires
	for (auto& fs : out.submits)
	{
		for (const auto& f : out.fields)
		{
			// Si le champ appartient a ce formulaire, ou si le champ est orphelin (ex: input CSRF)
			if (f.formId == fs.formId || (f.formId < 0 && (out.submits.size() == 1 || fs.formId == 1)))
				fs.fields.push_back((int)(&f - out.fields.data()));
		}
	}
	return true;
}
