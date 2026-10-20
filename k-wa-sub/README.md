# K-WA SUB

Un synthétiseur de basses pour le **M-VAVE FM-1** : drum & bass, dub, dubstep, techno. Né du tracker
[KAWA](../README.fr.md) (même plateforme : mises à jour, secours USB, USB-MIDI, audio, écran, flash), mais sans
séquenceur, sans pistes, sans samples ni batteries : tout le processeur va au son.

**Essayer sans rien installer :** ouvrez [`emulator/index.html`](emulator/index.html) dans un navigateur
(un seul fichier, fonctionne hors ligne), cliquez sur « Allumer le son ».

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
| **Matrice** | 4 liaisons source › destination (LFO, enveloppes, vélocité, molette, aftertouch, note, aléatoire › cutoff, résonance, pitch, timbres A/B, niveaux, X-MOD, drive, ampli, sub, bruit, **profondeur du wobble**) |
| **FX** | SATURATION (soft, tube, hard, fold) · BITCRUSH · CHORUS (au-dessus de ~150 Hz : le sub reste mono) · DUB DELAY ping-pong calé sur le tempo, plus sombre à chaque répétition · REVERB · MASTER (glue, sub boost, sortie) |
| **Jeu** | MONO, LEGATO (glide entre touches tenues), POLY 4 voix ; glide ; pitch bend |

23 presets d'usine (DEEP PRESSURE, REESE CLASSIC, WOBBLE 01, YOI TALKER, NEURO GROWL, 808 DROP, DUB STAB, TECHNO
RUMBLE, ACID LINE…) et 32 emplacements utilisateur.

## Le wobble en live

- **Page PLAY** : `K1` cutoff, `K2` vitesse du wobble, `K3` profondeur, `K4` résonance. `SEL` passe à la seconde
  série (sub, drive, delay, sortie).
- La vitesse du wobble est **calée sur la grille** : passer de 1/4 à 1/8 puis 1/16T reste sur le temps (la phase se
  calcule depuis le compteur de temps, pas depuis le dernier réglage). En `RETRIG NOTE`, chaque note repart du début.
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
| `SEQ` | 04 FILTER | cutoff / résonance / drive · modulations du filtre |
| `LFO` | 05 LFO | LFO 1 WOBBLE · LFO 2 · matrice |
| `FX` | 06 FX | saturation · crush · chorus · delay · reverb · master |
| `ARP` | 07 PRESETS | banque A (usine) · banque B (utilisateur) |
| `GLO` | 08 SYSTEM | canal MIDI, mode de voix, glide, bend, vélocité, tempo, horloge, octave, style |

`SELECT` choisit la ligne, `ALGORITHM` change sa valeur, `K1`–`K4` font ce qu'indique le bas de l'écran (dans
l'émulateur, leur fonction s'affiche sous chaque potard), `PRESETS` charge le patch suivant depuis n'importe quelle
page. `SAVE` ouvre l'enregistrement (emplacement, nom lettre par lettre, style) ; `SAVE` à nouveau écrit.
`OCT-` / `OCT+` : octave du clavier ; les deux ensemble : tout couper.

## Les deux cœurs

Le FM-1 (JieLi AC79) a deux cœurs. K-WA SUB est découpé pour eux (`firmware/src/ksub_dsp.c`) :

- **cœur 1** : les voix (oscillateurs, filtre, enveloppes, LFO, matrice) du bloc suivant ;
- **cœur 0** : l'interruption audio, les effets du bloc en cours, l'interface et l'USB.

Un bloc de latence (5,8 ms). **Reste à faire sur le matériel** : le démarrage du second cœur
(`firmware/hal/fm1_cpu1.h`) doit être lu dans le SDK JieLi et essayé sur un vrai FM-1 ; d'ici là il répond « non »
et tout tourne sur le cœur 0, avec exactement le même son (c'est aussi ce que fait l'émulateur).

## Construire

```sh
cd source
python3 tools/gen_ksub.py        # tables (hauteurs, filtre, enveloppes, tables d'onde) et polices
web/make_emulator.sh             # -> ../emulator/index.html (clang avec la cible wasm32 et wasm-ld)
node tests/shots.mjs && python3 tests/png.py   # les écrans de tests/tour.txt -> build/shots/sheet.png, et un WAV
node tests/levels.mjs            # chaque preset : crête, RMS, écrêtage, DC
```

Le paquet pour le FM-1 (`./build.sh` de KAWA, adapté : `tools/build.py`, identité `FM-1_97Y`, sortie
`k-wa-sub.fwsc`) n'a **pas encore été construit ni essayé** : l'installation viendra quand le son et l'interface
dans l'émulateur seront validés. Mêmes règles que KAWA : entiers seulement, pas de division 64 bits, une interruption
audio courte ; la mémoire de stockage (un secteur de 32 patches à `0x97000`) reste hors de la zone qu'une mise à
jour efface.

## Licence

GPL-3.0 (comme KAWA). Polices de l'écran : DejaVu Sans Bold (licence Bitstream Vera).
