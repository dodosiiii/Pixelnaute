#pragma once
//=============================================================================
// dns.hpp - Resolution DNS du nom d'hote (pour les informations systeme)
//=============================================================================
#include "../common.hpp"
#include <string>
#include <vector>

// Initialise la couche sockets (libctru soc). A appeler une seule fois.
bool dnsServiceInit();
void dnsServiceExit();

// Resout un hote en adresses IP. Retourne false en cas d'echec.
bool dnsResolve(const std::string& host, std::vector<std::string>& ips);

// Reserve l'IP resolue sous forme lisible.
bool dnsResolveFirst(const std::string& host, std::string& firstIP);

// Statut du Wi-Fi (etat systeme).
std::string wifiStatusText();