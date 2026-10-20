# K-WA SUB

Un synthétiseur de basses pour le **M-VAVE FM-1** : drum & bass, dub, dubstep, techno. Né du tracker
[KAWA](../README.fr.md) (même plateforme : mises à jour, secours USB, USB-MIDI, audio, écran, flash), mais sans
séquenceur, sans pistes, sans samples ni batteries : tout le processeur va au son.

**Essayer sans rien installer :** ouvrez [`emulator/index.html`](emulator/index.html) dans un navigateur
(un seul fichier, fonctionne hors ligne) : le son démarre au premier appui (touche, bouton ou clavier). **L'installer :** voir
[Installer sur le FM-1](#installer-sur-le-fm-1).

## Le son

| Bloc | Ce qu'il fait |
| --- | --- |
| **OSC A / OSC B** | Deux oscillateurs, chacun avec son moteur, octave, demi-ton, fin, niveau et trois contrôles de timbre |
| **8 moteurs** | `ANALOG` (sinus › triangle › dent de scie › carré, largeur, drive) · `SUPER` (2, 3, 5 ou 7 dents de scie désaccordées : reese, supersaw) · `WAVE` (8 tables d'onde × 8 positions : BASIC, HARMONIC, GROWL, VOCAL, FM, PWM, DIGITAL, SYNC, avec warp) · `FM` (2 opérateurs, 16 ratios, feedback) · `PHASE` (distorsion de phase façon Casio CZ, dont 3 types résonants) · `FOLD` (wavefolder, symétrie) · `SYNC` (hard sync 1 à 8×) · `VOCAL` (formants A E I O U, basse qui parle) |
| **COMBINE** | `ADD` (A + B), `RING` (A × B), `X-MOD` (B module la phase de A), dérive analogique, reset de phase à la note |
| **SUB + NOISE** | Sub sinus / triangle / carré / pulse à -2, -1 ou 0 octave, **direct** (propre, hors filtre) ou dans le filtre ; bruit coloré |
| **Filtre** | 7 caractères : ladder `LP 24`, `LP 12`, `BAND`, `HIGH`, `NOTCH` (avec compensation des basses), `VOWEL` (formants : le cutoff parcourt A E I O U, idéal pour les « yoi »), `COMB` (accordé par le cutoff, au clavier avec KEY TRACK) ; drive, enveloppe, suivi de clavier, vélocité |
| **Enveloppes** | AMP (ADSR), FILTER (ADSR), MOD (AD vers la destination de son choix, ex. chute de pitch 808) |
| **LFO 1 « WOBBLE » / LFO 2** | 9 formes (SINE, TRI, SAW, SQUARE, S&H, WUB, STEPS, DRIFT), libre en Hz ou **calé sur le tempo** (4 mesures … 1/64, pointées et triolets), retrig à la note, phase, lissage, une destination directe + la matrice |
| **Destinations** | Les mêmes pour les deux LFO, l'enveloppe MOD et la matrice : cutoff, résonance, drive, pitch, puis **tous les réglages d'EDIT des oscillateurs actifs** (niveau, les 3 timbres sous le nom de leur moteur, ex. `A WAVE`, `A PWIDTH`, `B DETUNE`, et la hauteur propre de chaque oscillateur), X-MOD, sub, bruit, couleur du bruit, ampli, quantité d'enveloppe du filtre, la vitesse de chaque LFO (en synchro : d'une division à l'autre), la profondeur du wobble et du LFO 2, le gate du STEP, et **chaque réglage des effets** : drive (quantité, mix), crush (bits, rate, mix), chorus (rate, profondeur, mix), delay (feedback, tone, mix), reverb (taille, amorti, mix), glue, sub boost, volume. Un oscillateur ou un effet éteint n'est pas proposé, sauf la saturation (`SAT AMOUNT`, `SAT MIX`, juste après le filtre), qui s'allume quand on la choisit. Changer une destination ouvre une liste déroulante (celles d'avant et d'après, leur groupe, la position) qui se ferme seule. |
| **STEP** | Un effet pas-à-pas de 16 pas sur le tempo (vue `LFO` › STEP FX) : 16 motifs (OFFBEAT, PUMP, DUB CHOP, TRANCE, TRIPLET, BROKEN, SKANK, GALLOP, STAIRS, DNB ROLL, HALFTIME, STUTTER, ACID, SYNCOPE, RANDOM…), au 1/4 jusqu'au 1/32 et en triolets ; il coupe le son (GATE) et/ou module une destination, et c'est aussi une source de la matrice. |
| **Matrice** | 4 liaisons source › destination (LFO, enveloppes, vélocité, molette, aftertouch, note, aléatoire) |
| **FX** | SATURATION (soft, tube, hard, fold) · BITCRUSH · CHORUS (au-dessus de ~150 Hz : le sub reste mono) · DUB DELAY ping-pong calé sur le tempo, plus sombre à chaque répétition · REVERB · MASTER (glue, sub boost, sortie) |
| **Jeu** | MONO, LEGATO (glide entre touches tenues), POLY 4 voix ; glide ; pitch bend |

42 presets d'usine (+ INIT), chacun avec son genre : DNB, DUB, DUBSTEP, TECHNO, JUNGLE, UK GARAGE, TRAP, HOUSE, NEUROFUNK,
DUB TECHNO, BREAKS, MINIMAL ; et 32 emplacements utilisateur (banque B). La liste des presets affiche, en petit à
droite, le genre et les moteurs de chaque patch.

## Le wobble en live

- **Page PLAY** : `K1` cutoff, `K2` vitesse du wobble, `K3` profondeur, `K4` résonance. `SEL` passe à la seconde
  série (sub, drive, delay, sortie).
- La vitesse du wobble est **calée sur la grille** : passer de 1/4 à 1/8 puis 1/16T reste sur le temps (la phase se
  calcule depuis le compteur de temps, pas depuis le dernier réglage). En `RETRIG NOTE`, chaque note repart du début.
- **LFO GRID** (pages LFO et SYSTEM) : `STRAIGHT` ne garde que les divisions rondes (4 mesures … 1/2, 1/4, 1/8, 1/16,
  1/32, 1/64) : le bouton RATE, la macro WOB RATE, le CC76 et les modulations de vitesse sautent les T et les D ;
  un preset en 1/8T joue alors en 1/16, la division ronde la plus proche, et l'écran affiche ce qu'on entend.
- `PLAY` = **HOLD** : la note reste, les deux mains sont libres pour les potards.
- `REC` = **TAP tempo** : la frappe donne le tempo et recale le LFO. En `CLOCK MIDI`, le tempo et le temps suivent
  l'horloge MIDI (start, puis recalage à chaque noire).
- MIDI : molette (CC1, routée sur la profondeur du wobble dans WOBBLE 01), CC74 cutoff, CC71 résonance, CC76 vitesse
  du wobble, CC77 profondeur, CC123 tout couper.

## Les pages et le panneau

| Bouton | Page | Vues (même bouton ou `SEL`) |
| --- | --- | --- |
| `HOME` | 01 PLAY | patch courant + macros live · mix live |
| `EDIT` | 02 BASS | OSC A · OSC B · SUB + NOISE · COMBINE |
| `ENV` | 03 ENVELOPE | AMP · FILTER · MOD |
| `SEQ` | 04 FILTER | cutoff / résonance / drive · modulations du filtre · **SEQ BASS** |
| `LFO` | 05 LFO | LFO 1 WOBBLE · LFO 2 · STEP FX · matrice |
| `FX` | 06 FX | saturation · crush · chorus · delay · reverb · master |
| `ARP` | 07 PRESETS | banque A (usine) · banque B (utilisateur) · **ARP** |
| `GLO` | 08 SYSTEM | canal MIDI, mode de voix, glide, bend, vélocité, tempo, horloge, octave, style |

Les points en haut à droite de l'écran montrent les vues de la page (la vue affichée est allumée). **Banques et presets.** L'encodeur `PRESETS` ne marche que sur `HOME` et `EDIT` (pour ne rien changer par erreur
ailleurs) ; il parcourt la banque A puis les patches enregistrés de la banque B, en boucle. Si le son a été modifié
et pas enregistré, il demande d'abord : `SEL` charge, un autre bouton garde le son (sans réponse en 6 s : gardé).
La page `ARP` › PRESETS montre la banque A, `ARP` encore la banque B (`K1` défile, `K3` charge).

**Thèmes, palettes, police.** `SYSTEM › THEME` change toute la mise en page :
- CLASSIC : le premier dessin ;
- STUDIO : sans bandeau, barre d'état et onglets des 8 pages, réglages en cases avec la couleur de leur potard et ce
  qui les module ;
- PURE : dans l'esprit de Pure SUB, onglet vert-jaune, grand visuel, 4 anneaux pour les 4 potards ;
- RACK : rail des pages à gauche, gros caractères.

`SYSTEM › PALETTE` donne 6 palettes par thème : la sienne, une variante (AMBER, OCEAN, PURE RED, MAGMA), SYNTHWAVE,
MINT, noir et blanc, et inversé (noir sur blanc). `SYSTEM › FONT` : SANS (DejaVu Sans Bold), MONO (IBM Plex Mono),
HYPER (Atkinson Hyperlegible Mono, faite pour être lue par tous) ou PIXEL (Press Start 2P, nette à 8 px).

**Visuels.** Chaque vue montre ce qu'elle règle : forme d'onde de chaque oscillateur et du sub, enveloppes (avec un
point qui suit la note jouée), courbe du filtre (avec le cutoff modulé en direct), LFO (avec leur position),
grille STEP (avec le pas joué), courbe de saturation, crush, chorus, échos du delay, queue de la reverb, vu-mètre et
compression du master, charge des cœurs.

`SELECT` choisit la ligne, `ALGORITHM` change sa valeur, `K1`–`K4` font ce qu'indique le bas de l'écran (dans
l'émulateur, leur fonction s'affiche sous chaque potard), `PRESETS` charge le patch suivant depuis n'importe quelle
page. `SAVE` ouvre l'enregistrement (emplacement, nom lettre par lettre, style) ; `SAVE` à nouveau écrit.
**`SAVE` maintenu = UNDO** : il annule la dernière modification (les tours d'un même bouton dans la même seconde
et demie comptent pour une seule ; un chargement de preset aussi), puis une de plus toutes les 0,4 s tant qu'il
reste appuyé, jusqu'à 16 en arrière.
`OCT-` / `OCT+` : octave du clavier ; les deux ensemble : tout couper.

## SEQ BASS et ARP

**SEQ BASS** (`SEQ`, 3e vue) : 16 motifs de 1 à 16 pas, chacun une note (C1 à C5), un silence, un accent, un slide
(la note suivante glisse, sans nouvelle attaque, comme une TB-303). Les 8 premiers motifs sont des lignes prêtes à
jouer (DNB ROLL, DUB STEPPA, TECHNO OFF, HALFTIME, ACID, 2-STEP, OCTAVES, ROOT).

| Commande | Effet |
|---|---|
| `PLAY` | lance / arrête la séquence (depuis n'importe quelle page ; horloge interne : départ sur la mesure) |
| clavier pendant la lecture | transpose (C4 = tel qu'écrit), sans jouer de note |
| `REC` (sur la vue SEQ) | enregistrement pas à pas : chaque touche écrit sa note sur le pas du curseur et avance |
| `K1` / `K2` / `K3` / `K4` | pas (curseur) · note · type (REST, NOTE, ACCENT, SLIDE, ACC+SLIDE) · motif |
| `SELECT` + `ALGO` | aussi : longueur, vitesse (1/4 à 1/32), swing (50 à 75 %) |

La grille montre les 16 pas comme un petit piano-roll : hauteur = note, orange = accent, trait à droite = slide,
vert = le pas joué, trait sous le pas = le curseur (orange en REC). Les motifs et l'ARP sont enregistrés à part des
patches (2e secteur du stockage, à l'arrêt) : changer de son ne change pas la ligne.

**ARP** (`ARP`, dernière vue après les banques) : ON/OFF, MODE (UP, DOWN, UP-DOWN, RANDOM, ORDER), RATE, OCTAVES
(1 à 4), GATE (10 à 100 %, 100 % = lié). Il joue les touches tenues ; avec HOLD (`PLAY` maintenu) elles restent.
L'ARP démarre éteint à chaque allumage. SEQ BASS et ARP suivent le tempo, et l'horloge MIDI quand `CLOCK` = MIDI.

**Changé :** `PLAY` lance le SEQ ; **HOLD** est maintenant `PLAY` maintenu 0,6 s. `REC` reste TAP tempo, sauf sur
la vue SEQ BASS.

**Boutons maintenus.** Une barre de chargement apparaît en bas de l'écran avec ce qui va se passer écrit au-dessus ;
relâcher avant la fin annule (le bouton fait alors son action normale) :

| Maintenir | Effet |
|---|---|
| `SAVE` | UNDO (sur la page PRESETS : ajoute ou retire le preset sélectionné des favoris, marqué `*`) |
| `SEL` | A/B : on entend le son enregistré tant que `SEL` reste appuyé |
| `PLAY` | HOLD : les touches restent tenues (ou relâchées si HOLD était déjà mis) |
| `LFO` + tourner un potard | assignation rapide « à la Pure SUB » : le LFO 1 va sur ce que règle le potard, le tour règle la quantité |
| `ENV` + tourner un potard | pareil avec l'enveloppe de modulation |
| un bouton de page + `K1`–`K4` | couche Shift : les potards de cette page, sans la quitter |
| un bouton de page, 1,2 s | aléatoire contrôlé de la page (environ un quart de la plage ; `SAVE` maintenu annule) |

Tourner un réglage l'affiche en grand une seconde, avec l'ancienne valeur. Au démarrage, un écran montre le dernier
patch et l'état du second cœur. Sur PRESETS, `K2` fait défiler tous les presets ou seulement les favoris.

## Version B (test : 4 moteurs)

Une deuxième version, à essayer dans l'émulateur [`emulator/b.html`](emulator/b.html) (et sur le FM-1 avec
`install/version-b/`), ne garde que **4 moteurs**, un par usage :

| Moteur gardé | Rôle | Ce qu'il remplace |
|---|---|---|
| ANALOG | soustractif : sinus → triangle → scie → carré, largeur, drive | FOLD (sinus + saturation FOLD), SYNC (voir plus bas) |
| REESE | pile de scies désaccordées (2 à 7) | SUPER |
| WAVE | 8 tables × 8 images, position, warp | VOCAL (table VOCAL) |
| FM | 2 opérateurs, ratio, feedback | PHASE |

Le **hard sync** devient un mode du mélange A + B : `COMBINE › MODE = SYNC` (B redémarre avec A, `X-MOD` monte B
de 0 à +24 demi-tons, modulable par les LFO : la vraie « sync sweep »). Ce mode existe aussi dans la version
normale. Les patches faits avec PHASE, FOLD, SYNC ou VOCAL sont convertis au chargement ; les patches enregistrés
restent compatibles entre les deux versions.

**Double Shift (version B seulement).** Sur PLAY, EDIT, ENV, FILTER, LFO et FX, le premier écran montre 12 réglages
en 3 colonnes de 4 (une ligne par potard `K1`–`K4`) :

| Colonne | Couleur | Pour la jouer |
|---|---|---|
| gauche : la base (MIX, AMP, WOBBLE, SPACE…) | vert fluo | les potards seuls |
| milieu : OSC A, FILTER ENV, LFO 1, DRIVE… | jaune | `OCT−` maintenu + potard |
| droite : OSC B, MOD ENV, LFO 2, MASTER… | orange | `OCT+` maintenu + potard |

`OCT−` / `OCT+` tapés seuls changent toujours l'octave (tenus plus d'une demi-seconde sans potard : rien) ; les deux
ensemble coupent tout. Les cases ne montrent que des initiales ; le nom complet, la valeur et l'ancienne valeur
s'affichent en haut pendant qu'on tourne. `SELECT` + `ALGO` règlent aussi n'importe laquelle des 12 cases. Le
bouton de la page (ou `SEL`) passe ensuite aux vues détaillées habituelles, puis revient aux 3 colonnes. Un effet
éteint s'allume quand on monte un de ses réglages.

| Page | Gauche (base) | Milieu (`OCT−`) | Droite (`OCT+`) |
|---|---|---|---|
| PLAY | cutoff, wob rate, wob depth, résonance | sub, saturation, delay, volume | A P1, B P1, X-MOD, filter env |
| EDIT | niveaux A, B, SUB, X-MOD | OSC A : P1, P2, P3, demi-tons | OSC B : P1, P2, P3, demi-tons |
| ENV | AMP A D S R | FILTER A D S R | MOD A, D, quantité, destination |
| FILTER | cutoff, résonance, drive, env | type, key, vélocité, route du sub | sub : onde, octave, bruit, couleur |
| LFO | wob rate, depth, destination, grille | LFO 1 : forme, phase, lissage, retrig | LFO 2 : forme, rate, depth, destination |
| FX | delay temps, feedback, mix, reverb | saturation type, quantité, mix, bits | glue, sub boost, volume, crush mix |

## À faire

- **Page MIDI** (prévue, pas encore faite) : canal par fonction, start/stop du SEQ par l'horloge MIDI, envoi de
  l'horloge, CC appris, réponse aux program change.
- Le nouveau workflow `OCT−` / `OCT+` maintenus en 3 colonnes : maquette dans
  [`docs/mockups/workflow-shift.png`](docs/mockups/workflow-shift.png), en attente de validation.
- MACROS (4 macros × 4 destinations), MORPH / XY.

## Installer sur le FM-1

Le paquet est [`install/k-wa-sub-0.8.fwsc`](install/k-wa-sub-0.8.fwsc) (identité `FM-1_978` ; les anciens dans `install/older/`). Il s'installe exactement
comme KAWA, avec le même chargeur de mise à jour, et fonctionne depuis le firmware d'origine comme depuis KAWA :

- **Installateur rapide** : ouvrir [`installer.html`](installer.html) dans Chrome ou Edge (un seul fichier, hors ligne,
  le firmware est dedans), cliquer sur « Installer K-WA SUB », confirmer. Il accepte aussi un autre .fwsc (KAWA...).
- **Dans le navigateur** (Chrome ou Edge, Web MIDI) : la page d'installation de KAWA ([`../index.html`](../index.html)),
  bouton « Use a local .fwsc file… », puis choisir `k-wa-sub-0.8.fwsc`.
- **Avec Python 3** : `INSTALL-Linux.sh`, `INSTALL-Mac.command` ou `INSTALL-Windows.bat` dans ce dossier (ou
  `python3 install/fm1_install.py install/k-wa-sub-0.8.fwsc`).

Un câble USB **de données**, aucun autre logiciel MIDI ouvert, rien à débrancher avant la fin. Pour revenir à KAWA :
installer `kawa-0.5.fwsc` de la même façon. Pour le firmware officiel : M-UPGRADE, comme décrit pour KAWA. En cas de
problème : maintenir `OCT-` à l'allumage donne le **secours USB** (l'installateur marche toujours). Les patches
utilisateur sont rangés à `0x97000` : enregistrer un patch écrase la sauvegarde automatique de KAWA (son
emplacement 0).

## Les deux cœurs

Le FM-1 (JieLi AC79) a deux cœurs, et K-WA SUB les utilise :

- **cœur 1** : les voix (oscillateurs, filtre, enveloppes, LFO, matrice) du bloc suivant ;
- **cœur 0** : l'interruption audio, les effets du bloc en cours, l'écran, les boutons, le MIDI et l'USB.

Il y a un bloc de latence (5,8 ms). Le démarrage du cœur 1 reprend la séquence du SDK de JieLi (`EnableOtherCpu` et
`cpu1_start`, voir `source/firmware/hal/fm1_cpu1.h`), avec des sécurités :

- si le cœur 1 ne répond pas, tout tourne sur le cœur 0 (même son) ;
- si le cœur 1 cesse de rendre ses blocs (3 blocs de suite), le cœur 0 reprend les voix aussitôt, `CORE 1 STUCK: ONE CORE` s'affiche et `AUDIO CORES` passe à `SINGLE` (à remettre sur `DUAL` dans SYSTEM pour réessayer) ;
- si le FM-1 redémarre dans les 10 s d'un démarrage à deux cœurs, le démarrage suivant se fait sur un seul cœur ;
  `SYSTEM › AUDIO CORES` permet de réessayer ;
- pendant chaque écriture en flash (patch, réglages), le cœur 1 attend dans une boucle en RAM ;
- avant une mise à jour, le mode UBOOT ou un plantage, il est remis en reset.

`SYSTEM › AUDIO CORES` affiche `DUAL · ON` quand le cœur 1 tourne vraiment. `CPU LOAD` donne la part d'un bloc
utilisée par chaque cœur.

**Gouverneur de charge** (comme KAWA) : si un bloc dépasse 70 % deux fois de suite, le son s'allège d'un cran :
1. la reverb et le chorus s'arrêtent ;
2. il reste 2 voix et 3 dents de scie par SUPER ;
3. il reste 1 voix et 2 dents de scie.

Après 2 s sous 45 %, on remonte d'un cran. Un bloc ne dépasse jamais son temps : la fin reste silencieuse, et le
chien de garde ne voit jamais l'audio bloqué. `CPU LOAD` affiche `· -1`, `-2` ou `-3` quand le son est allégé.

**Pas encore essayé sur un vrai FM-1.** Au premier essai, regarde la page SYSTEM : `AUDIO CORES`, `CPU LOAD` en
jouant un preset lourd (WIDE REESE en POLY, accords), et dis-moi ce qui s'affiche.

## Construire

```sh
cd source
python3 tools/gen_ksub.py        # tables (hauteurs, filtre, enveloppes, tables d'onde) et polices
web/make_emulator.sh             # -> ../emulator/index.html (clang avec la cible wasm32 et wasm-ld)
tools/get_toolchain.sh           # la chaîne JieLi (une fois), + les 3 fichiers du SDK AC79 (voir ../../source/BUILDING.md)
./build.sh --release 0.2         # -> build/k-wa-sub-0.8.fwsc, avec toutes les vérifications de KAWA
tests/run_loader_tests.sh        # la mise à jour sur l'ordinateur (de KAWA vers K-WA SUB, coupure, paquet abîmé...)
tests/run_stress.sh              # chaque preset, chaque paramètre aux extrêmes, sous UBSan : aucun entier ne déborde
node tests/shots.mjs && python3 tests/png.py   # les écrans de tests/tour.txt -> build/shots/sheet.png, et un WAV
node tests/levels.mjs            # chaque preset : crête, RMS, écrêtage, DC
```

Mêmes règles que KAWA : entiers seulement, pas de division 64 bits, une interruption audio courte, et des registres
touchés seulement dans `hal/`. `./build.sh` vérifie tout cela. La mémoire de stockage (un secteur de 32 patches à
`0x97000`) reste hors de la zone qu'une mise à jour efface.

## Licence

GPL-3.0 (comme KAWA). Polices de l'écran : DejaVu Sans Bold (licence Bitstream Vera).
