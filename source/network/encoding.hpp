#pragma once

#include <string>

// Convertit une page ISO-8859-1 ou Windows-1252 en UTF-8. Les corps binaires
// doivent passer convertText=false pour conserver exactement leurs octets.
std::string convertEncodingToUtf8(const std::string& input,
	const std::string& contentType, bool convertText = true);
