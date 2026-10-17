# Plugins de sources audio

Un plugin apporte des sons à EasyLunii : épisodes de podcasts, vidéos, catalogue d'un site… Dans l'assistant (**Depuis une source…**) et dans l'éditeur (**Source…** sur une scène), l'utilisateur cherche ou colle un lien, écoute, coche des sons. EasyLunii les télécharge par le plugin, puis les convertit pour la Lunii.

Un plugin est un **programme indépendant**, écrit dans n'importe quel langage. EasyLunii le lance pour chaque demande, lui envoie une requête JSON et lit sa réponse JSON.

Plugins fournis :
- `podcast` : flux RSS, recherche dans l'annuaire Apple Podcasts, pages d'émission (dont Radio France) ;
- `youtube` : via yt-dlp, téléchargé automatiquement au premier usage.

## Installation

Un plugin est un dossier contenant `plugin.json` et son exécutable. EasyLunii parcourt dans l'ordre :

| Système | Dossiers |
|---|---|
| Linux | `<dossier de easylunii>/plugins/`, `<préfixe>/share/easylunii/plugins/`, `~/.local/share/easylunii/plugins/` |
| Windows | `<dossier de easylunii.exe>\plugins\`, `%LOCALAPPDATA%\easylunii\plugins\` |

Si deux plugins ont le même `id`, le premier trouvé est utilisé. Pour ajouter le vôtre, déposez son dossier dans le dernier emplacement.

## plugin.json

```json
{
    "id": "monplugin",
    "name": "Mon catalogue",
    "version": "1.0",
    "description": "Phrase affichée sous le choix de la source.",
    "executable": "monplugin.py",
    "search": true,
    "searchHint": "titre à chercher",
    "link": true,
    "linkHint": "lien d'une page du site"
}
```

| Champ | Rôle |
|---|---|
| `id` | identifiant unique, utilisé pour le cache (lettres, chiffres, tirets) |
| `executable` | chemin relatif au dossier du plugin ; sous Windows, `.exe` est ajouté s'il manque une extension `.exe`/`.bat` |
| `search` / `link` | commandes prises en charge : le texte saisi commençant par `http(s)://` ou `www.` est un lien, sinon une recherche |
| `searchHint`, `linkHint` | textes d'aide du champ de saisie |
| `icon` | facultatif : image (PNG…) relative au dossier |

Sous Linux, l'exécutable doit avoir le droit d'exécution (`chmod +x`). Un script peut commencer par `#!/usr/bin/env python3`.

## Appel

```
<exécutable> <commande>        commande : search | open | fetch
```

- **Entrée standard** : une requête JSON, puis fin de fichier.
- **Sortie standard** : des lignes JSON (une par ligne, vidées au fil de l'eau) :
  - progression, facultative : `{"progress": 0.42, "message": "Téléchargement : 42 %"}` (`progress` entre 0 et 1, `-1` si inconnue) ;
  - puis **une** ligne finale : `{"result": { … }}` ou `{"error": "message lisible par l'utilisateur"}`.
- **Sortie d'erreur** : libre (journal). Elle est montrée seulement si le plugin s'arrête sans réponse.
- **Annulation** : EasyLunii arrête le processus. Le plugin doit faire de même avec ses propres sous-processus.
- **Répertoire courant** : le dossier du plugin.

Variables d'environnement fournies :

| Variable | Contenu |
|---|---|
| `EASYLUNII_PLUGIN_DATA` | dossier inscriptible propre au plugin (outils téléchargés, réglages) : `~/.local/share/easylunii/plugin-data/<id>/`, sous Windows `%LOCALAPPDATA%\easylunii\plugin-data\<id>\` |
| `EASYLUNII_FFMPEG` | chemin de ffmpeg s'il est connu |
| `EASYLUNII_VERSION` | version d'EasyLunii |

## Commandes

### `search`
Requête : `{"query": "loup"}` — réponse : `{"result": {"items": [ … ]}}`

### `open`
Requête : `{"url": "https://…"}` — réponse : `{"result": {"title": "Nom de l'émission", "items": [ … ]}}`

Également appelée quand l'utilisateur ouvre un élément de type `collection` (double-clic) : `url` vaut alors le champ `url` de l'élément, ou son `id` à défaut.

### `fetch`
Requête : `{"item": { …élément tel que renvoyé… }, "dir": "/dossier/temporaire"}`

Réponse : `{"result": {"audio": "/dossier/temporaire/audio.m4a", "image": "/dossier/temporaire/image.jpg", "title": "…"}}`

Les fichiers doivent être écrits **dans `dir`**. EasyLunii les range ensuite dans son cache (`~/.cache/easylunii/sources/<id>/`, sous Windows `%LOCALAPPDATA%\cache\easylunii\sources\<id>\`). Un élément déjà récupéré (même `id`) n'est plus redemandé. `image` et `title` sont facultatifs. Tout format audio lu par ffmpeg convient : la conversion en MP3 pour la Lunii est faite par EasyLunii.

## Éléments

| Champ | | Rôle |
|---|---|---|
| `id` | **obligatoire** | identifiant stable et unique dans le plugin (sert au cache) |
| `kind` | | `"audio"` (par défaut) ou `"collection"` (émission, playlist : s'ouvre par `open`) |
| `title` | **obligatoire** | titre affiché, et titre de l'histoire par défaut |
| `subtitle`, `description` | | textes affichés |
| `duration` | | secondes |
| `date` | | date ISO 8601 (`2026-10-17` ou date et heure) |
| `image` | | URL ou chemin d'une vignette (affichée dans la liste) |
| `url` | | lien de la page ou de la collection |
| `audioUrl` | | URL directe du son : active le bouton **Écouter** avant téléchargement |
| `group` | | `{"title": "Nom de la série", "part": 2}` : regroupement des parties avec l'option « Séries regroupées » |

Tout autre champ est conservé et renvoyé tel quel dans `fetch`. On peut y ranger ce dont le plugin a besoin.

## Exemple minimal (Python)

```python
#!/usr/bin/env python3
# Catalogue fixe : search filtre par titre, fetch télécharge le son.
import json, sys, urllib.request

CATALOGUE = [
    {"id": "conte-1", "title": "Le loup et l'agneau", "audioUrl": "https://exemple.org/loup.mp3"},
    {"id": "conte-2", "title": "La cigale et la fourmi", "audioUrl": "https://exemple.org/cigale.mp3"},
]

def reply(obj):
    print(json.dumps(obj, ensure_ascii=False), flush=True)

cmd, req = sys.argv[1], json.load(sys.stdin)
try:
    if cmd == "search":
        q = req["query"].lower()
        reply({"result": {"items": [i for i in CATALOGUE if q in i["title"].lower()]}})
    elif cmd == "fetch":
        item, dest = req["item"], req["dir"] + "/audio.mp3"
        reply({"progress": -1, "message": "Téléchargement de " + item["title"]})
        urllib.request.urlretrieve(item["audioUrl"], dest)
        reply({"result": {"audio": dest, "title": item["title"]}})
    else:
        reply({"error": "Commande non prise en charge : " + cmd})
except Exception as e:
    reply({"error": str(e)})
```

Pour tester sans EasyLunii : `echo '{"query":"loup"}' | ./monplugin.py search`.

## Bon usage

Les sons récupérés par un plugin restent soumis aux droits de leurs auteurs et aux conditions des sites. Ils sont destinés à un usage personnel. Un plugin ne doit pas contourner une protection technique (DRM, abonnement).
