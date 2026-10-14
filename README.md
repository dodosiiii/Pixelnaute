# Pixelnaute — exclusivement pour New Nintendo 3DS / New Nintendo 2DS XL

**Pixelnaute** est un navigateur web expérimental pour New Nintendo 3DS et
New Nintendo 2DS XL. Il est conçu pour rester léger et afficher des pages web
avec un moteur HTML/CSS natif simplifié.

> **Version actuelle : 0.2.0**
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

- Navigation par adresse et recherche avec **DuckDuckGo Lite**.
- Moteurs alternatifs : **Wikipedia** et **Mojeek**.
- Onglets, historique, favoris, retour, avance et rechargement.
- Rendu natif simplifié de HTML et CSS, sans moteur JavaScript.
- Chargement facultatif d’images.
- Gestion de base des formulaires GET.
- Client réseau HTTP/HTTPS avec suivi des redirections.
- Vérification des certificats TLS, avec repli de compatibilité configurable.
- Contrôle manuel des mises à jour depuis les réglages.

Les réglages et données du navigateur sont stockés sur la carte SD, dans
`sdmc:/N3DSWeb/`.

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

Les formulaires GET sont pris en charge. Les formulaires POST sont signalés
comme non pris en charge et ne sont pas envoyés.

### Mises à jour

Dans **Réglages > Mises à jour**, Pixelnaute vérifie la dernière release stable
du dépôt `dodosiiii/Pixelnaute`. Si un tag de version plus récent et l’asset
`N3DSWeb.3dsx` sont disponibles, l’application télécharge et installe la mise à
jour. L’ancienne version est conservée dans un fichier `.bak`.

Cette installation intégrée est prévue pour la version **`.3dsx` lancée depuis
le Homebrew Launcher**; elle ne met pas à jour une installation `.cia`. Après
une mise à jour, fermez puis relancez l’application manuellement.

Pour publier une release, poussez un tag de version tel que `v0.2.0`. Le
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

- Pas de moteur JavaScript : les sites qui dépendent de JavaScript, notamment
  les applications web modernes, peuvent être incomplets ou inutilisables.
- Pas de flexbox ni de grille CSS; la prise en charge HTML/CSS reste limitée.
- Les défis anti-bot interactifs, CAPTCHA et vérifications nécessitant
  JavaScript ne peuvent pas être résolus par le navigateur natif. La détection
  affiche un avertissement pour certains défis explicites, mais cela ne les
  contourne pas.
- Les cookies sont conservés en mémoire pendant une requête et ses
  redirections, mais pas entre les pages.
- Une connexion directe à Internet depuis la console peut être refusée par
  certains sites ou réseaux.
- Les formulaires POST ne sont pas pris en charge.

## Contribuer

Les contributions et rapports de bogues sont les bienvenus. Pour aider au
diagnostic, indiquez :

1. le modèle de console et le mode de lancement (`.3dsx` ou autre) ;
2. l’adresse du site et les étapes pour reproduire le problème ;
3. si possible, les fichiers de diagnostic `boot.txt` et le dossier `diag`
   de `sdmc:/N3DSWeb/` (vérifiez qu’ils ne contiennent pas d’informations
   personnelles avant de les partager).

## Licence

Pixelnaute est distribué sous licence [MIT](./LICENSE).

[releases GitHub]: https://github.com/dodosiiii/Pixelnaute/releases
