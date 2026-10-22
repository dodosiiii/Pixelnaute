<div align="center">

<img src="assets/icon.png" alt="Icône Pixelnaute : une loupe dans une fenêtre de navigateur" width="96">

# Pixelnaute

### Un navigateur web expérimental pour New Nintendo 3DS

Un moteur HTML/CSS natif, directement sur la console — sans navigateur distant ni proxy de rendu.

<p>
  <a href="https://github.com/dodosiiii/Pixelnaute/releases"><img src="https://img.shields.io/github/v/release/dodosiiii/Pixelnaute?display_name=tag&label=derni%C3%A8re%20version" alt="Dernière version"></a>
  <a href="https://github.com/dodosiiii/Pixelnaute/actions/workflows/pc-tests.yml"><img src="https://github.com/dodosiiii/Pixelnaute/actions/workflows/pc-tests.yml/badge.svg" alt="Tests de régression PC"></a>
  <a href="https://github.com/dodosiiii/Pixelnaute/actions/workflows/release.yml"><img src="https://github.com/dodosiiii/Pixelnaute/actions/workflows/release.yml/badge.svg" alt="Compilation des releases"></a>
</p>

<img src="assets/pixelnaute-banner-preview.svg" alt="Aperçu du mot PIXELNAUTE en lettres 3D, sans fond" width="680">

**Version en développement : 0.6.0** · [Télécharger une version](https://github.com/dodosiiii/Pixelnaute/releases) · [Signaler un problème](https://github.com/dodosiiii/Pixelnaute/issues)

</div>

> [!WARNING]
> Pixelnaute est un projet expérimental en développement, destiné aux
> **New Nintendo 3DS**, **New Nintendo 3DS XL** et **New Nintendo 2DS XL**.
> Les modèles 3DS/2DS d’origine ne sont pas pris en charge. Les sites complexes,
> les applications web modernes et certaines pages protégées peuvent ne pas
> fonctionner.

## Sommaire

- [Installation](#installation)
- [Fonctionnalités](#fonctionnalités)
- [Commandes](#commandes)
- [Compiler depuis les sources](#compiler-depuis-les-sources)
- [Tests](#tests)
- [Limites et mémoire](#limites-et-mémoire)
- [Structure du projet](#structure-du-projet)
- [Contribuer](#contribuer)
- [Licence et attribution](#licence-et-attribution)

## Installation

### Depuis une release

1. Téléchargez **`N3DSWeb.3dsx`** depuis les [releases GitHub](https://github.com/dodosiiii/Pixelnaute/releases).
2. Copiez-le sur la carte SD, par exemple dans `sd:/3ds/Pixelnaute/`.
3. Lancez-le depuis le **Homebrew Launcher**.

Une version **CIA** est également proposée dans les releases pour les
New 3DS compatibles. Installez-la avec un gestionnaire de titres homebrew.
Le CIA utilise une bannière animée et prend en charge la mise à jour intégrée ;
cette fonction n’est pas disponible dans la version `.3dsx`.

## Fonctionnalités

### Navigation

- Navigation HTTP/HTTPS, redirections, cookies de session, historique,
  favoris et onglets.
- DuckDuckGo Lite par défaut, avec Wikipedia et Mojeek comme moteurs alternatifs.
- Mode lecteur, recherche dans la page actuellement chargée, mini-carte avec
  repères de liens et d’ancres, et QR code pour partager une URL (jusqu’à
  106 octets après encodage).
- Flux RSS 2.0 et Atom affichés sous forme compacte.
- Formulaires GET et POST `application/x-www-form-urlencoded`.
- Téléchargement explicite de fichiers vers la carte SD, avec reprise si le
  serveur accepte les requêtes `Range` et une limite de 32 Mio.
- Cache SD pour les pages, feuilles de style, scripts et images, avec
  revalidation HTTP et gestion depuis les réglages.

### Rendu et scripts

- Analyse HTML tolérante, styles externes et DOM modifiable par les scripts.
- Moteur CSS partiel : cascade, sélecteurs courants, pseudo-classes enfants,
  variables CSS, couleurs, dégradés, bordures, ombres, Flexbox et Grid partiel.
- Moteur JavaScript QuickJS expérimental, avec prise en charge partielle du DOM,
  des événements, des temporisateurs, du stockage, de `fetch` et de
  XMLHttpRequest.
- Chargement des images désactivé par défaut. Quand il est activé, les images
  proches de l’écran sont traitées en priorité et adaptées au budget mémoire.
- Mode sombre des pages et taille du texte réglable.

### Interface et diagnostic

- Interface disponible en anglais, français, italien et espagnol.
- Réglages de navigation, de moteur de recherche, de certificats TLS et de
  chargement des images.
- Écran de diagnostic avec informations sur la page, le DOM, les ressources,
  le réseau et la mémoire.

## Commandes

| Action | Commande |
| --- | --- |
| Ouvrir le menu | `START` |
| Parcourir les pages | Circle Pad |
| Déplacer le pointeur (New 3DS) | C-Stick |
| Activer un lien ou une option | `A` ou toucher l’écran |
| Utiliser la barre d’adresse | Toucher l’écran inférieur |
| Valider une adresse | `X` |
| Faire défiler une page | Circle Pad ou glisser sur l’écran |

Dans la barre d’adresse, pendant la saisie, le Circle Pad choisit une touche,
`A` l’insère et le C-Stick déplace le curseur. Dans le menu, déplacez le
contour de sélection avec le Circle Pad et validez avec `A`.

## Compiler depuis les sources

### Windows avec devkitPro

Installez devkitPro/devkitARM et les bibliothèques nécessaires : libctru,
Citro2D, Citro3D, libcurl, mbedTLS et zlib. Depuis la racine du dépôt :

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Le script produit `N3DSWeb.3dsx` et `N3DSWeb.elf`. Il utilise les chemins
devkitPro définis dans `build.ps1` ; adaptez-les si votre installation est
ailleurs.

Pour construire également le CIA, installez `bannertool` et `makerom`, puis
lancez :

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -BuildCIA
```

### Linux ou environnement devkitPro déjà configuré

Avec `make` et `DEVKITARM` configurés :

```sh
make
make cia
```

Le CIA utilise l’identifiant de titre personnalisé `000400000FF6A600` et le
mode mémoire étendu de 124 Mio. Il est réservé aux modèles New 3DS.

### Bannière animée

La bannière standard du CIA affiche le mot **PIXELNAUTE** en 3D, en rotation,
sans panneau de fond. Le modèle CGFX reste sous la limite de 512 Kio et le
script de build vérifie cette limite. Pour régénérer le modèle et son aperçu,
sous Windows avec Python, Git et les dépendances indiquées dans
`tools/banner/requirements.txt`, lancez :

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\banner\build-banner.ps1
```

Le build CIA utilise `assets/pixelnaute-banner.cgfx` lorsqu’il est disponible.
La bannière animée doit encore être vérifiée sur une vraie console avant toute
publication : un build réussi ne remplace pas ce test.

## Mises à jour intégrées

Dans la version CIA, **Réglages > Installer mise à jour CIA** recherche une
release stable plus récente. Après confirmation, Pixelnaute télécharge le CIA,
vérifie son empreinte SHA-256, puis demande au système de l’installer et de
redémarrer le titre. La version `.3dsx` ne dispose pas de cette fonction.

## Tests

### Tests PC sous Windows

Avec Visual Studio et `cl.exe`, lancez :

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\pc\run.ps1
```

La génération de l’image de contrôle est facultative et nécessite Python et
Pillow.

### Tests PC sous Linux

```sh
bash tests/pc/run-linux.sh
```

Les tests couvrent les résolveurs de recherche, le parseur CSS et certaines
fonctions de rendu. GitHub Actions les exécute également sur chaque push et
pull request. Ils ne remplacent pas les essais sur console : Wi-Fi, services
système, installation CIA et rendu sur l’écran 3DS doivent être vérifiés sur
le matériel.

## Limites et mémoire

Pixelnaute n’est pas un navigateur de bureau complet. Les pages sont récupérées
directement par la console ; aucun proxy distant n’est requis. Les ressources
réseau sont traitées séquentiellement, ce qui peut ralentir les pages complexes.

Les limites actuelles sont volontairement adaptées à la mémoire de la console :

| Ressource | Limite actuelle |
| --- | --- |
| Téléchargement HTML | 4 Mio maximum, ajusté si la mémoire disponible est insuffisante |
| Nœuds DOM | 16 384 |
| Règles CSS | 1 536 |
| Feuilles de style | 128 Kio |
| Texte DOM | Arène de 4 Mio |
| Contenu de page | Jusqu’à 5 segments, avec rendu progressif |
| Onglets | 2 pendant la phase de test mémoire |
| Mémoire du runtime JavaScript | 4 Mio par page |
| Cache des segments sur carte SD | 1 Gio ; entrées de plus d’une semaine supprimées |

CSS Grid et Flexbox sont partiels. Les transformations, animations, images de
fond, positionnements complexes et plusieurs sélecteurs ne sont pas pris en
charge ou peuvent différer des navigateurs modernes. Les API JavaScript sont
également partielles ; les SPA, les défis anti-bot et les sites qui exigent un
navigateur officiellement pris en charge peuvent échouer.

Les images sont désactivées par défaut. Leur décodage peut être ignoré si la
mémoire disponible est insuffisante. Le navigateur limite également la durée
d’exécution des scripts afin de préserver la réactivité. Les formulaires
multiparties, comme l’envoi de fichiers, ne sont pas pris en charge.

## Structure du projet

| Dossier | Contenu |
| --- | --- |
| `source/browser/` | Navigation, onglets et chargement des pages |
| `source/network/` | Transport HTTP/HTTPS |
| `source/html/` et `source/css/` | Analyse HTML, DOM et styles |
| `source/render/` | Mise en page et rendu |
| `source/search/` | Moteurs de recherche |
| `source/ui/` et `source/input/` | Interface et commandes |
| `source/settings/` et `source/storage/` | Réglages et données persistantes |
| `source/updater/` | Recherche et installation des mises à jour |
| `tests/` | Tests PC et outils de diagnostic |
| `tools/banner/` | Génération de la bannière 3D |

## Contribuer

Les rapports de bogues et les contributions sont les bienvenus. Pour aider à
reproduire un problème, indiquez :

1. Le modèle de console et le mode utilisé (`.3dsx` ou CIA).
2. L’adresse du site et les étapes pour reproduire le problème.
3. Si possible, les journaux `boot.txt` et le dossier `diag` de
   `sdmc:/N3DSWeb/`. Vérifiez qu’ils ne contiennent aucune information
   personnelle avant de les partager.

## Licence et attribution

Le code et les éléments originaux de Pixelnaute sont distribués sous la
[licence avec attribution Pixelnaute](./LICENSE). Toute redistribution,
y compris d’une version modifiée ou binaire, doit inclure la licence et
conserver le crédit à **@dodosiiii**. Les composants tiers restent soumis à
leurs propres avis de copyright et licences.
