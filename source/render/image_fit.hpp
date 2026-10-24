#pragma once

#include "../css/css.hpp"

struct ImageFitRect
{
	float destX = 0.0f, destY = 0.0f;
	float destW = 0.0f, destH = 0.0f;
	float sourceX = 0.0f, sourceY = 0.0f;
	float sourceW = 0.0f, sourceH = 0.0f;
};

inline bool calculateImageFitRect(int boxWidth, int boxHeight,
	int imageWidth, int imageHeight, CssImageFit fit,
	int positionX, int positionY, ImageFitRect& rect)
{
	if (boxWidth <= 0 || boxHeight <= 0 || imageWidth <= 0 || imageHeight <= 0)
		return false;

	rect.destX = 0.0f;
	rect.destY = 0.0f;
	rect.destW = (float)boxWidth;
	rect.destH = (float)boxHeight;
	rect.sourceX = 0.0f;
	rect.sourceY = 0.0f;
	rect.sourceW = (float)imageWidth;
	rect.sourceH = (float)imageHeight;

	if (fit == CssImageFitContain)
	{
		const float scale = std::min((float)boxWidth / imageWidth,
			(float)boxHeight / imageHeight);
		rect.destW = imageWidth * scale;
		rect.destH = imageHeight * scale;
		rect.destX = (boxWidth - rect.destW) *
			(std::max(0, std::min(100, positionX)) / 100.0f);
		rect.destY = (boxHeight - rect.destH) *
			(std::max(0, std::min(100, positionY)) / 100.0f);
	}
	else if (fit == CssImageFitCover)
	{
		const float scale = std::max((float)boxWidth / imageWidth,
			(float)boxHeight / imageHeight);
		rect.sourceW = boxWidth / scale;
		rect.sourceH = boxHeight / scale;
		rect.sourceX = (imageWidth - rect.sourceW) *
			(std::max(0, std::min(100, positionX)) / 100.0f);
		rect.sourceY = (imageHeight - rect.sourceH) *
			(std::max(0, std::min(100, positionY)) / 100.0f);
	}

	return rect.destW > 0.0f && rect.destH > 0.0f &&
		rect.sourceW > 0.0f && rect.sourceH > 0.0f;
}
