#pragma once

#include <cstring>
#include <string>

struct UiTranslation
{
	const char* french;
	const char* english;
	const char* italian;
	const char* spanish;
};

inline const char* uiLanguageName(const std::string& language)
{
	if (language == "fr") return "Francais";
	if (language == "it") return "Italiano";
	if (language == "es") return "Espanol";
	return "English";
}

inline std::string uiTranslate(const std::string& language, const std::string& source)
{
	static const UiTranslation translations[] = {
		{ "Nouvel onglet", "New tab", "Nuova scheda", "Nueva pestana" },
		{ "Fermer l'onglet", "Close tab", "Chiudi scheda", "Cerrar pestana" },
		{ "Ajouter aux favoris", "Add bookmark", "Aggiungi preferito", "Anadir favorito" },
		{ "Favoris", "Bookmarks", "Preferiti", "Favoritos" },
		{ "Historique", "History", "Cronologia", "Historial" },
		{ "Reglages", "Settings", "Impostazioni", "Ajustes" },
		{ "A propos", "About", "Informazioni", "Acerca de" },
		{ "Recharger", "Reload", "Ricarica", "Recargar" },
		{ "Moteur de recherche", "Search engine", "Motore di ricerca", "Motor de busqueda" },
		{ "Page d'accueil", "Home page", "Pagina iniziale", "Pagina de inicio" },
		{ "Verifier les certificats", "Verify certificates", "Verifica certificati", "Verificar certificados" },
		{ "Taille du texte", "Text size", "Dimensione testo", "Tamano del texto" },
		{ "Charger les images", "Load images", "Carica immagini", "Cargar imagenes" },
		{ "JavaScript", "JavaScript", "JavaScript", "JavaScript" },
		{ "Effacer l'historique", "Clear history", "Cancella cronologia", "Borrar historial" },
		{ "Mises a jour", "Updates", "Aggiornamenti", "Actualizaciones" },
		{ "Cache des pages", "Page cache", "Cache pagine", "Cache de paginas" },
		{ "Couleur de l'ecran", "Bottom screen color", "Colore schermo inferiore", "Color de pantalla inferior" },
		{ "Langue", "Language", "Lingua", "Idioma" },
		{ "haut/bas pour defiler", "up/down to scroll", "su/giu per scorrere", "arriba/abajo para desplazar" },
		{ "B : retour | touchez une ligne", "B: back | tap a row", "B: indietro | tocca una riga", "B: volver | toca una fila" },
		{ "Historique (B : retour)", "History (B: back)", "Cronologia (B: indietro)", "Historial (B: volver)" },
		{ "Favoris (B : retour, X : effacer)", "Bookmarks (B: back, X: delete)", "Preferiti (B: indietro, X: elimina)", "Favoritos (B: volver, X: borrar)" },
		{ "(vide)", "(empty)", "(vuoto)", "(vacio)" },
		{ "(aucun favori)", "(no bookmarks)", "(nessun preferito)", "(sin favoritos)" },
		{ "Rechercher ou saisir une adresse", "Search or enter address", "Cerca o inserisci indirizzo", "Buscar o escribir direccion" },
		{ "Accueil : tapez l'adresse, ou X pour un onglet", "Home: enter an address, or X for a new tab", "Home: inserisci un indirizzo o premi X per una scheda", "Inicio: escribe una direccion o pulsa X para una pestana" },
		{ "Page tronquee (limite 4 000 px)", "Page clipped (4,000 px limit)", "Pagina troncata (limite 4.000 px)", "Pagina recortada (limite 4.000 px)" },
		{ "New 3DS uniquement", "New 3DS only", "Solo New 3DS", "Solo New 3DS" },
		{ "Mise en page...", "Laying out page...", "Impaginazione...", "Preparando pagina..." },
		{ "Chargement des styles...", "Loading styles...", "Caricamento stili...", "Cargando estilos..." },
		{ "Chargement du CSS...", "Loading CSS...", "Caricamento CSS...", "Cargando CSS..." },
		{ "JavaScript...", "JavaScript...", "JavaScript...", "JavaScript..." },
		{ "Chargement du site...", "Loading website...", "Caricamento sito...", "Cargando sitio..." },
		{ "Page lourde : la console", "Large page: console is", "Pagina pesante: la console", "Pagina pesada: la consola" },
		{ "La console prepare la page", "is preparing the page", "sta preparando la pagina", "esta preparando la pagina" },
		{ "Veuillez patienter.", "Please wait.", "Attendere prego.", "Espera, por favor." },
		{ "Moteur : ", "Search: ", "Ricerca: ", "Busqueda: " },
		{ "Telechargement ", "Downloading ", "Download ", "Descargando " },
		{ "Recherche...", "Checking...", "Ricerca...", "Buscando..." },
		{ "Oui", "On", "Si", "Si" },
		{ "Non", "Off", "No", "No" },
		{ "Non (risque)", "Off (risky)", "No (rischio)", "No (riesgo)" },
		{ "Ocean", "Ocean", "Oceano", "Oceano" },
		{ "Foret", "Forest", "Foresta", "Bosque" },
		{ "Violet", "Violet", "Viola", "Violeta" },
		{ "English", "English", "Inglese", "Ingles" },
		{ "Francais", "French", "Francese", "Frances" },
		{ "Italiano", "Italian", "Italiano", "Italiano" },
		{ "Espanol", "Spanish", "Spagnolo", "Espanol" },
		{ "Langue : ", "Language: ", "Lingua: ", "Idioma: " },
		{ "Taille du texte : ", "Text size: ", "Dimensione testo: ", "Tamano del texto: " },
		{ "Verification des certificats activee", "Certificate checks enabled", "Verifica certificati attiva", "Verificacion de certificados activada" },
		{ "Attention : certificats non verifies pour tous les sites", "Warning: certificates are not verified", "Attenzione: certificati non verificati", "Aviso: certificados sin verificar" },
		{ "Images activees (en developpement)", "Images enabled (experimental)", "Immagini attive (sperimentale)", "Imagenes activadas (experimental)" },
		{ "Images desactivees", "Images disabled", "Immagini disattivate", "Imagenes desactivadas" },
		{ "Runtime JS active (execution web a venir)", "JavaScript runtime enabled", "Runtime JavaScript attivo", "Motor JavaScript activado" },
		{ "JavaScript desactive", "JavaScript disabled", "JavaScript disattivato", "JavaScript desactivado" },
		{ "Maximum d'onglets atteint", "Tab limit reached", "Limite schede raggiunto", "Limite de pestanas alcanzado" },
		{ "Dernier onglet", "Last tab", "Ultima scheda", "Ultima pestana" },
		{ "Rien a mettre en favori", "Nothing to bookmark", "Niente da aggiungere", "Nada que anadir" },
		{ "Ajoute aux favoris", "Added to bookmarks", "Aggiunto ai preferiti", "Anadido a favoritos" },
		{ "Favori supprime", "Bookmark deleted", "Preferito eliminato", "Favorito eliminado" },
		{ "Touchez encore pour supprimer", "Tap again to delete", "Tocca di nuovo per eliminare", "Toca otra vez para borrar" },
		{ "Impossible d'enregistrer les reglages", "Could not save settings", "Impossibile salvare impostazioni", "No se pudo guardar ajustes" },
		{ "Memoire insuffisante pour JavaScript", "Not enough memory for JavaScript", "Memoria insufficiente per JavaScript", "Memoria insuficiente para JavaScript" },
		{ "Mise a jour deja en cours", "Update already in progress", "Aggiornamento gia in corso", "Actualizacion en curso" },
		{ "Attendez la fin du chargement", "Wait for loading to finish", "Attendi la fine del caricamento", "Espera a que termine la carga" },
		{ "Recherche de mise a jour...", "Checking for updates...", "Ricerca aggiornamenti...", "Buscando actualizaciones..." },
		{ "Reglages (haut/bas pour defiler)", "Settings (up/down to scroll)", "Impostazioni (su/giu per scorrere)", "Ajustes (arriba/abajo para desplazar)" },
		{ "Cache des pages", "Page cache", "Cache pagine", "Cache de paginas" },
		{ "Site inconnu", "Unknown site", "Sito sconosciuto", "Sitio desconocido" },
		{ "Erreur de lecture", "Read error", "Errore di lettura", "Error de lectura" },
		{ "Carte SD inaccessible", "SD card unavailable", "Scheda SD non disponibile", "Tarjeta SD no disponible" },
		{ "Aucun site en cache", "No cached sites", "Nessun sito in cache", "No hay sitios en cache" },
		{ "Vider tout le cache", "Clear all cache", "Svuota tutta la cache", "Borrar toda la cache" },
		{ "B : retour | X : vider tout", "B: back | X: clear all", "B: indietro | X: svuota tutto", "B: volver | X: borrar todo" },
		{ "Cache vide", "Cache cleared", "Cache svuotata", "Cache vaciada" },
		{ "Site supprime du cache", "Site removed from cache", "Sito rimosso dalla cache", "Sitio eliminado de la cache" },
		{ "Erreur pendant la suppression", "Error while deleting", "Errore durante l'eliminazione", "Error al borrar" },
		{ "Touchez encore pour vider", "Tap again to clear", "Tocca ancora per svuotare", "Toca otra vez para borrar" },
		{ "B : retour | touchez une ligne", "B: back | tap a row", "B: indietro | tocca una riga", "B: volver | toca una fila" },
		{ "Historique (B : retour)", "History (B: back)", "Cronologia (B: indietro)", "Historial (B: volver)" },
		{ "Favoris (B : retour, X : effacer)", "Bookmarks (B: back, X: delete)", "Preferiti (B: indietro, X: elimina)", "Favoritos (B: volver, X: borrar)" },
		{ "Accueil : tapez l'adresse, ou X pour un onglet", "Home: enter an address, or X for a new tab", "Home: inserisci un indirizzo o premi X per una scheda", "Inicio: escribe una direccion o pulsa X para una pestana" },
		{ "(par defaut)", "(default)", "(predefinito)", "(predeterminado)" },
		{ "(aucun favori)", "(no bookmarks)", "(nessun preferito)", "(sin favoritos)" },
		{ "espace", "space", "spazio", "espacio" },
		{ "seg.", "seg.", "seg.", "seg." },
		{ "Page d'accueil", "Home page", "Pagina iniziale", "Pagina de inicio" },
		{ "Erreur", "Error", "Errore", "Error" },
		{ "Erreur de lecture", "Read error", "Errore di lettura", "Error de lectura" },
		{ "Touchez encore pour supprimer", "Tap again to delete", "Tocca di nuovo per eliminare", "Toca otra vez para borrar" },
		{ "Rien a mettre en favori", "Nothing to bookmark", "Niente da aggiungere", "Nada que anadir" },
		{ "Ajoute aux favoris", "Added to bookmarks", "Aggiunto ai preferiti", "Anadido a favoritos" },
		{ "Favori supprime", "Bookmark deleted", "Preferito eliminato", "Favorito borrado" },
		{ "Impossible d'enregistrer les reglages", "Could not save settings", "Impossibile salvare impostazioni", "No se pudo guardar ajustes" },
		{ "Memoire insuffisante pour JavaScript", "Not enough memory for JavaScript", "Memoria insufficiente per JavaScript", "Memoria insuficiente para JavaScript" },
		{ "Attendez la fin du chargement", "Wait for loading to finish", "Attendi la fine del caricamento", "Espera a que termine la carga" },
		{ "Mise a jour deja en cours", "Update already in progress", "Aggiornamento gia in corso", "Actualizacion en curso" },
		{ "Recherche de mise a jour...", "Checking for updates...", "Ricerca aggiornamenti...", "Buscando actualizaciones..." },
		{ "Veuillez patienter.", "Please wait.", "Attendere prego.", "Espera, por favor." },
		{ "Moteur : ", "Search: ", "Ricerca: ", "Busqueda: " },
		{ "Historique efface", "History cleared", "Cronologia cancellata", "Historial borrado" },
		{ "Dernier onglet", "Last tab", "Ultima scheda", "Ultima pestana" },
		{ "Maximum d'onglets atteint", "Tab limit reached", "Limite schede raggiunto", "Limite de pestanas alcanzado" },
		{ "Pixelnaute", "Pixelnaute", "Pixelnaute", "Pixelnaute" },
		{ "Navigateur autonome pour New Nintendo 3DS", "Standalone browser for New Nintendo 3DS", "Browser autonomo per New Nintendo 3DS", "Navegador independiente para New Nintendo 3DS" },
		{ "Aucun PC, aucun proxy : connexion directe.", "No PC, no proxy: direct connection.", "Senza PC o proxy: connessione diretta.", "Sin PC ni proxy: conexion directa." },
		{ "Moteur de recherche reglable dans Reglages.", "Choose your search engine in Settings.", "Scegli il motore in Impostazioni.", "Elige el buscador en Ajustes." },
		{ "Projet original : @dodosiiii", "Original project: @dodosiiii", "Progetto originale: @dodosiiii", "Proyecto original: @dodosiiii" },
		{ "Nouvel onglet", "New tab", "Nuova scheda", "Nueva pestana" },
		{ nullptr, nullptr, nullptr, nullptr }
	};

	static const struct PrefixTranslation
	{
		const char* french;
		const char* english;
		const char* italian;
		const char* spanish;
	} prefixes[] = {
		{ "Chargement... ", "Loading... ", "Caricamento... ", "Cargando... " },
		{ "Chargement ", "Loading ", "Caricamento ", "Cargando " },
		{ "Telechargement ", "Downloading ", "Download ", "Descargando " },
		{ "Taille du texte : ", "Text size: ", "Dimensione testo: ", "Tamano del texto: " },
		{ nullptr, nullptr, nullptr, nullptr }
	};
	for (const PrefixTranslation* item = prefixes; item->french; ++item)
	{
		const size_t length = std::strlen(item->french);
		if (source.compare(0, length, item->french) == 0)
			return std::string(language == "fr" ? item->french :
				(language == "it" ? item->italian :
					(language == "es" ? item->spanish : item->english))) +
				source.substr(length);
	}

	for (const UiTranslation* item = translations; item->french; ++item)
		if (source == item->french)
			return language == "fr" ? item->french :
				(language == "it" ? item->italian :
					(language == "es" ? item->spanish : item->english));
	return source;
}
