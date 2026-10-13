#pragma once
//=============================================================================
// https.hpp - Client HTTPS (via httpc + TLS systeme)
//=============================================================================
#include "http.hpp"
#include <string>

// Effectue une requete GET HTTPS (redirections suivies). Ajoute un
// message explicite si le site utilise un TLS/certificat non pris en charge.
bool httpsGet(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const volatile int* cancelFlag, std::atomic<float>* progressOut);