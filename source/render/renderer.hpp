#pragma once
//=============================================================================
// renderer.hpp - Affichage du PageModel sur l'ecran haut (C3D/Citro2D)
//=============================================================================
#include "../common.hpp"
#include "bitmap.hpp"
#include "layout.hpp"
#include "../image/image_store.hpp"
#include <citro2d.h>
#include <string>

class PageRenderer
{
public:
	bool init();
	void shutdown();

	// Reconstruit l'image de l'ecran a partir de la page + offset de defilement.
	void draw(const PageModel& page, int scrollY, int scrollX = 0,
		int zoomPercent = 100);

	// Images de la page (vignettes), dessinees par-dessus la texture.
	void setImageStore(ImageStore* store) { images_ = store; }

	bool imageDirty() const { return dirty_; }
	void markClean() { dirty_ = false; }
	void forceRedraw() { dirty_ = true; }

private:
	C3D_Tex tex{};
	void* texData = nullptr;
	bool texReady = false;
	bool dirty_ = true;
	int layoutW_ = 0, layoutH_ = 0;
	int lastScroll_ = -1;
	int lastScrollX_ = -1;
	Tex3DS_SubTexture subtex_{};
	C2D_Image image{};
	ImageStore* images_ = nullptr;

	void upload(const PageModel& page, int scrollY, int scrollX, float scale);
	void drawImages(const PageModel& page, int scrollY, int scrollX,
		float scale);
	void drawFixedBoxes(const PageModel& page, int scrollX, float scale);
};