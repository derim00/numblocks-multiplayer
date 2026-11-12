# NumBlocks Multiplayer : Minecraft en multijoueur sur NumWorks

**Joue à plusieurs à NumBlocks, le Minecraft 1.8.8 de la NumWorks, chacun sur sa
calculatrice et depuis chez soi.** Un joueur ouvre son monde, les autres le
trouvent dans la liste des parties et le rejoignent : vous construisez dans le
même monde, vous vous voyez et vous vous parlez.

Ce dépôt est une version modifiée de **NumBlocks**, le jeu de
[Mason Chen](https://github.com/Mason363) qui fait partie de
**[NumPlay](https://github.com/Mason363/NumPlay)**. Tout le jeu est son travail ;
ce dépôt ajoute seulement le multijoueur. Merci à lui, et va voir NumPlay : plus
de 50 jeux pour calculatrices.

Le multijoueur passe par **[NumSocial](https://github.com/derim00/numsocial)** :
la calculatrice reste branchée en USB à un ordinateur (Mac ou Windows) qui fait
le lien avec Internet.

## Ce que fait le multijoueur

- **Multiplayer**, sur l'écran titre : la liste de toutes les parties ouvertes
  dans le monde (nom du monde, hôte, joueurs, mode). Choisis-en une avec les
  flèches, **Join Server**.
- **Ton pseudo** : bouton « Name » de cet écran (alpha pour les lettres).
- **Ouvrir ton monde** : en jeu, Pause > **Open to LAN** > choisis le mode des
  invités (Survie ou Créatif) > **Start LAN World**. Le chat affiche le code de
  la partie ; elle apparaît dans la liste de tout le monde.
- **En partie** : les autres joueurs apparaissent, chacun avec un t-shirt de
  couleur et son pseudo au-dessus de la tête. Les blocs posés et cassés sont
  partagés (même ceux d'avant ton arrivée), le chat (touche ×) aussi, et l'heure
  suit celle de l'hôte.
- **Quitter** : Pause > **Disconnect**. Quand l'hôte ferme sa partie, les
  autres sont prévenus.
- Jusqu'à **8 joueurs** par partie.

Pas besoin d'être sur le même Wi-Fi : malgré le nom du bouton (celui de
Minecraft), tout passe par Internet, sans réglage de box ni compte à créer.

**Et sans Internet ?** Chaque partie ouverte est aussi annoncée sur le réseau
local : les ordinateurs sur le même Wi-Fi la voient dans la liste, marquée
**[LAN]**, et jouent en direct entre eux, même sans connexion à Internet (au
lycée, en voiture avec un partage de connexion...). Sous Windows, accepte la
première fois que le pare-feu demande d'autoriser NumSocial sur les réseaux
privés.

### Limites de cette version

- Seul l'**Overworld** est partagé : un joueur dans le Nether ou l'End disparaît
  pour les autres.
- Chaque calculatrice fait vivre **ses propres monstres et animaux**.
- Le **contenu des coffres** et des fours n'est pas partagé.
- Pas de dégâts entre joueurs.

## Installer

Le multijoueur a besoin du firmware NumSocial (qui prête son lien USB au jeu) :
NumBlocks Multiplayer s'installe donc **avec NumSocial**, en cochant
**NumBlocks** au moment d'installer.

1. Suis le tutoriel de **[NumSocial](https://github.com/derim00/numsocial#installation-lappli-principale)**
   pour obtenir l'appli NumSocial (ta copie privée la compile, NumBlocks compris).
2. Sur la page de NumSocial, coche **NumBlocks (multijoueur)**, puis
   **Installer sur la calculatrice**.
3. Laisse NumSocial ouvert sur l'ordinateur et la calculatrice branchée, ouvre
   **NumBlocks** > **Multiplayer**.

NumBlocks Multiplayer marche aussi sans ordinateur, en solo, comme NumBlocks.

## Comment ça marche

```
NumBlocks (calculatrice) ⇄ USB ⇄ NumSocial (ordinateur) ⇄ Internet ⇄ NumSocial (ordinateur de l'hôte) ⇄ USB ⇄ NumBlocks
```

- **L'ordinateur de l'hôte est le serveur** : il garde la liste des blocs
  modifiés et des joueurs, accueille ceux qui arrivent et fait passer le reste.
- **Les ordinateurs se trouvent par un broker MQTT public**
  (`broker.emqx.io`, avec deux autres en secours) : la liste des parties est un
  ensemble de messages retenus. Rien à créer, rien à héberger.
- **Le monde ne voyage pas** : il est recréé à partir de sa graine sur chaque
  calculatrice ; seuls les blocs modifiés circulent, plus les positions 5 fois
  par seconde.
- **Sur le réseau local**, l'ordinateur de l'hôte annonce la partie (multicast
  et broadcast UDP) et la sert en TCP : la même chose que par le broker, sans
  Internet.
- **Côté calculatrice**, le jeu parle à NumSocial par le lien USB que le
  firmware NumSocial publie pour les applis externes, dans une table repérée
  par sa signature (`src/numsocial_link.h`).
  Les nouveautés sont dans `src/mp.c` (le multijoueur), `src/gui.c` (les
  écrans), `src/render.c` et `src/mob.c` (les autres joueurs).

## Tester sur un ordinateur

Le jeu se compile aussi pour l'ordinateur : deux parties qui se rejoignent, chacune
avec sa passerelle NumSocial, à travers le vrai broker (sous un préfixe de test,
jamais dans la vraie liste).

```bash
cd games/numblocks
make build/mp
python3 tests/mp_test.py CHEMIN/VERS/numsocial   # (son .venv avec paho-mqtt)
```

## Licence

Le code du jeu est sous **GNU GPL v3.0**, comme NumBlocks : voir [LICENSE](LICENSE).
Les textures, la police et les écrans viennent de Minecraft 1.8.8 et
appartiennent à Mojang ; ils ne sont pas couverts par cette licence.
`src/numsocial_link.h` est sous licence MIT, pour que n'importe quelle appli
puisse l'utiliser.

Inspiré de Minecraft, sans lien avec Mojang ou Microsoft, ni avec NumWorks.
Version d'origine : NumPlay, commit indiqué dans `UPSTREAM`.
