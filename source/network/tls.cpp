#include "tls.hpp"

std::string tlsCapabilities()
{
	// Le service httpc de la console utilise sa pile TLS integree.
	// Il negocie TLS 1.0..1.2. iOS n'utilise pas TLS 1.3 cote console.
	return "TLSv1.0 - TLSv1.2 (httpc systeme)";
}

std::string tlsExplainError(const std::string& raw, bool sslError)
{
	if (sslError)
		return "Connexion securisee impossible.\n\n"
			"Ce site utilise une technologie TLS ou un\n"
			"certificat non reconnu par la New 3DS.\n\n"
			"Le contenu principal pourrait ne pas etre\n"
			"accessible. Vous pouvez revenir en arriere\n"
			"ou choisir \"Continuer quand meme\".";
	return raw;
}