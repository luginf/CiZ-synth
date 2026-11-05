# CiZ-synth

Wrapper mono-machine : le driver `cz101` de MAME 0.289 embarqué dans une application JUCE.
Le MIDI entre par l'interface MIDI, le son sort par la carte audio.

## Structure

| Dossier | Contenu |
|---|---|
| `Source/PluginProcessor.*` | Branche MAME, relaie le MIDI, lit l'audio, sauvegarde le dossier des ROMs |
| `Source/PluginEditor.*` | UI : panneau MAME cliquable, voyant MIDI, VU-mètre, clavier à l'écran |
| `Source/FrontendStubs.cpp` | Stubs du frontend de MAME (pas d'UI MAME) |
| `Engine/EmbeddedEmulatorEngine.*` | Copie du moteur de VES avec une liste de drivers réduite à `cz101` et la NVRAM sauvegardée |
| `Resources/plugins` | `boot.lua` + plugin `layout` (nécessaire pour le panneau) |

Le cœur de MAME n'est **pas** recompilé ici : CMake lie les bibliothèques et le
`cz101.o` déjà compilés dans l'arborescence de VES
(`VintageEmulatorStudio/validation/mame-0.289-patched/build`).
Pour modifier le driver ou la puce sonore (`src/mame/casio/cz101.cpp`,
`src/devices/sound/upd933.cpp`), il faut recompiler l'objet correspondant là-bas.

## Build

```sh
./build.sh
```

Produit `build/CiZSynth_artefacts/Release/Standalone/CiZ-synth.app`.

## Données à l'exécution

- ROMs : `~/Documents/VES-ROMs` (il faut `cz101.zip` et `hd44780.zip`) ; modifiable avec le bouton **ROMs...**
- Patches internes (NVRAM) : `~/Library/Application Support/CiZ-synth/nvram/cz101`
- Configuration : `~/Library/Application Support/CiZ-synth/settings.xml`
