# Comparaison des moteurs JS sur New 3DS

Cette sonde compare QuickJS et MuJS avant leur intégration dans Pixelnaute. Elle mesure le tas suivi par chaque moteur, le temps d'initialisation et le temps d'un script de 100 000 itérations avec concaténations de chaînes.

## Construire

Depuis la racine du dépôt :

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\jsbench\build.ps1 -Engine all
```

Les deux `.3dsx` se trouvent dans `tests/jsbench/build/`. Les tailles observées avec la toolchain actuelle sont 985 940 octets pour QuickJS et 444 236 octets pour MuJS. Elles incluent la sonde, libctru et les données d'application; ce ne sont pas les tailles seules des moteurs.

## Mesurer sur la console

1. Copie `quickjs.3dsx` et `mujs.3dsx` dans deux dossiers d'applications du Homebrew Launcher sur la SD.
2. Lance chaque sonde dans les mêmes conditions, une fois chacune.
3. Lis à l'écran les valeurs `init_ms`, `script_ms`, `script_ok`, `engine_heap_after_init_bytes`, `engine_heap_after_script_bytes` et `engine_heap_after_shutdown_bytes`. MuJS affiche aussi son pic de tas mesuré.
4. Chaque lancement écrit aussi ces valeurs dans `sdmc:/jsbench.txt`. Copie ou renomme le fichier après le premier moteur, car le deuxième lancement le remplace.
5. Envoie les valeurs des deux moteurs. Ne choisis pas l'un d'eux seulement selon la taille du `.3dsx`.

La sonde réserve 1 Mio de pile native, comme le navigateur : MuJS utilise un cadre de pile d'environ 33 Kio pour compiler une expression régulière dès son initialisation. La première version du benchmark MuJS plantait avec la pile 3DSX par défaut; le dump confirmait le crash dans `js_regcompx`. La sonde limite QuickJS à 4 Mio de tas et à une pile JavaScript de 96 Kio. MuJS reçoit une limite d'allocations cumulatives de 4 Mio et une limite d'instructions élevée pour ce test. Son API compte les allocations de façon cumulative; cette limite n'est donc pas équivalente à la limite de tas de QuickJS. QuickJS utilise un allocateur de benchmark qui suit la taille des blocs, car `malloc_usable_size` n'existe pas sur newlib; l'ancienne valeur de 360 octets était incomplète. MuJS suit les tailles demandées, avec 8 octets de métadonnées par allocation. La mesure système `osGetMemRegionFree()` renvoyait 0 sur la console et n'est donc pas utilisée.

Le build New 3DS compile QuickJS sans l'extension `Atomics` (non nécessaire à ce navigateur mono-thread) et avec le support `newlib` utilisé par la cible. Sources/licences : QuickJS MIT, MuJS ISC.
