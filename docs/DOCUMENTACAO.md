# CZ-101 Studio — Documentação

Emulação do sintetizador Casio CZ-101 (1984) como aplicativo e plugin para macOS, construída sobre o MAME 0.289 embarcado do projeto Vintage Emulator Studio. Uso pessoal, não comercial.

Período: setembro de 2026. Plataforma: macOS em Apple Silicon (arm64).

## Visão geral

O objetivo foi ter **uma unidade por instrumento** em vez de um emulador com dezenas de máquinas: um programa que só roda o CZ-101, recebe MIDI, toca pela placa de som e serve de base para melhorar a interface e a fidelidade sonora.

O trabalho resultou em dois projetos irmãos:

| Projeto | Pasta | Papel |
|---|---|---|
| CZ-101 Studio | `/Volumes/STORAGE/GITS/CZ101Studio` | Versão de desenvolvimento: Standalone, lê as ROMs de uma pasta, painel recarregável sem recompilar. |
| CZ-101 Studio Personal | `/Volumes/STORAGE/GITS/CZ101Studio-Personal` | Versão para tocar: ROMs, painel e scripts embutidos no binário; Standalone, AU e VST3; librarian de timbres por SysEx; 13 cartuchos RA-3 com 208 timbres. |

A emulação do instrumento é inteiramente do MAME: o firmware original da Casio roda no processador NEC uPD7810 emulado, o som sai do chip Casio uPD933 emulado e o display é o controlador Hitachi HD44780 emulado. Os projetos fazem a ponte entre o MAME, o áudio/MIDI do macOS e a interface.

## Arquitetura

```
 MIDI (interface, DAW ou teclado do painel)
        │
        ▼
 CZ101Processor (JUCE) ── filtra clock/active sensing, pausa durante o librarian
        │ sendMidiBytes
        ▼
 EmbeddedEmulatorEngine (thread do MAME)
   ├─ driver cz101: uPD7810 + firmware Casio, uPD933 (som), HD44780 (LCD), RA-3
   ├─ porta MIDI IN  ◄── fila de bytes
   ├─ porta MIDI OUT ──► fila de bytes ──► MidiByteAssembler (host) ou librarian
   ├─ áudio (blocos de 20 ms) ──► fila ──► AudioJitterBuffer ──► placa / DAW
   ├─ vídeo (layout renderizado) ──► MameDisplay (painel clicável)
   └─ NVRAM (memória interna) ──► arquivo, ao sair / após escrita / a cada 30 s
```

Pontos centrais:

- **O MAME não é recompilado.** Os projetos linkam as bibliotecas estáticas e o objeto do driver (`cz101.o`) já compilados na árvore do VES. A lista de drivers do motor foi reduzida a `cz101`.
- **Motor embarcado.** O `EmbeddedEmulatorEngine` do VES foi copiado para cada projeto e estendido: saída MIDI, gravação de NVRAM a pedido e opção de salvar a NVRAM.
- **Build.** CMake + Ninja + JUCE 8.0.13. O código que inclui cabeçalhos do MAME é compilado numa biblioteca de objetos própria com exatamente os mesmos defines da `libemu.a`.

## Linha do tempo do trabalho

### 1. Primeira unidade: CZ-101 Studio

- Projeto JUCE novo com o motor do VES, lista de drivers só com `cz101` e NVRAM persistente.
- Link direto contra as bibliotecas do MAME já compiladas (sem rebuild do MAME).
- Primeira janela com o layout interno do MAME, luz de MIDI, VU e teclado na tela (depois removido a pedido).

### 2. Ruído de buffer

- Com buffer de 512 amostras o som picotava; com 64 ficava limpo. O mesmo sintoma foi relatado por usuários do VES no fórum KVR Audio.
- Causa medida: o MAME entrega áudio em blocos de 50 Hz (882 quadros a 44,1 kHz a cada 20 ms), e a fila do motor não tinha pré-carga. Um callback de 512 quadros frequentemente encontrava a fila com menos dados e tocava silêncio.
- Solução: `AudioJitterBuffer` (detalhes na seção de soluções). Resultado medido com buffer de 512: de 4 falhas em 85 s para 0 falhas em 90 s, latência de 24 a 40 ms e zero reamostragem.

### 3. Permissões e pastas do macOS

- O macOS pedia acesso à pasta Documentos a cada build (a assinatura ad-hoc muda) e o MAME ficava bloqueado lendo a ROM até a resposta.
- O `userApplicationDataDirectory` do JUCE aponta para `~/Library` no macOS, não para `Application Support`.
- Solução: dados em `~/Library/Application Support/CZ101Studio`, ROMs fora de Documentos e o caminho de ROMs deixou de ser restaurado do estado salvo pelo JUCE.

### 4. Painel

- Desenho vetorial do painel feito pelo usuário no Google Stitch (SVG de 1600 × 500).
- `tools/make_artwork.py` renderiza o SVG com o Google Chrome em modo headless (3200 × 1000) e gera um `default.lay` do MAME que coloca por cima 51 botões clicáveis, 49 teclas, 35 LEDs e a tela LCD emulada.
- Ajustes: sobreposições de texto corrigidas, ícones de forma de onda redesenhados e centralizados, rótulos abaixo da linha azul, palavras UP/DOWN ao lado da roda de pitch removidas (ficaram só as setas) e janela com proporção fixa do painel.

### 5. Versão Personal

- ROMs (`cz101.zip`, `hd44780.zip`), painel e scripts Lua do MAME embutidos com `juce_add_binary_data` e gravados em `~/Library/Application Support/CZ101StudioPersonal/runtime` na primeira execução.
- Formatos Standalone, AU e VST3; nome e códigos próprios (`Wjst` / `Cz1p`).
- Trava de instância: o MAME não é reentrante, então só uma instância toca por processo; as demais aguardam e assumem quando a primeira fecha.
- Instalação: AU copiado para `~/Library/Audio/Plug-Ins/Components` (o macOS ignora AU por link simbólico); VST3 como link para o disco externo; assinatura ad-hoc de todos os bundles. O AU passou no `auval` da Apple.

### 6. SysEx, MIDI OUT e memória interna

- Saída MIDI do CZ-101 emulado ligada ao host (antes era descartada).
- Librarian interno com o handshake original da Casio, porque hosts MIDI descartam o SysEx sem F7 que o protocolo usa.
- Botões **Carregar .syx** e **Salvar .syx** com destino "Área de som" ou "INTERNAL 1–16".
- NVRAM gravada logo após uma carga em INTERNAL, a cada 30 s e ao sair.
- Ferramenta `cz101_sysex_test` validando tudo contra o firmware real.

### 7. Codec de timbres e conversão de fichas .PAT

- `tools/czsyx.py`: decodifica e codifica timbres; reproduz byte a byte os 16 presets de fábrica a partir dos valores de painel.
- Várias informações da documentação pública do formato estavam erradas e foram corrigidas com base no firmware (tabela adiante).
- `tools/pat2syx.py`: converteu 104 de 132 fichas de texto `.PAT` dos anos 1980/90 em `.syx`, com relatório.

### 8. Cartuchos RA-3

- O CZ-101 aceita o cartucho de RAM Casio RA-3, com 16 timbres (CARTRIDGE 1–16). O MAME o emula como um arquivo de 4 KB.
- 13 cartuchos montados a partir das pastas de timbres (208 timbres), gravados pelo próprio firmware do CZ-101.
- Seletor **Cartucho** no topo da janela: troca o cartucho com o instrumento ligado; o botão CARTRIDGE do painel toca o cartucho escolhido.
- **Carregar .syx** ganhou as posições CARTRIDGE 1–16, com o nome do timbre de cada posição.

## Bugs e limitações corrigidos

### Em relação ao CZ-101 rodando no Vintage Emulator Studio

| # | Problema no VES | Evidência no código do VES | Correção no CZ-101 Studio | Verificação |
|---|---|---|---|---|
| 1 | Picotes/ruído com buffers grandes (512+) | Fila de áudio sem pré-carga; MAME entrega blocos de 20 ms | `AudioJitterBuffer` com pré-carga, margem adaptativa e correção de relógio | 0 falhas em 90 s com buffer 512 (antes 4 em 85 s) |
| 2 | Timbres por SysEx não chegam ao sintetizador | `isSupportedMidiForPrototype` só aceita Note, CC, Program Change e Pitch Bend | Todo MIDI é repassado ao MAME, exceto clock e active sensing | Timbre enviado e lido de volta idêntico |
| 3 | O sintetizador não consegue responder por MIDI | Porta de saída MIDI do motor com `write()` vazio | Fila de saída real, entregue ao host ou ao librarian | Dump dos 16 presets via MIDI OUT |
| 4 | Memória interna perdida a cada reinício | MAME com `-nonvram_save` e pasta de NVRAM temporária apagada ao iniciar | NVRAM persistente, gravada ao sair, após escrita interna e a cada 30 s | Timbre em INTERNAL 1 preservado após reiniciar o motor |
| 5 | Espera fixa de 10 s para liberar áudio e MIDI | `getBootElapsedMs() < 10000` | Liberação 2 s após o primeiro quadro de vídeo | Log: "pronto" 2 a 3 s após iniciar |

Também evitado na versão Personal: a partida travada quando falta a ROM de dispositivo `hd44780` ou quando o macOS bloqueia a leitura da pasta das ROMs, porque as ROMs vêm embutidas.

### Em relação ao MAME 0.289 original

- **Nenhum bug do driver do CZ-101 foi corrigido.** O driver funciona bem no MAME; o que mudou foi a integração.
- **NVRAM protegida contra encerramento anormal.** O MAME só grava a NVRAM ao sair (`running_machine::nvram_save()` é privado). O motor passou a gravá-la durante a execução, com uma cópia do mesmo procedimento usando apenas a API pública das NVRAMs.
- **Painel.** O layout interno do CZ-101 no MAME é esquemático (retângulos e texto); foi substituído por uma arte completa com as mesmas ligações de entrada.

### Problemas de plataforma resolvidos

| Problema | Causa | Solução |
|---|---|---|
| Aviso de acesso a Documentos travando o boot | ROM lida de pasta protegida; assinatura ad-hoc muda a cada build | ROMs em Application Support / embutidas |
| Dados gravados em `~/Library/CZ101Studio` | `userApplicationDataDirectory` do JUCE é `~/Library` no macOS | Caminho explícito para `Application Support` |
| AU não aparece para os hosts | O registro de Audio Units ignora `.component` instalado por link simbólico | AU copiado; VST3 continua como link |
| AU recusado pelo registro | Só o binário assinado pelo linker, recursos não selados | `codesign --force --deep --sign -` nos bundles |
| Librarians externos não funcionam | JUCE entrega SysEx sem F7 por `handlePartialSysexMessage`, que o host não repassa | Librarian dentro do plugin |

## Soluções técnicas

### AudioJitterBuffer

Fica entre a fila de áudio do MAME e o callback da placa/DAW (`Source/AudioJitterBuffer.h`).

- Só começa a tocar quando há na fila um bloco do MAME + um bloco do dispositivo + uma margem de segurança.
- Se faltar áudio, deixa um bloco em silêncio, aumenta a margem (até 80 ms) e volta a acumular.
- A cada 4 s observa o nível mínimo da fila; só se ele estiver claramente fora do alvo ajusta a velocidade de reprodução em ±500 ppm (0,9 cent) com interpolação Hermite. No restante do tempo copia as amostras 1:1.
- Os relógios medidos do MAME e da placa coincidem na média, então na prática o ajuste fica em 0 ppm.

### Painel (artwork do MAME)

- `Resources/artwork-src/cz101-panel.svg`: desenho do usuário, com os textos fixos do LCD e LEDs pintados removidos.
- `tools/make_artwork.py`: renderiza o SVG e escreve `Resources/artwork/cz101/default.lay`, com as coordenadas de cada controle ligadas às entradas do driver (`kc9`–`kc15`, teclas `kc0`–`kc8`) e aos LEDs (`led_*`, `led_env*`, `led_tone*`).
- No projeto de desenvolvimento a arte é lida da pasta do projeto: editar o SVG, rodar o script e clicar em **Reiniciar**.

### Librarian de timbres (SysEx)

Protocolo da Casio (n = canal MIDI − 1; `pp` = programa: 60 área de som, 20–2F INTERNAL 1–16):

```
Pedido de envio (CZ → computador)
  computador: F0 44 00 00 7n 10 pp     CZ: F0 44 00 00 7n 30
  computador: 7n 31                     CZ: <256 nibbles> F7
  computador: F7

Pedido de recebimento (computador → CZ)
  computador: F0 44 00 00 7n 20 pp     CZ: F0 44 00 00 7n 30
  computador: <256 nibbles> F7          CZ: F7
```

- `Source/CzSysex.h` implementa o protocolo sem dependência do JUCE.
- Durante uma transferência o processor pausa o MIDI do host e desvia a saída do CZ para o librarian (`AbstractFifo`), rodando numa thread própria.
- O `.syx` padrão de 264 bytes enviado inteiro de uma vez também é aceito pelo firmware (resposta `F0 44 00 00 70 30 F7`).

### Correções à documentação pública do formato de timbre

Referência usada: página "Casio CZ MIDI/SYSEX" em youngmonkey.ca. Conferido contra os 16 presets lidos do firmware e 100 arquivos `.syx` reais.

| Campo | Documentação | Comportamento real |
|---|---|---|
| Vibrato delay/rate/depth (3 bytes) | valor, byte alto, byte baixo | valor, byte **baixo**, byte **alto** |
| Vibrato delay 87 | `67 01 7F` | `57 01 7F` (erro de digitação) |
| Nível do DCA | linear, `127 × nível / 99` | `nível + 28` (0 continua 0) |
| Bit "nível descendo" (0x80 no rate) | quando o nível desce | também quando o nível alvo do passo é 0 |
| Detune fine | 30 → `1F` | `fine + fine ÷ 15` (30 → `20`) |
| Vibrato depth 0–31 | `v, 00, v+1` | confirmado pelo firmware; alguns editores antigos gravavam `v, v, 00` |

Layout confirmado da forma de onda (2 bytes): byte alto `[1ª forma 3][2ª forma 3][2ª ativa][0]`, byte baixo `[seletor 6–8: 2][modulação 3][000]`.

### Conversão das fichas .PAT

- 132 fichas de texto digitadas à mão por diversos autores, em pelo menos oito layouts.
- O conversor reconhece cabeçalhos em linha, tabela ASCII, chave/valor e blocos DCO/DCW/DCA empilhados ou lado a lado, com envelopes em "99/85/s", "rate = 99, level = 21 (sus)", colunas ou linhas horizontais.
- Cada ficha é validada (faixas 0–99, até 8 passos, sustain/end) e codificada pelo codec verificado.
- Resultado: 104 convertidas em `CZPATCHES/czpats/SYX convertidos`, com `RELATORIO.md`; 28 de layout livre ficaram para conversão manual. Os originais não foram alterados.

### Cartuchos RA-3

- **Gravação pelo firmware.** `tools/cz101_cart_builder` liga o CZ-101 emulado com um cartucho vazio, grava cada timbre por SysEx em CARTRIDGE n (programa `40`–`4F`) e confere lendo de volta. Ao desligar, o MAME grava a RAM do cartucho no arquivo. Depois religa com o arquivo e lê as 16 posições de novo. O formato interno do cartucho é, portanto, exatamente o que um CZ-101 grava.
- **Montagem do conjunto.** `tools/make_cartridges.py` separa as pastas, chama o gerador para cada cartucho e escreve, para cada um, a lista posição → nome (o CZ-101 não guarda nomes de timbre) e o `INDICE.md`.
- **No app.** Os cartuchos vão embutidos e são extraídos para `runtime/cartridges` só se ainda não existirem, para nunca apagar timbres gravados neles. O cartucho escolhido fica em `settings.xml`, é encaixado na partida (`-cart`) e trocado em funcionamento pela troca de mídia do motor.
- **Persistência.** Trocar de cartucho grava o anterior no arquivo (o MAME retira a mídia antes de carregar outra). Depois de carregar um `.syx` num CARTRIDGE, o app reinsere o mesmo cartucho para gravar o arquivo na hora. Um WRITE feito no painel para o cartucho é gravado ao trocar de cartucho ou fechar o app.

| Cartucho | Timbres | Origem |
|---|---|---|
| 01 CZ Pack 1 | 16 | Coffeeshopped CZ Pack 1 (16 primeiros em ordem alfabética) |
| 02 RelaCZ | 16 | CZounds RelaCZ |
| 03–06 Italo DisCZo 1–4 | 64 | CZounds Italo DisCZo |
| 07–12 CZ Pats 1–6 | 96 | Fichas `.PAT` convertidas |
| 13 CZ Pats 7 | 12 | 8 fichas `.PAT` restantes + Stiff Bass, Toad Bass, Wist Pad e Yay Afterhit do CZ Pack 1 |

## Estrutura de pastas (versão Personal)

| Caminho | Conteúdo |
|---|---|
| `CMakeLists.txt` | Plugin JUCE (Standalone/AU/VST3), dados embutidos, biblioteca `cz101_mame`, ferramenta de teste |
| `build.sh` | Build, assinatura ad-hoc e instalação dos plugins |
| `Source/PluginProcessor.*` | Motor, MIDI, áudio, librarian, NVRAM, trava de instância |
| `Source/PluginEditor.*` | Painel clicável, VU, luz de MIDI, menus de timbre, proporção da janela |
| `Source/AudioJitterBuffer.h` | Buffer de áudio entre MAME e dispositivo |
| `Source/CzSysex.h` | Protocolo SysEx da Casio |
| `Source/MidiByteAssembler.h` | Converte bytes do MIDI OUT em mensagens |
| `Source/FrontendStubs.cpp` | Stubs do frontend do MAME |
| `Engine/EmbeddedEmulatorEngine.*` | Motor do VES adaptado |
| `Embedded/roms/` | `cz101.zip`, `hd44780.zip` |
| `Resources/artwork-src/`, `Resources/artwork/` | SVG do painel e arte gerada |
| `Resources/plugins/` | `boot.lua` e plugin `layout` do MAME |
| `tools/make_artwork.py` | Gera a arte do painel |
| `tools/czsyx.py`, `tools/czsyx_tables.py` | Codec de timbres |
| `tools/pat2syx.py` | Conversor de fichas `.PAT` |
| `tools/cz101_sysex_test.cpp` | Teste do protocolo contra o firmware |
| `Embedded/cartridges/` | 13 cartuchos RA-3 (`.bin`), listas de timbres (`.txt`) e `INDICE.md` |
| `tools/cz101_cart_builder.cpp` | Grava um cartucho pelo firmware e verifica |
| `tools/make_cartridges.py` | Monta o conjunto de cartuchos a partir das pastas de timbres |
| `tools/build_docs.py` | Gera a página HTML bilíngue desta documentação |

## Como compilar e usar

```sh
cd /Volumes/STORAGE/GITS/CZ101Studio-Personal
./build.sh                                   # Standalone + AU + VST3, assina e instala
cmake --build build --target cz101_sysex_test  # ferramenta de teste (opcional)

python3 tools/make_artwork.py                # após editar o SVG do painel (depois ./build.sh)
python3 tools/pat2syx.py PASTA_PAT PASTA_SAIDA
python3 tools/czsyx.py dump TIMBRE.syx

cmake --build build --target cz101_cart_builder
python3 tools/make_cartridges.py             # recria os 13 cartuchos (depois ./build.sh)
```

Dependências: árvore do VES com o MAME 0.289 já compilado (`SUBTARGET=vesembedded`), JUCE 8.0.13, SDL3 estático do build do VES, Xcode (clang), CMake, Ninja e Google Chrome (só para gerar a arte).

Dados em tempo de execução (Personal): `~/Library/Application Support/CZ101StudioPersonal/` (`runtime/`, `nvram/`); log em `~/Library/Logs/CZ101StudioPersonal.log`.

## Limitações conhecidas

- POWER, pitch bend e volume do painel são apenas desenho (pitch bend funciona por MIDI).
- Uma instância do CZ-101 por processo (limitação do MAME).
- VST3: a entrega de SysEx depende do DAW (o Ableton Live não repassa SysEx a plugins). O librarian interno não depende disso.
- 28 fichas `.PAT` de layout livre não convertidas automaticamente.
- O MAME marca a ROM de fonte do HD44780 como dump imperfeito; é apenas um aviso.
- O MAME entrega áudio em blocos de 20 ms, o que define a latência mínima (≈ 20–40 ms).

## Créditos

### Emulação e código de terceiros

| Componente | Autoria | Licença | Uso neste projeto |
|---|---|---|---|
| MAME 0.289 | MAMEdev e colaboradores | GPL-2.0-or-later (projeto); arquivos com licenças próprias | Toda a emulação do instrumento |
| Driver Casio CZ-101 (`casio/cz101.cpp`) | Dirk Best, Devin Acker | BSD-3-Clause | Máquina emulada |
| Casio uPD933 (`sound/upd933.cpp`) | Devin Acker | BSD-3-Clause | Chip de som |
| Cartucho Casio RA-3 (`casio/ra3.cpp`) | Devin Acker | BSD-3-Clause | Dispositivo do driver |
| Hitachi HD44780 (`video/hd44780.cpp`) | Sandro Ronco | BSD-3-Clause | Display LCD |
| NEC uPD7810 (`cpu/upd7810`) | Juergen Buchmueller | BSD-3-Clause | CPU que roda o firmware |
| Portas e imagens MIDI (`bus/midi`, `imagedev/midiin`) | R. Belmont, Aaron Giles | BSD-3-Clause | MIDI IN/OUT |
| Gerenciador de som (`emu/sound.cpp`) | O. Galibert, Aaron Giles | BSD-3-Clause | Saída de áudio |
| Layout interno `cz101.lay` | colaboradores do MAME | CC0-1.0 | Mapa de entradas e LEDs usado como referência |
| `boot.lua` | Miodrag Milanovic | BSD-3-Clause | Plugins Lua do MAME |
| Plugin `layout` (`init.lua`) | Carl | BSD-3-Clause | Scripts de layout |
| Vintage Emulator Studio | Autodafe (github.com/antoniograzioli/VintageEmulatorStudio) | App: AGPL-3.0-only; integração MAME: BSD-3-Clause | Motor embarcado, patches do MAME, build do MAME e do SDL3 |
| JUCE 8.0.13 | Raw Material Software (JUCE) | AGPLv3 ou licença comercial JUCE | Plugin, áudio, MIDI, interface |
| SDL 3 | Sam Lantinga e colaboradores | zlib | Dependência das bibliotecas OSD do MAME |

### Firmware, documentação e timbres

| Item | Autoria | Uso |
|---|---|---|
| Firmware do CZ-101 (ROMs) | Casio Computer Co., Ltd. | Executado pelo emulador; uso pessoal, não redistribuir |
| ROM de caracteres do HD44780 | Hitachi (dump do acervo do MAME) | Fonte do LCD |
| "Casio CZ MIDI/SYSEX" | Texto publicado em youngmonkey.ca (autor original não identificado na página) | Base do protocolo e do formato de timbre |
| Fichas `.PAT` | Miles Bader, Paul Boutin, Michael Schaeffer, Tom Benson, Wayne Wylupski, Truls Ellefsen, Robert Lansdale, Martin Sant, John Pritchard, J. Peter Alfke, Glenn Bruns, Brent Wilson, Alan Bland, revista Electronics & Music Maker (set. 1986) e autores identificados só por e-mail | Timbres convertidos para `.syx` |
| CZ Pack 1 | Coffeeshopped | Timbres `.syx` de teste |
| RelaCZ, Italo DisCZo | CZounds | Timbres `.syx` usados na validação do codec |

### Ferramentas

- Google Stitch: ferramenta usada pelo usuário para desenhar o painel.
- Google Chrome (headless): renderização do SVG do painel.
- Apple Xcode / clang, `codesign` e `auval`: compilação, assinatura e validação do AU.
- CMake e Ninja: sistema de build.
- Python e a biblioteca Python-Markdown: scripts de arte, codec, conversor e geração desta documentação em HTML.
- Homebrew: instalação de ferramentas de linha de comando.
- Fórum KVR Audio: relatos de usuários sobre o ruído de buffer no VES.

### Autoria deste projeto

- Walter Junior.

## Aviso legal

- Projeto de uso pessoal e sem fins comerciais.
- As ROMs do CZ-101 são propriedade da Casio. A versão Personal as embute no binário e **não deve ser distribuída**.
- Distribuir o código exige cumprir as licenças acima (GPL-2.0-or-later do MAME, AGPL-3.0 do VES/JUCE e as demais).
- Casio, CZ-101 e demais marcas pertencem aos seus donos; este projeto não é afiliado nem endossado por eles.
