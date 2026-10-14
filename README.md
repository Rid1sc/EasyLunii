# Lunii Natif

Gestionnaire d'histoires pour la Lunii, en C++/Qt 6 natif : bibliothèque, transferts vers l'appareil, éditeur de packs façon STUdio.

Dérivé de [Lunii.QT](https://github.com/o-daneel/Lunii.QT) (o-daneel, GPLv3), dont le cœur a été réécrit en C++ en suivant son code Python comme référence. Format des packs : [STUdio](https://github.com/marian-m12l/studio) (marian-m12l, MPL-2.0). Licence : GPLv3.

## Fonctions

**Bibliothèque**
- Plusieurs dossiers surveillés (zip, 7z, pk), indexés dans SQLite, sans déplacer les fichiers. Seuls les fichiers modifiés sont relus.
- Grille de jaquettes ou liste. Jaquettes : base officielle Lunii, vignette STUdio ou premier écran du pack. Elles sont mises en cache.
- Recherche insensible aux accents (titre, auteur, description, étiquettes), filtre d'âge, tri (titre, âge, ajout, taille, durée).
- Collections (glisser des histoires sur une collection) et étiquettes.
- Glisser-déposer : de la bibliothèque (ou de Dolphin) vers la Lunii pour envoyer, et de la Lunii vers la bibliothèque pour sauvegarder.

**Lunii** (v1, v2, v3 ; Flam pas encore)
- Détection USB des Lunii branchées, même non montées. Montage et éjection par udisksctl.
- Ordre des histoires par glisser-déposer, masquage, mode nuit par histoire, réglages du mode nuit.
- Import : STUdio (zip/7z), `.plain.pk`, archives v1/v2 (zip/7z), sauvegardes personnelles v3. Conversion automatique des images et des sons.
- Export : `.plain.pk` sur v1/v2 (ou v3 avec les clés de l'appareil), sinon sauvegarde chiffrée liée à l'appareil.
- Récupération des histoires perdues et nettoyage des dossiers orphelins.
- Comme dans Lunii.QT, l'export des histoires officielles est refusé sur v1/v2.

**Éditeur**
- Graphe de nœuds : scènes (image, son, commandes) et choix (options parcourues avec la molette). Liens OK et Maison tirés à la souris. Un lien scène vers scène crée automatiquement le choix intermédiaire.
- Inspecteur, annuler/rétablir, rangement automatique, vérification du pack.
- Images recadrées en 320×240, sons convertis en MP3 mono 44,1 kHz.
- Simulateur de Lunii (molette, OK, maison, pause, lecture automatique).
- Assistant : des fichiers audio, ou des zips/dossiers de parties à réunir, donnent un pack à menus, avec thèmes et annonces vocales Piper.
- Envoi direct sur la Lunii.

## Sobriété

- Qt Widgets, sans navigateur embarqué. Environ 125 Mo de mémoire résidente avec une bibliothèque de 273 histoires, ou avec un pack de 150 Mo ouvert dans l'éditeur. Les sons restent sur disque.
- Lectures d'archives en flux (libarchive), analyse et transferts dans des threads de travail.
- La pile multimédia n'est chargée qu'à la première écoute.

## Compilation

Dépendances (Fedora) : `qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtsvg-devel qt6-qttools-devel libarchive-devel openssl-devel cmake ninja-build gcc-c++`.
Au moment de l'exécution : `ffmpeg` (conversions audio), `udisks2` (montage), et, facultatif, Piper dans `~/.local/share/piper` pour les annonces vocales.

```sh
cmake -S . -B ~/.cache/lunii-natif-build -G Ninja
cmake --build ~/.cache/lunii-natif-build
cmake --install ~/.cache/lunii-natif-build --prefix ~/.local
```

Le dossier de compilation est hors du disque externe : l'exFAT n'accepte pas les liens symboliques.

## Organisation

| Dossier | Contenu |
|---|---|
| `src/core` | Accès aux Lunii, chiffrement XXTEA/AES, formats (STUdio, plain, v2, v3), conversions, bases de titres. Sans interface. |
| `src/app` | Fenêtre, bibliothèque (index, modèle, page), panneau Lunii, file de tâches, éditeur (`editor/` : graphe, simulateur, assistant). |
| `src/cli` | `lunii-natif-cli` : accès au cœur en ligne de commande (tests, scripts). |
| `tests` | `lunii-natif-uitest` : tests d'interface sur une fausse Lunii. |

Données : index et réglages dans `~/.local/share/lunii-natif` et `~/.config/lunii-natif`, vignettes dans `~/.cache/lunii-natif`. Bases de titres et cache des jaquettes partagés avec Lunii.QT dans `~/.lunii-qt`.

## Tests

- **Cœur, comparé à Lunii.QT v3.1.4.** Les sorties sont identiques octet par octet pour XXTEA et AES (7 tailles), pour les imports STUdio, zip v2 et 7z sur une fausse v3 et une fausse v2, et pour l'export `.plain.pk` et son réimport. Seule différence : la mise en forme JSON de `_metadata.json`. Les fausses Lunii sont construites avec les `.md` de vrais appareils.
- **Interface.** `lunii-natif-uitest <dossier .md> <pack.zip> [zips…]` couvre le dépôt sur la Lunii, le refus des doublons, l'export, le masquage, la suppression, le glisser de la Lunii vers la bibliothèque et la réindexation, le dépôt sur une collection, l'éditeur (ajout, lien, annulation, suppression, enregistrement) et l'assistant.
- **Enregistrement des packs.** `lunii-natif-cli studio-resave` relit puis réenregistre un pack : `story.json`, les ressources et la vignette sont identiques.

## Pas encore fait

- Flam (`device_flam.py` de Lunii.QT).
- Téléchargement et sauvegarde du firmware (compte Luniistore).
- Assistant : les thèmes sont créés par tranches (« Histoires 1 à 5 »). Pour un regroupement par sujet, les renommer et les réorganiser dans l'éditeur.
