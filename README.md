# Pixelnaute — exclusivement pour New Nintendo 3DS / New Nintendo 2DS XL

**Pixelnaute** est un navigateur web expérimental pour New Nintendo 3DS et
New Nintendo 2DS XL. Il est conçu pour rester léger et afficher des pages web
avec un moteur HTML/CSS natif simplifié.

> **Version en développement : 0.5.0**
>
> Pixelnaute est un projet en développement. Il peut ouvrir la plus pars des page pages,
> mais ne remplace pas un navigateur moderne : les sites complexes, les
> applications web et les pages protégées par des défis JavaScript ne
> marche pas.

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
  - Moteur CSS partiel : sélecteurs courants, cascade, conditions `@media`
    mobiles limitées, variables personnalisées `--nom`/`var()` avec repli,
    pseudo-classes `:first-child`, `:last-child`, `:nth-child()` (entiers,
    `odd`/`even` et formules `an+b`),
    dimensions et `calc()` simple, couleurs hex/RGB/HSL avec alpha, gradients
    linéaires et radiaux centrés, bordures, coins arrondis, ombres simples et
    prise en charge limitée de Flexbox (rangées/colonnes, retour à la ligne,
    `gap` horizontal et vertical, `justify-content`)
    et CSS Grid (colonnes fixes ou fractionnaires, `repeat()` avec nombre fixe
    ou `auto-fit`/`auto-fill`, `minmax()`, placement séquentiel et `grid-column`).
    Les blocs préformatés (`<pre>`) conservent leurs retours à la ligne et
    respectent notamment leurs couleurs, dimensions et espacements CSS.
  - Moteur JavaScript QuickJS intégré, activé par défaut, avec des bindings DOM,
    événements, temporisateurs, stockage, `fetch` et XMLHttpRequest partiels.
    Son contexte mémoire n’est créé que pour les pages avec des scripts ; les
    changements du DOM sont regroupés pour éviter de refaire toute la mise en
    page après chaque script.
  - Indicateur de chargement pendant le traitement des scripts.

- **Navigation et réseau :**
  - Requêtes HTTP/HTTPS, redirections, cookies de session, historique, favoris
    et onglets.
  - Formulaires GET et POST encodés en URL (`application/x-www-form-urlencoded`).
  - Moteurs de recherche DuckDuckGo Lite, Wikipedia et Mojeek.
  - Chargement des images désactivé par défaut ; il peut être activé dans les
    réglages, mais augmente la consommation mémoire et peut ralentir ou faire
    échouer le rendu sur la console.
  - Taille du texte réglable (85 %, 100 % ou 115 %) pour mieux s’adapter à
    l’écran de la console ; le changement s’applique aux pages ouvertes ensuite.
  - Interface disponible en anglais (langue par défaut), français, italien et
    espagnol ; thèmes de couleur appliqués uniquement à l’écran inférieur.
  - Gestion du cache des pages depuis les réglages : espace occupé, taille par
    site, suppression d’un site ou vidage complet. Les anciennes entrées sans
    métadonnées sont regroupées sous « site inconnu ».

Le moteur n'est pas un navigateur CSS complet : Grid, positionnement complexe,
animations, transformations, images de fond, sélecteurs de frères et de
nombreuses pseudo-classes restent absents ou incomplets. Le rendu et les API web restent volontairement
limités par la mémoire et les capacités de la New 3DS. Une page visible dans un
navigateur PC peut donc avoir une mise en page différente ou ne pas fonctionner
ici.

## Objectifs de développement v0.6

La v0.6 sera une mise à jour plus large, préparée après la publication de la
v0.5. Les priorités proposées sont :

1. **Rendre les régressions visibles et reproductibles** : conserver des pages
   de référence compactes, ajouter des tests de rendu pixel par pixel pour les
   cas importants et améliorer les diagnostics de chargement et de mise en page.
2. **Fiabiliser le rendu statique** : consolider d'abord les couleurs,
   l'héritage et les variables CSS, puis les dimensions, les blocs, les
   bordures, Flexbox et Grid sur des exemples réels.
3. **Combler les manques visuels les plus gênants** : évaluer les pseudo-
   éléments, les icônes SVG et les images courantes, en privilégiant les pages
   qui échouent réellement plutôt qu'une implémentation complète du standard.
4. **Améliorer le chargement des pages** : rendre plus prévisibles les mises à
   jour après CSS/JavaScript, les pages longues et le chargement optionnel des
   images, tout en surveillant mémoire et réactivité sur console.
5. **Étendre la compatibilité JavaScript de façon ciblée** : ajouter seulement
   les API demandées par des sites de référence et couvertes par des tests.

Ces éléments sont des pistes, pas une promesse de compatibilité navigateur.
Leur ordre pourra changer selon les résultats des essais sur console et les
défauts rencontrés après la v0.5.

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
- Pendant la saisie, le Circle Pad déplace le contour bleu entre les touches,
  **A** insère la touche sélectionnée et **X** valide l’adresse. Le C-Stick
  déplace le curseur dans l’adresse pour corriger une partie sans tout effacer.
- Dans le menu ouvert avec les trois traits, le Circle Pad déplace également
  le contour bleu et **A** ouvre l’option sélectionnée.
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

Quand la vérification TLS est activée, Pixelnaute ne désactive pas
automatiquement cette vérification en cas d’échec. Vous pouvez désactiver
manuellement **Réglages > Vérifier les certificats**, mais ce choix s’applique
à tous les sites et rend les connexions HTTPS vulnérables à l’interception.
Laissez cette option activée sauf nécessité particulière.

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

Pour publier une version, poussez un tag correspondant, par exemple `v0.5.0`. Le
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

Avant la publication de la v0.5, vérifier sur New 3DS que le texte ne disparaît
pas après le chargement CSS, y compris sur les pages longues et les blocs avec
fond. Vérifier aussi navigation et formulaires, changement d'onglet pendant un
`fetch`, navigation vers une autre page pendant une requête, cache SD et échec
de certificat TLS. Dans ce dernier cas, le navigateur doit afficher une erreur
sans réessayer silencieusement avec la vérification désactivée.

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
- Le parseur DOM conserve jusqu'à 8 192 nœuds et le budget CSS autorise jusqu'à
  1 536 règles et 128 Ko de feuilles de style.
- Le rendu des pages longues est progressif : un premier segment de 2 000 px
  est préparé, puis d'autres segments de 2 000 px sont ajoutés au défilement.
  Un recouvrement de 240 px entre segments garantit que l'écran reste rempli
  pendant le raccord. Le contenu logique est plafonné à 100 000 px ; si un
  segment ne peut pas être alloué, l'application le signale et conserve la page
  déjà visible.
- Le cache des segments sur carte SD est limité à 1 Gio ; les fichiers âgés de
  plus d'une semaine sont supprimés. C'est un cache de rendu, pas un budget de
  mémoire vive supplémentaire ; une erreur de cache ne doit pas bloquer la page.
- Chaque runtime JavaScript est limité à 4 Mio de tas. Les scripts de
  chargement ont un budget de 100 ms chacun et de 300 ms cumulées par page ;
  les callbacks ultérieurs sont interrompus par tranches de 100 ms. Ces limites
  protègent la mémoire et la réactivité, mais peuvent empêcher certains scripts
  complexes de terminer.
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
