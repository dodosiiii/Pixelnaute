#include "downloads.hpp"
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>
#include <string>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

bool isHtmlContent(const std::string& contentType, const std::string& body)
{
	std::string ct = toLower(contentType);
	if (ct.find("text/html") != std::string::npos) return true;
	if (ct.find("application/xhtml") != std::string::npos) return true;
	if (ct.find("text/") != std::string::npos && ct.find("html") != std::string::npos) return true;
	if (ct.empty() || ct.find("text/plain") != std::string::npos)
	{
		// sniff : debut du corps ressemblant a du HTML
		size_t n = body.size() < 16 ? body.size() : 16;
		for (size_t i = 0; i < n; i++)
			if (body[i] == '<') return true;
	}
	return false;
}

static bool setDownloadMetadata(DownloadFileInfo& file, uint64_t size,
	time_t modifiedAt, std::string& error)
{
	file.size = size;
	file.modifiedAt = (uint64_t)modifiedAt;
	const size_t dot = file.name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
	{
		file.extension = file.name.substr(dot);
		file.name.erase(dot);
	}
	else
		file.extension = "(sans extension)";

	struct tm* local = localtime(&modifiedAt);
	if (!local)
	{
		error = "Impossible de lire la date d'un fichier telecharge.";
		return false;
	}
	char date[32];
	if (strftime(date, sizeof(date), "%d/%m/%Y %H:%M", local) == 0)
	{
		error = "Impossible de formater la date d'un fichier telecharge.";
		return false;
	}
	file.modified = date;
	return true;
}

static bool appendDownloadDirectory(const std::string& directory,
	bool pdfOnly, std::vector<DownloadFileInfo>& files, std::string& error)
{
#ifdef _WIN32
	const std::string pattern = directory + "\\*";
	struct _finddata_t item;
	const intptr_t handle = _findfirst(pattern.c_str(), &item);
	if (handle == -1)
	{
		if (errno == ENOENT) return true;
		error = "Impossible de lire le dossier de telechargement.";
		return false;
	}
	do
	{
		if (item.name[0] == '.' || (item.attrib & _A_SUBDIR)) continue;
		DownloadFileInfo file;
		file.name = item.name;
		if (pdfOnly && (file.name.size() < 4 ||
			toLower(file.name.substr(file.name.size() - 4)) != ".pdf"))
			continue;
		file.path = directory + "\\" + file.name;
		if (setDownloadMetadata(file, (uint64_t)item.size,
			(time_t)item.time_write, error))
			files.push_back(std::move(file));
		else if (!error.empty())
		{
			_findclose(handle);
			files.clear();
			return false;
		}
	} while (_findnext(handle, &item) == 0);
	_findclose(handle);
#else
	DIR* dir = opendir(directory.c_str());
	if (!dir)
	{
		error = "Impossible d'ouvrir le dossier " + directory + ".";
		return false;
	}
	int readError = 0;
	for (;;)
	{
		errno = 0;
		struct dirent* item = readdir(dir);
		if (!item)
		{
			readError = errno;
			break;
		}
		const std::string name = item->d_name;
		if (name == "." || name == "..") continue;
		if (pdfOnly && (name.size() < 4 ||
			toLower(name.substr(name.size() - 4)) != ".pdf"))
			continue;
		DownloadFileInfo file;
		file.name = name;
		file.path = directory + "/" + name;
		struct stat info;
		if (stat(file.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
			continue;
		if (setDownloadMetadata(file, (uint64_t)info.st_size,
			info.st_mtime, error))
			files.push_back(std::move(file));
		else if (!error.empty())
		{
			closedir(dir);
			files.clear();
			return false;
		}
	}
	closedir(dir);
	if (readError != 0)
	{
		error = "Erreur pendant la lecture du dossier " + directory + ".";
		files.clear();
		return false;
	}
#endif
	return true;
}

bool downloadsListFiles(std::vector<DownloadFileInfo>& files,
	std::string& error)
{
	files.clear();
	error.clear();
	if (!appendDownloadDirectory(NW_SD_DOWN, false, files, error))
		return false;
	if (!appendDownloadDirectory(NW_SD_PDFS, true, files, error))
	{
		files.clear();
		return false;
	}
	std::sort(files.begin(), files.end(),
		[](const DownloadFileInfo& a, const DownloadFileInfo& b)
		{
			if (a.modifiedAt != b.modifiedAt)
				return a.modifiedAt > b.modifiedAt;
			return a.name < b.name;
		});
	return true;
}

bool downloadsSave(const std::string& body, const std::string& filename,
	std::string& outPath)
{
	std::string path = storageUniqueDownloadName(filename);
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;
	size_t written = fwrite(body.data(), 1, body.size(), f);
	int err = fclose(f);
	if (written != body.size() || err != 0)
	{
		remove(path.c_str());
		return false;
	}
	outPath = path;
	return true;
}