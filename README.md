# CZ-101 Studio

Wrapper de uma máquina só: o driver `cz101` do MAME 0.289 embarcado num app JUCE.
MIDI entra pela interface MIDI, o som sai pela placa de áudio.

## Estrutura

| Pasta | O que é |
|---|---|
| `Source/PluginProcessor.*` | Liga o MAME, repassa MIDI, lê o áudio, salva a pasta de ROMs |
| `Source/PluginEditor.*` | UI: painel do MAME clicável, luz de MIDI, VU, teclado na tela |
| `Source/FrontendStubs.cpp` | Stubs do frontend do MAME (sem UI do MAME) |
| `Engine/EmbeddedEmulatorEngine.*` | Cópia do motor do VES com a lista de drivers só com `cz101` e NVRAM salva |
| `Resources/plugins` | `boot.lua` + plugin `layout` (necessário para o painel) |

O núcleo do MAME **não** é recompilado aqui: o CMake linka as bibliotecas e o
`cz101.o` já compilados na árvore do VES
(`VintageEmulatorStudio/validation/mame-0.289-patched/build`).
Para mudar o driver ou o chip de som (`src/mame/casio/cz101.cpp`,
`src/devices/sound/upd933.cpp`) é preciso recompilar o objeto correspondente lá.

## Build

```sh
./build.sh
```

Gera `build/CZ101Studio_artefacts/Release/Standalone/CZ-101 Studio.app`.

## Dados em tempo de execução

- ROMs: `~/Documents/VES-ROMs` (precisa de `cz101.zip` e `hd44780.zip`); dá para trocar no botão **ROMs...**
- Patches internos (NVRAM): `~/Library/Application Support/CZ101Studio/nvram/cz101`
- Configuração: `~/Library/Application Support/CZ101Studio/settings.xml`
