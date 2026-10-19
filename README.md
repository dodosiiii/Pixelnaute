# Pixelnaute — exclusivement pour New Nintendo 3DS / New Nintendo 2DS XL

**Pixelnaute** est un navigateur web expérimental pour New Nintendo 3DS et
New Nintendo 2DS XL. Il est conçu pour rester léger et afficher des pages web
avec un moteur HTML/CSS natif simplifié.

> **Version en développement : 0.3.0**
>
> Pixelnaute est un projet en développement. Il peut ouvrir certaines pages,
> mais ne remplace pas un navigateur moderne : les sites complexes, les
> applications web et les pages protégées par des défis JavaScript peuvent ne
> pas fonctionner.

## Sommaire

- [Fonctionnalités](#fonctionnalités)
- [Installation sur la console](#installation-sur-la-console)
- [Utilisation](#utilisation)
- [Compilation depuis les sources](#compilation-depuis-les-sources)
- [Tests hors console](#tests-hors-console)
- [Mises à jour](#mises-à-jour)
- [Architecture du projet](#architecture-du-projet)
- [Limites connues](#limites-connues)
- [Contribuer](#contribuer)
- [Licence](#licence)

## Fonctionnalités

Pixelnaute récupère les pages directement sur la console et les rend avec son
moteur HTML/CSS natif. Il ne s'appuie pas sur un navigateur PC distant ni sur
un proxy de rendu.

- **HTML, CSS et JavaScript :**
  - Analyse HTML tolérante, DOM manipulable par les scripts et styles externes.
  - Moteur CSS partiel : sélecteurs courants, cascade, feuilles `@media` mobiles,
    dimensions, couleurs, bordures simples et une prise en charge limitée de
    Flexbox (rangées, retour à la ligne et espacements `gap`).
  - Moteur JavaScript QuickJS intégré, activé par défaut, avec des bindings DOM,
    événements, temporisateurs, stockage, `fetch` et XMLHttpRequest partiels.
  - Indicateur de chargement pendant le traitement des scripts.

- **Navigation et réseau :**
  - Requêtes HTTP/HTTPS, redirections, cookies de session, historique, favoris
    et onglets.
  - Formulaires GET et POST encodés en URL (`application/x-www-form-urlencoded`).
  - Moteurs de recherche DuckDuckGo Lite, Wikipedia et Mojeek.
  - Chargement des images désactivé par défaut ; il peut être activé dans les
    réglages, mais augmente la consommation mémoire et peut ralentir ou faire
    échouer le rendu sur la console.

Le rendu et les API web restent volontairement limités par la mémoire et les
capacités de la New 3DS. Une page visible dans un navigateur PC peut donc avoir
une mise en page différente ou ne pas fonctionner ici.

## Installation sur la console

### Utiliser une version publiée

1. Téléchargez `N3DSWeb.3dsx` depuis la page des [releases GitHub].
2. Copiez le fichier dans le dossier `3ds` de la carte SD, par exemple :
   `sd:/3ds/Pixelnaute/N3DSWeb.3dsx`.
3. Insérez la carte dans la console et lancez le fichier depuis le
   **Homebrew Launcher**.

Pixelnaute vise les modèles **New Nintendo 3DS**, **New Nintendo 3DS XL** et
**New Nintendo 2DS XL**. Les modèles 3DS/2DS d’origine ne sont pas pris en
charge.

### Compiler le projet

Prérequis :

- un environnement **devkitPro/devkitARM** avec libctru, Citro2D, Citro3D,
  libcurl, mbedTLS et zlib ;
- Windows PowerShell pour utiliser le script fourni.

Depuis la racine du dépôt, lancez :

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Le script compile l’application et produit `N3DSWeb.3dsx` et `N3DSWeb.elf`
dans le dossier du projet. Il utilise les chemins devkitPro Windows indiqués
dans `build.ps1`; adaptez ces chemins si votre installation est ailleurs.

Sur un environnement devkitPro où `make` et `DEVKITARM` sont déjà configurés,
la compilation peut aussi être lancée avec :

```sh
make
```

## Utilisation

- Touchez la barre d’adresse sur l’écran inférieur pour saisir une adresse ou
  une recherche.
- Touchez un lien pour l’ouvrir. Faites glisser le doigt sur la page pour la
  faire défiler.
- Utilisez les boutons de navigation à l’écran pour revenir en arrière,
  avancer, revenir à l’accueil ou recharger.
- Appuyez sur **START** pour ouvrir le menu : nouvel onglet, favoris,
  historique, réglages et informations.
- Le **Circle Pad** fait défiler la page. Sur New 3DS, le **C-Stick** déplace
  le pointeur; appuyez sur **A** pour activer l’élément survolé.
- Dans **Réglages**, touchez le moteur pour passer au suivant, activez ou
  désactivez le chargement des images et choisissez si le navigateur vérifie
  les certificats TLS.

### Réseau et recherche

DuckDuckGo Lite est le moteur par défaut, car son HTML est reconnu par le
parseur de résultats de Pixelnaute. Wikipedia et Mojeek sont proposés comme
alternatives. Les anciens réglages de moteurs non pris en charge sont ramenés
à DuckDuckGo Lite au chargement.

Une seule requête réseau est exécutée à la fois. Si vous ouvrez une autre page
pendant un chargement, la nouvelle requête remplace l’ancienne. Les réponses
restent associées à l’onglet qui les a demandées.

Quand la vérification TLS est activée, Pixelnaute vérifie d’abord le certificat
et peut réessayer sans cette vérification si la connexion échoue. Ce repli
facilite la compatibilité avec certains certificats, mais réduit la sécurité :
les connexions ainsi réessayées ne garantissent plus l’identité du site.

Les formulaires GET et POST encodés en URL sont pris en charge. Les
téléversements multiparties, par exemple l'envoi de fichiers, ne le sont pas.
Le runtime JavaScript est expérimental : ses APIs et son comportement ne sont
pas équivalents à ceux de Chrome, Firefox ou Safari.

### Mises à jour

Dans **Réglages > Mises à jour**, Pixelnaute vérifie la dernière release stable
du dépôt `dodosiiii/Pixelnaute`. Si un tag de version plus récent et l’asset
`N3DSWeb.3dsx` sont disponibles, l’application télécharge et installe la mise à
jour. L’ancienne version est conservée dans un fichier `.bak`.

Cette installation intégrée est prévue pour la version **`.3dsx` lancée depuis
le Homebrew Launcher**; elle ne met pas à jour une installation `.cia`. Après
une mise à jour, fermez puis relancez l’application manuellement.

Pour publier la v0.3, poussez un tag de version tel que `v0.3.0`. Le
workflow GitHub Actions compile l’application et joint `N3DSWeb.3dsx` à la
release. La vérification intégrée nécessite qu’une release stable soit publiée
et contienne cet asset.

## Tests hors console

Un test PC couvre les résolveurs de recherche, le parseur CSS et certaines
fonctions du moteur de rendu. Il nécessite Visual Studio avec `cl.exe` et se
lance depuis PowerShell :

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\pc\run.ps1
```

Le script exécute les tests puis peut produire une image de contrôle. Cette
conversion d’image est facultative et nécessite Python ainsi que Pillow.

Les tests PC ne remplacent pas un essai sur console : ils ne valident ni les
connexions réelles en Wi-Fi, ni les services système, ni le comportement du
rendu sur l’écran de la 3DS.

## Architecture du projet

| Chemin | Rôle |
| --- | --- |
| `source/browser/` | Navigation, onglets et traitement des pages |
| `source/network/` | Requêtes HTTP/HTTPS et transport réseau |
| `source/html/` | Analyse HTML et arbre DOM |
| `source/css/` | Analyse des règles et résolution des styles |
| `source/render/` | Mise en page, texte et dessin logiciel |
| `source/search/` | Moteurs de recherche et réécriture des résultats |
| `source/settings/` | Réglages persistants |
| `source/storage/` | Historique, favoris et stockage |
| `source/ui/` et `source/input/` | Interface et contrôles |
| `source/updater/` | Vérification et installation des releases |
| `tests/` | Sondes console et tests hors console |
| `.github/workflows/` | Compilation et publication des releases GitHub |

Les pages sont récupérées directement par la console. **Aucun proxy de rendu
n’est requis ou utilisé par la version actuelle.**

## Limites connues

- QuickJS est intégré, mais ce n'est pas un moteur de navigateur complet. Les APIs DOM et Web disponibles sont partielles ; de nombreuses applications monopages (SPA), bibliothèques modernes et fonctions JavaScript peuvent échouer ou dépasser les budgets d'exécution et de mémoire.
- Le moteur CSS ne prend pas en charge tout le standard : notamment CSS Grid et les mises en page complexes, ainsi que de nombreuses propriétés, unités et sélecteurs.
- Les images sont désactivées par défaut pour limiter les risques de manque de mémoire. Lorsqu'elles sont activées, le navigateur limite le nombre d'images chargées et la taille de son cache.
- Les défis anti-bot interactifs (Cloudflare Turnstile, reCAPTCHA complexe) et les connexions exigeant un navigateur officiellement pris en charge peuvent échouer. Modifier le User-Agent ne fournit pas les fonctions manquantes et ne garantit pas l'acceptation d'un site.
- Le parseur DOM conserve jusqu'à 32 768 nœuds et le budget de règles CSS autorise jusqu'à 512 règles et 128 Ko de feuilles de style.
- Les formulaires multiparties (notamment le téléversement de fichiers) ne sont pas pris en charge.
- Les ressources réseau sont traitées séquentiellement ; le chargement de pages complexes peut donc prendre du temps.

## Contribuer

Les contributions et rapports de bogues sont les bienvenus. Pour aider au
diagnostic, indiquez :

1. le modèle de console et le mode de lancement (`.3dsx` ou autre) ;
2. l’adresse du site et les étapes pour reproduire le problème ;
3. si possible, les fichiers de diagnostic `boot.txt` et le dossier `diag`
   de `sdmc:/N3DSWeb/` (vérifiez qu’ils ne contiennent pas d’informations
   personnelles avant de les partager).

## Licence et attribution

Le code et les éléments originaux de Pixelnaute sont distribués sous la
[licence avec attribution Pixelnaute](./LICENSE). Toute redistribution, y
compris d'une version modifiée ou binaire, doit conserver le crédit à
**@dodosiiii** et ne doit pas présenter le projet original comme ayant été
créé seul par la personne qui le redistribue.

La licence du projet ne remplace pas celles des composants tiers. Leurs avis
de copyright et licences respectifs restent applicables.

[releases GitHub]: https://github.com/dodosiiii/Pixelnaute/releases
