#pragma once
//=============================================================================
// tls.hpp - Couche TLS : capacites + aide au diagnostic
//=============================================================================
#include "../common.hpp"
#include <string>

// Nom de la couche securite utilisee (version TLS prise en charge par la console).
std::string tlsCapabilities();

// Traduit un echec réseau/TLS en message utilisateur comprehensible.
std::string tlsExplainError(const std::string& rawError, bool sslError);
