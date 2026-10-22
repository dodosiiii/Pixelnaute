#include "https.hpp"

bool httpsGet(const std::string& url, const HttpOptions& opt, NetResponse& out,
	const std::atomic<int>* cancelFlag, std::atomic<float>* progressOut)
{
	std::string u = url;
	if (strStartsWith(u, "http://"))
		u = "https://" + u.substr(7);

	if (!strStartsWith(u, "https://"))
	{
		out.error = "URL HTTPS invalide";
		return false;
	}
	return httpGet(u, opt, out, cancelFlag, progressOut);
}
