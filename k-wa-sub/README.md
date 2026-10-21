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

50 presets d'usine (+ INIT), chacun avec son genre : DNB, DUB, DUBSTEP, TECHNO, JUNGLE, UK GARAGE, TRAP, HOUSE, NEUROFUNK,
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

| Bouton | Page | Vues (le même bouton à nouveau) |
| --- | --- | --- |
| `HOME` | 01 PLAY | patch courant + macros live · mix live |
| `EDIT` | 02 BASS | OSC A · OSC B · OSC C (SAMPLE) · SUB + NOISE · COMBINE |
| `ENV` | 03 ENVELOPE | AMP · FILTER · MOD |
| `SEQ` | 04 FILTER | cutoff / résonance / drive · modulations du filtre · **SEQ BASS** |
| `LFO` | 05 LFO | LFO 1 WOBBLE · LFO 2 · STEP FX · matrice |
| `FX` | 06 FX | saturation · compresseur · crush · chorus · phaser · delay · reverb · master |
| `ARP` | 07 PRESETS | banque A (usine) · banque B (utilisateur) · **ARP** |
| `GLO` | 08 SYSTEM · MIDI | canal MIDI, mode de voix, glide, bend, vélocité, tempo, horloge, octave, style |

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
MINT, noir et blanc, INVERTED (tons de gris : fond gris clair, cases un peu plus foncées, texte sombre), RED (le
style STUDIO en rouge) et ICE (bleu glacier) : 8 palettes par thème. `SYSTEM › FONT` : SANS (DejaVu Sans Bold), MONO (IBM Plex Mono),
HYPER (Atkinson Hyperlegible Mono, faite pour être lue par tous) ou PIXEL (Press Start 2P, nette à 8 px).
`SYSTEM › CONTRAST` (sous PALETTE) : `HIGH` (par défaut) étire chaque palette pour que le fond soit vraiment noir et
le texte vraiment blanc (les cartes gardent un léger gris pour rester visibles) ; `MAX (DEEP BLACK)` choisit en plus la
courbe gamma la plus sombre de l'écran (ST7789, 2,5 au lieu de 2,2 : noirs plus profonds, visible seulement sur le
FM-1) ; `NORMAL` garde les couleurs d'avant. Avec INVERTED, c'est l'inverse : fond blanc, texte noir.

`SYSTEM › LOUPE` (juste sous FONT) : les lignes et les valeurs passent à la plus grande taille qui tient dans leurs
cases (SANS 9 → 11, MONO et HYPER 10 → 12, valeurs 11-12 → 13-14) ; un texte qui toucherait le bord de sa case repasse
à la taille du dessous. PIXEL n'a que 8 et 16 : la loupe n'y change rien.

**Visuels.** Chaque vue montre ce qu'elle règle : forme d'onde de chaque oscillateur et du sub, enveloppes (avec un
point qui suit la note jouée), courbe du filtre (avec le cutoff modulé en direct), LFO (avec leur position),
grille STEP (avec le pas joué), courbe de saturation, crush, chorus, échos du delay, queue de la reverb, vu-mètre et
compression du master, charge des cœurs. Chaque moteur a son dessin : **WAVE** montre les 8 images de la table en
profondeur, celle qui est lue en clair (elle avance quand on tourne POSITION) ; **FM** montre les deux opérateurs
(le modulateur et son ratio, la flèche de l'index, la boucle de feedback, la porteuse) ; **SUPER / REESE** l'éventail
des scies selon le detune ; **ANALOG** sa place sur l'échelle SIN TRI SAW SQR. Le compresseur dessine sa courbe, ce
qu'il retire et un tour de son PUMP ; le phaser ses creux qui bougent ; delay et reverb changent de dessin avec leur
type. La grande valeur qui s'affiche quand on tourne se place sous le dessin, pour le voir changer.

**OSC C · SAMPLE** (`EDIT`, juste après OSC B ; dans OSC A et B, ENGINE est maintenant la 1re ligne, donc `K1`) :
une couche de sample par-dessus les deux oscillateurs, mélangée après le filtre (comme le SUB en DIRECT), avant
l'enveloppe d'amplitude. 10 sons très différents, faits par le générateur (rien d'enregistré, 8 bits µ-law) : 808 BOOM,
KICK, SNARE, OI VOX, REESE LOOP, GROWL LOOP, PLUCK, FM BELL, METAL HIT, BRASS STAB ; et 5 textures en boucle : VINYL,
TAPE HISS, RAIN, WIND, DATA. Réglages : SAMPLE, LEVEL, PLAY (AUTO : comme le sample est fait ; LOOP : en boucle ;
ONCE : une fois), SPEED (100 % à 10 % : plus lent sans changer la hauteur, par deux grains qui se croisent ; la
hauteur se règle avec TUNE), TUNE (±24 demi-tons), PITCH (AUTO : les sons suivent le clavier, les textures non ;
KEYS ; FIXED). La vue montre la forme du sample et où en est la dernière note. PHASE DUB
(VINYL), TAPE TECHNO (TAPE HISS) et FM 808 (KICK) s'en servent.

**Listes déroulantes** : choisir une SOURCE de la matrice ou une cible de MACRO (fenêtre SEL › MACROS) ouvre la liste,
comme pour les destinations des LFO.

**Effets.** **COMPRESSEUR** : AMOUNT (seuil et ratio ensemble, avec gain de rattrapage), PUMP (le creux du sidechain
sur le tempo, 1/4, 1/8, 1/2 ou 1 mesure, sur toute la sortie, reverb comprise), ATTACK, RELEASE. **PHASER** : 4
étages par côté, RATE, DEPTH, FEEDBACK, MIX. **DELAY › TYPE** : PING-PONG, TAPE (pleurage, saturé, plus sombre),
DIGITAL (net, stéréo), DUB (plus sombre, plus long). **REVERB › TYPE** : ROOM, HALL (celle d'avant), PLATE (dense,
claire), SPRING (ressort : bande médium dans des peignes courts, pour le dub). Réglages modulables par les LFO :
COMP AMOUNT, PHASER RATE / DEPTH / MIX. **GLIDE** (durée, 5 ms à 2 s ; vers 40 ms à mi-course basse, 350 ms aux trois quarts ; une voix qui ne jouait plus glisse depuis la dernière note, sauf en LEGATO) : `EDIT › COMBINE` et `SYSTEM`. **A / B MIX** (`EDIT › COMBINE`, `K4`, à côté de MODE et des niveaux A et B) : la balance entre les deux oscillateurs, le plus fort des deux garde son niveau. Les patches
utilisateur gardent ces nouveaux réglages (dans le 2e secteur du stockage) ; ceux d'avant les ont éteints.

**Nouveaux presets** : SYNC SWEEP (dubstep, sync modulée), PUMP HOUSE, PHASE DUB (phaser, delay TAPE, reverb SPRING),
TAPE TECHNO, REESE PHASER, FM 808, WAVE MORPH (dub techno, delay DUB, reverb PLATE), PLATE STAB.

`SELECT` choisit la ligne, `ALGORITHM` change sa valeur, `K1`–`K4` règlent toujours **les 4 premières lignes de la vue, dans l'ordre** (les réglages les plus joués sont mis en tête ; le bas de l'écran les rappelle) (dans
l'émulateur, leur fonction s'affiche sous chaque potard), `PRESETS` charge le patch suivant depuis n'importe quelle
page. `SAVE` ouvre l'enregistrement (emplacement, nom lettre par lettre, style) ; `SAVE` à nouveau écrit.
**`SAVE` maintenu = UNDO / REDO** : une fenêtre s'ouvre, `OCT−` annule la dernière modification, `OCT+` la refait
(les tours d'un même bouton dans la même seconde et demie comptent pour une seule ; un chargement de preset aussi),
jusqu'à 16 en arrière ; relâcher `SAVE` ferme la fenêtre.
`OCT-` / `OCT+` : octave du clavier ; les deux ensemble : tout couper.

**Accélération des potards.** Plus on tourne vite, plus le pas est grand (jusqu'à ×6, ×12 en FAST ; les petites
plages restent fines, le tempo passe de 0,1 BPM à plusieurs BPM). `SYSTEM › KNOB ACCEL` : OFF, ON (par défaut),
FAST. Monter un réglage d'un effet éteint (ou du STEP FX) l'allume.

## Les fenêtres SEL : FX LIVE, MACROS, MORPH

`SEL` n'ouvre plus les vues de la page (c'est le bouton de la page qui les fait défiler) : il ouvre quatre fenêtres :
`SEL` une fois FX LIVE, deux fois MACROS, trois fois MORPH X/Y, quatre fois MORPH A/B, cinq fois retour. Un bouton de page referme la fenêtre et va à sa page.
`SEL` maintenu reste A/B (COMPARE).

**FX LIVE** : des effets à jouer en direct (pas enregistrés, éteints à chaque démarrage).

| Potard | Effet |
|---|---|
| `K1` DJ FILTER | au milieu : rien ; à gauche passe-bas, à droite passe-haut (résonance : `FILTER RESO`) |
| `K2` PUMP | le creux du sidechain sur le tempo (`PUMP RATE` : 1/4, 1/8, 1/2, 1 mesure) |
| `K3` STUTTER | rejoue la dernière tranche en boucle : 1/4, 1/8, 1/16, 1/32, 1/64 (puis OFF : on repart) |
| `K4` TAPE STOP | ralentit comme une bande jusqu'à l'arrêt (STOP) ; on revient à 0 : retour au direct en fondu |

`SELECT` + `ALGO` atteignent aussi PUMP RATE et FILTER RESO. STUTTER et TAPE agissent avant la saturation et les
effets (le delay et la reverb continuent derrière), le DJ FILTER et le PUMP à la sortie.

**MACROS** : `K1`–`K4` = 4 macros, chacune envoie jusqu'à 4 cibles avec leur quantité. Elles agissent sur tous
les sons (elles ne sont pas dans le patch) ; leurs cibles sont enregistrées, leur position revient à 0 au démarrage.
Au départ : M1 FILTER (cutoff, résonance), M2 WOBBLE (profondeur et vitesse du LFO 1), M3 GRIT (drive du filtre,
bruit, saturation), M4 TONE (le 1er réglage des deux oscillateurs, X-MOD). `SELECT` choisit une cible (M1 cible 1,
sa quantité, M1 cible 2…), `ALGO` la change ; une cible choisie sans quantité part à +50 %.

## MORPH (SEL 3 et 4)

Le son passe d'un son à un autre en continu. Les réglages qui ont une valeur glissent ; ceux qui sont un choix (moteur,
type de filtre, destination, ON/OFF) prennent la valeur du son le plus proche. Un réglage identique dans tous les sons
n'est pas touché (une modification faite à la main reste). Le calcul se fait dans la boucle principale, pas dans
l'audio. « NOW » = le son qui jouait à l'ouverture de MORPH (recapturé à chaque ouverture tant qu'on n'a pas bougé le
morph ; charger un preset relâche le morph). `SAVE` enregistre le résultat, `SAVE` maintenu annule.

**MORPH X/Y** : quatre sons aux coins (A en haut à gauche = NOW au départ, B en haut à droite, C et D en bas).
- `K1` X, `K2` Y : le point dans le carré ; à droite, la part de chaque son.
- `K3` (ou `SELECT`) choisit un coin, `K4` (ou `ALGO`) lui donne un son : NOW, un preset d'usine ou un patch utilisateur.

**MORPH A/B** : A = NOW, B = un son choisi.
- `K1` MORPH 0–100 %, `K2` (ou `ALGO`) choisit B.
- `K3` PARTS : ALL, OSC, FILTER, ENV, MOD, FX (seulement cette partie bouge).
- `K4` TIME (1 temps à 16 mesures) ; `PLAY` (dans cette fenêtre) fait le trajet tout seul vers l'autre bout, sur le tempo.
- La liste montre les réglages qui changent le plus : trait cyan = A, violet = B, blanc = maintenant.

**MORPH X**, **MORPH Y** et **MORPH A/B** sont des destinations (LFO, matrice, STEP, MACROS, juste après RESO) : un LFO
sur X et l'autre sur Y font tourner le son entre les quatre coins sur le tempo. Le LFO (ou le slot) qui pilote le morph
n'est jamais lui-même morphé. Les choix de sons sont enregistrés (avec SEQ / MACROS) ; ~1 Ko de RAM.

## EQ (FX maintenu)

`FX` maintenu 0,6 s ouvre l'**EQ** (et le referme). Il agit sur la sortie de tous les sons (comme un EQ de table de
mixage, juste avant le master) et il est gardé avec SEQ BASS, ARP et MACROS.

- En haut, la **courbe** (20 Hz à 18 kHz, ±12 dB, repères 100 Hz, 1 kHz, 10 kHz) et un point par bande, de la couleur
  de son potard ; en dessous, les **4 bandes** : type, fréquence, gain, largeur.
- `K1`–`K4` : la **fréquence** de chaque bande (20 Hz à 18 kHz) ; `OCT−` maintenu + potard : son **gain**
  (−12 à +12 dB) ; `OCT+` maintenu + potard : son **type** ; `SELECT` choisit une bande, `ALGO` sa **largeur** (Q 0,4 à 5).
- 7 types par bande : OFF, LOW CUT, LOW SHELF, BELL, NOTCH, HIGH SHELF, HIGH CUT (12 dB/oct pour les coupe-bas et
  coupe-haut, la largeur donne leur résonance). Au départ : LOW SHELF 85 Hz, BELL 313 Hz, BELL 2 kHz, HIGH SHELF
  7,5 kHz, tout à 0 dB (rien ne change tant qu'on ne monte ou ne baisse pas un gain).

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
| `SAVE` | UNDO / REDO : tant que `SAVE` est tenu, `OCT−` annule, `OCT+` refait (16 pas) ; sur la page PRESETS : ajoute ou retire le preset sélectionné des favoris, marqué `*` |
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
`install/version-b/`), ne garde que **4 moteurs**, un par usage (la question CLASSIC / SHIFT y propose SHIFT
d'abord) :

| Moteur gardé | Rôle | Ce qu'il remplace |
|---|---|---|
| ANALOG | soustractif : sinus → triangle → scie → carré, largeur, drive | FOLD (sinus + saturation FOLD), SYNC (voir plus bas) |
| REESE | pile de scies désaccordées (2 à 7) | SUPER |
| WAVE | 8 tables × 8 images, position, warp | VOCAL (table VOCAL) |
| FM | 2 opérateurs, ratio, feedback | PHASE |

Le **hard sync** devient un mode du mélange A + B : `COMBINE › MODE = SYNC` (B redémarre avec A, `X-MOD` monte B
de 0 à +24 demi-tons, modulable par les LFO : la vraie « sync sweep »). Ce mode existe aussi dans la version
normale. Les 9 presets d'usine qui utilisaient PHASE, FOLD, SYNC ou VOCAL ont leur propre version B, refaite sur les
4 moteurs (CONCRETE, YOI TALKER, NEURO GROWL, PHASE HORN, SYNC RIPPER, TEAROUT, NEURO STAB, MINIMAL CLICK, YOI
RIDDIM) ; les patches utilisateur faits avec ces moteurs sont convertis au chargement et restent compatibles entre
les deux versions.

## Deux façons de jouer : CLASSIC ou SHIFT

À chaque démarrage, K-WA SUB demande comment jouer (dans les deux versions, A et B) :

- **CLASSIC** : des pages de listes, comme un synthé classique. `SELECT` choisit la ligne, `ALGO` change sa valeur,
  `K1`–`K4` règlent les 4 premières lignes ; `OCT−` / `OCT+` changent l'octave.
- **SHIFT** : 12 réglages par page en 3 colonnes (détail ci-dessous), `OCT−` / `OCT+` maintenus servent de Shift.

`OCT−` choisit CLASSIC, `OCT+` choisit SHIFT ; un potard bascule entre les deux et `SEL` (ou un autre bouton)
valide ; sans réponse en 20 s, le dernier choix est gardé. Ensuite : `SYSTEM › UI MODE` change de mode à tout
moment, `SYSTEM › ASK AT START` = NO pour ne plus avoir la question.

**Mode SHIFT.** Sur PLAY, EDIT, ENV, FILTER, LFO et FX, le premier écran montre 12 réglages en 3 colonnes de 4 (une
ligne par potard `K1`–`K4`) :

| Colonne | Pour la jouer |
|---|---|
| gauche : la base (MIX, AMP, WOBBLE, SPACE…) | les potards seuls |
| milieu : OSC A, FILTER ENV, LFO 1, DRIVE… | `OCT−` maintenu + potard |
| droite : OSC B, MOD ENV, LFO 2, MASTER… | `OCT+` maintenu + potard |

`SYSTEM › SHIFT COLOURS` : THEME (par défaut : la couleur du thème, un peu plus claire au milieu, un peu vers sa
2e couleur à droite) ou GREEN YEL ORANGE (vert fluo / jaune / orange).

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

## MIDI

`GLO` (SYSTEM), 2e vue : **MIDI**.

| Ligne | Ce qu'elle fait |
|---|---|
| MIDI LEARN | tourner à droite : arme ; puis tourner n'importe quel réglage (potard, ALGO, fenêtres) ; puis bouger un CC sur le contrôleur : ce CC pilote ce réglage sur toute sa plage (16 CC au plus, gardés) |
| CC MAP | combien de CC sont appris ; tourner à gauche : tout effacer |
| MIDI CHANNEL, CLOCK | comme avant (OMNI ou 1-16 ; horloge interne ou MIDI) |
| START / STOP | Start / Stop / Continue d'un séquenceur ou d'un DAW lancent et arrêtent le SEQ BASS |
| PROGRAM CHANGE | 0 à 50 : la banque A ; au-delà : les patches de la banque B |
| CLOCK OUT | l'horloge interne envoyée (24 par temps) avec Start/Stop quand le SEQ part ou s'arrête |

Les CC fixes (1 molette, 71 résonance, 74 cutoff, 76 vitesse et 77 profondeur du LFO 1, 123 tout couper) restent,
sauf s'ils ont été appris pour autre chose.

## À faire

- Rien de la liste : MORPH X/Y et A/B sont faits (fenêtres `SEL` 3 et 4).

## Installer sur le FM-1

Le paquet est [`install/k-wa-sub-0.9.9-9.fwsc`](install/k-wa-sub-0.9.9-9.fwsc) (identité `FM-1_979` ; les anciens dans `install/older/`). Il s'installe exactement
comme KAWA, avec le même chargeur de mise à jour, et fonctionne depuis le firmware d'origine comme depuis KAWA :

- **Installateur rapide** : ouvrir [`installer.html`](installer.html) dans Chrome ou Edge (un seul fichier, hors ligne,
  le firmware est dedans), cliquer sur « Installer K-WA SUB », confirmer. Il accepte aussi un autre .fwsc (KAWA...).
- **Dans le navigateur** (Chrome ou Edge, Web MIDI) : la page d'installation de KAWA ([`../index.html`](../index.html)),
  bouton « Use a local .fwsc file… », puis choisir `k-wa-sub-0.9.9-9.fwsc`.
- **Avec Python 3** : `INSTALL-Linux.sh`, `INSTALL-Mac.command` ou `INSTALL-Windows.bat` dans ce dossier (ou
  `python3 install/fm1_install.py install/k-wa-sub-0.9.9-9.fwsc`).

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
- si le cœur 1 ne suit pas (6 blocs en retard ou coupés sur les 32 derniers : ~0,2 s), le cœur 0 reprend les voix,
  `CORE 1 TOO SLOW: 1 CORE` s'affiche et `AUDIO CORES` passe à `SINGLE` ;
- (avant : seulement 3 blocs de suite), le cœur 0 reprend les voix aussitôt, `CORE 1 STUCK: ONE CORE` s'affiche et `AUDIO CORES` passe à `SINGLE` (à remettre sur `DUAL` dans SYSTEM pour réessayer) ;
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

Un bloc au-dessus de 92 % allège tout de suite d'un cran. Après 2 s sous 45 %, on remonte d'un cran. `CPU LOAD`
affiche `· -1`, `-2` ou `-3` quand le son est allégé.

**Budget d'un bloc** (0.9.9-5) : le temps des effets (EQ, compresseur, delay, reverb...) est mesuré à chaque bloc, et
les voix reçoivent seulement ce qui reste sur 80 % du bloc (au moins un quart). L'attente du cœur 1 est limitée à
un demi-bloc. Avant, seules les voix étaient limitées : avec beaucoup d'effets, l'interruption audio pouvait prendre
tout le temps, la boucle principale ne nourrissait plus le chien de garde (~8 s) et **le FM-1 redémarrait / semblait
s'éteindre**. Aucune fonction n'a été retirée : la mémoire n'est pas en cause (RAM 52 Ko sur 96, flash 479 Ko sur 582).

**LAST STOP** : si le dernier arrêt n'était pas normal, l'écran de démarrage et la question CLASSIC / SHIFT
l'affichent en orange, et `SYSTEM › LAST STOP` le garde :
- `WATCHDOG (TOO BUSY)` : le processeur était surchargé (le budget ci-dessus doit l'éviter) ;
- `LOW VOLTAGE (POWER)` / `1.2V LOW (POWER)` : l'alimentation a chuté (batterie faible, câble ou port USB trop faible) ;
- `LONG PRESS` : bouton d'alimentation maintenu ;
- `CRASH` + adresse : une erreur du programme (à me transmettre) ;
- `POWER OFF` / `RESTART` : arrêt normal.

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
