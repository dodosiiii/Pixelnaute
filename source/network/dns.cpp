#include "dns.hpp"
#include <3ds.h>
#include <malloc.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static u32* s_socShared = nullptr;

bool dnsServiceInit()
{
	if (s_socShared) return true;
	s_socShared = (u32*)memalign(0x1000, 0x100000);
	if (!s_socShared) return false;
	if (R_FAILED(socInit(s_socShared, 0x100000)))
	{
		free(s_socShared);
		s_socShared = nullptr;
		return false;
	}
	return true;
}

void dnsServiceExit()
{
	if (s_socShared)
	{
		socExit();
		free(s_socShared);
		s_socShared = nullptr;
	}
}

bool dnsResolve(const std::string& host, std::vector<std::string>& ips)
{
	ips.clear();
	if (!s_socShared) return false;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* res = nullptr;
	int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
	if (rc != 0 || !res) return false;

	for (struct addrinfo* p = res; p && ips.size() < 4; p = p->ai_next)
	{
		char buf[64];
		if (p->ai_family == AF_INET)
		{
			struct sockaddr_in* sin = (struct sockaddr_in*)p->ai_addr;
			inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
			ips.push_back(buf);
		}
		// IPv6 : la 3DS ne dispose pas de reseau IPv6 fiable via le reseau systeme.
	}
	freeaddrinfo(res);
	return !ips.empty();
}

bool dnsResolveFirst(const std::string& host, std::string& firstIP)
{
	std::vector<std::string> ips;
	if (!dnsResolve(host, ips)) return false;
	firstIP = ips[0];
	return true;
}

std::string wifiStatusText()
{
	u32 wifi = 0;
	Result r = ACU_GetWifiStatus(&wifi);
	if (R_FAILED(r)) return "Wi-Fi : indisponible";

	// Le service ac n'expose pas l'etat de connexion DHCP dans libctru;
	// un appelle httpc nous dira si on peut vraiment joindre Internet.
	if (wifi == 0) return "Wi-Fi : pas de point d'acces";
	return "Wi-Fi : actif";
}