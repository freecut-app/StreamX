# StreamX 1.0.0 — relatório de testes

Ambiente: Windows 11 25H2 (build 26200), Intel i5-10400F (12 threads), 32 GB RAM, NVIDIA RTX 3060 Ti (driver 32.0.16.1088), monitor ultrawide 2560×1080 @ 75 Hz, headset USB. Data: 15/10/2026.

Os testes automáticos rodam o StreamX de verdade, usando opções `--selftest-*` do próprio executável. Os arquivos gerados foram verificados com o **ffprobe** (codec, resolução, duração e faixas) e com a análise de brilho do ffmpeg (`signalstats`), para não aceitar vídeo preto.

## ✅ Testes executados e aprovados

| # | Teste | Como | Resultado |
|---|---|---|---|
| 1 | Captura de tela + gravação | `--selftest-record 5 arquivo.mp4` | H.264 NVENC 2560×1080 60 fps + AAC 48 kHz, 5,00 s, brilho médio 60–101 (imagem real) |
| 2 | Bug encontrado e corrigido | 1ª versão gravava **tela preta** (brilho 16): a fonte não tinha monitor selecionado | Agora escolhe o monitor principal da lista real do plugin |
| 3 | Sincronia áudio/vídeo | ffprobe: `start_time` das trilhas | Vídeo e áudio começam em 0,000 s; durações diferem < 35 ms (1 quadro AAC) |
| 4 | **Clip It** | `--selftest-clip 8`: buffer ligado, salva após 8 s | Clipe de 8,0 s salvo sem parar o buffer e movido para `Clipes\2026-10\` |
| 5 | **Transmissão RTMP** | servidor RTMP local (`ffmpeg -listen 1`) + `--selftest-stream` | Servidor recebeu 7,98 s, H.264 + AAC, ~6,2 Mbps, **0 de 460 quadros perdidos** |
| 6 | **Multifaixa** | `--selftest-tracks`: microfone só na faixa 2, PC só na faixa 3 | Arquivo com 1 vídeo + **3 faixas AAC** separadas |
| 7 | **Atalhos globais** | `tests\send-hotkey.ps1` envia Ctrl+Alt+R pelo Windows com o StreamX sem foco | 1º atalho iniciou e 2º parou a gravação; arquivo finalizado (código 0) |
| 8 | **StreamX Control** | `--selftest-control` + curl | Sem token → 401 · token errado → 401 · status → 200 · gravar/parar → OK · cena inexistente → 409 · página remota → 200 · escuta **só em 127.0.0.1** |
| 9 | Fontes e dispositivos | `--selftest-sources` | Lista real: 1 monitor, 5 janelas, 7 alvos de jogo, 2 entradas e 3 saídas de áudio, 8 apps com áudio; 10 tipos de fonte criados |
| 10 | Filtro de áudio | idem | Supressão de ruído (RNNoise) aplicada ao microfone |
| 11 | Persistência / reinício | salvar → limpar → recarregar a coleção | Cenas, 10 itens e o filtro restaurados iguais |
| 12 | **Assistente** | `--selftest-assistant` | Equipamento "alto": NVENC, 60 fps, qualidade 18; para transmissão limita a 1920×810 (o teste mostrou que antes sugeria 2560×1080, acima do limite das plataformas; corrigido) |
| 13 | Interface | `--ui-screenshots`: abre a janela e fotografa as 5 páginas | Todas renderizam; app fecha com código 0 e **0 objetos do libobs vazados** |
| 14 | Instalador | instalação silenciosa (`/S /D=`) | Código 0, exe, atalhos (área de trabalho + Menu Iniciar), registro em "Aplicativos instalados", runtime VC++ incluído (101 MB instalado) |
| 15 | App instalado | `--selftest-record` pelo exe instalado | Gravação válida |
| 16 | Desinstalação | `Uninstall.exe /S` | Remove arquivos, atalhos e registro; **mantém gravações e configurações** |
| 17 | Consumo | janela aberta com prévia (Gerenciador de Tarefas) | ~104 MB de RAM em repouso |

## ⚠️ Depende de validação manual

| Item | Motivo |
|---|---|
| **Captura de jogos** (game_capture) | Precisa de um jogo aberto. Os alvos são listados corretamente (7 janelas detectadas), mas a injeção em DirectX/Vulkan e o comportamento de anti-cheats (Vanguard, EAC) só podem ser vistos com o jogo rodando. |
| **Game Mode: detecção do jogo** | Precisa de um jogo dos presets (Fortnite, Minecraft, GTA V…) em primeiro plano. |
| **Webcam** | Não há webcam conectada neste PC (0 dispositivos DirectShow). |
| **Dispositivo desconectado** | Precisa desplugar fisicamente o headset ou o microfone durante uma gravação. O plugin WASAPI do libobs reconecta sozinho quando o dispositivo volta. |
| **Transmissão real (Twitch/YouTube)** | Testada só com um servidor RTMP local; precisa de uma chave de transmissão real. RTMPS usa o mesmo caminho (mbedTLS do libobs). |
| **AMD AMF / Intel Quick Sync** | Esta máquina só tem NVIDIA. O StreamX só mostra os codificadores que o libobs detectar. |
| **StreamX Control pela rede local** | Testado no próprio PC; o acesso pelo celular depende do firewall do Windows liberar a porta (padrão 4466). |
| **Editar no FreeCut** | O FreeCut 1.0.0 ainda não importa arquivos por argumento (veja `docs/FREECUT_INTEGRATION.md`). Hoje o botão abre o FreeCut e seleciona o arquivo no Explorer para você arrastar. |
| **Arrastar/redimensionar fontes na prévia** | Interação com o mouse: não coberta pelo teste automático. |
| **Medidor de GPU** | Usa os contadores do Windows (PDH); em algumas máquinas pode aparecer "—". |

## Como repetir

```powershell
$x = "rundir\bin\64bit\StreamX.exe"
& $x --selftest-record 5 C:\temp\teste.mp4
& $x --selftest-clip 8
& $x --selftest-tracks 4 C:\temp\faixas.mp4
& $x --selftest-sources C:\temp\cenas.json
& $x --selftest-assistant
# RTMP: ffmpeg -listen 1 -i rtmp://127.0.0.1:1935/live/teste -c copy rx.flv   (em outro terminal)
& $x --selftest-stream rtmp://127.0.0.1:1935/live/teste 8
```
