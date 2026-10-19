#pragma once
#include <stddef.h>
#include <map>
#include <string>
#include <stdint.h>
#include <vector>

struct JSRuntime;
struct JSContext;
class Document;

// État durable par onglet, référencé par les callbacks de son contexte JS.
struct JSPageBinding
{
	Document* document = nullptr;
	std::string origin;
	std::string url;
	std::string pendingNavigation;
	int navigationMode = 0; // 1=assign, 2=replace, 3=reload
	int historyDelta = 0;
	uint32_t nextFetchId = 0;
	uint32_t contextGeneration = 0;
	void* fetchOwner = nullptr;
	int fetchTabIndex = -1;
	bool (*enqueueFetch)(void*, JSPageBinding*, int, uint32_t, bool, const std::string&,
		const std::string&, const std::string&) = nullptr;
	std::map<std::string, std::string> sessionStorage;
	std::vector<std::map<std::string, std::string>> computedStyles;
	uint64_t computedDomVersion = UINT64_MAX;
	size_t computedCssBytes = SIZE_MAX;
};

// Cree un runtime/contexte isole pour une page, avec un tas borne et une
// pile JavaScript adaptes a la New 3DS.
bool jsPageContextCreate(JSRuntime*& runtime, JSContext*& context);
void jsPageContextDestroy(JSRuntime*& runtime, JSContext*& context);

// Les scripts de chargement sont limites a 100 ms chacun et 300 ms par page.
// Les callbacks ulterieurs gardent une limite de 100 ms par tranche, sans
// consommer le budget cumule du chargement.
bool jsPageExecutionBegin(JSRuntime* runtime, bool chargePageBudget = true);
// Retourne true si QuickJS a interrompu l'evaluation pour depassement.
bool jsPageExecutionEnd(JSRuntime* runtime);

// Execute un script classique dans le contexte de la page. Les API DOM ne
// sont pas encore exposees a JavaScript.
bool jsPageEval(JSRuntime* runtime, JSContext* context,
	const char* source, size_t length, const char* filename);
void jsPageBindDocument(JSContext* context, Document* document, const char* pageUrl,
	JSPageBinding* binding = nullptr);
bool jsPageDispatchEvent(JSContext* context, const char* type, const char* target,
	const char* key = nullptr, int x = -1, int y = -1);
void jsPageTick(JSRuntime* runtime, JSContext* context);
void jsPageResolveFetch(JSRuntime* runtime, JSContext* context, uint32_t id,
	const char* url, const char* body, size_t bodyLength, const char* contentType,
	int status, const char* error);
void jsPageResolveXHR(JSRuntime* runtime, JSContext* context, uint32_t id,
	const char* url, const char* body, size_t bodyLength, const char* contentType,
	int status, const char* error);
