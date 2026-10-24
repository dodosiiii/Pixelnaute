#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct PdfTextRun
{
	std::string text;
	int fontSize = 12;
	bool bold = false;
	bool italic = false;
	bool monospace = false;
	uint32_t color = 0xFF111111;
};

struct PdfDocument
{
	std::vector<std::string> pages;
	std::vector<std::vector<PdfTextRun>> pageRuns;
};

struct PdfFileInfo
{
	std::string path;
	std::string name;
	uint64_t size = 0;
};

bool pdfParse(const std::string& bytes, PdfDocument& document,
	std::string& error);
bool pdfReadFile(const std::string& path, PdfDocument& document,
	std::string& error);
bool pdfListFiles(std::vector<PdfFileInfo>& files, std::string& error);
std::string pdfDocumentHtml(const PdfDocument& document,
	const std::string& title, size_t pageIndex, int zoomPercent = 100);
int pdfSelfTest();
