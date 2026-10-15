# StreamX — Arquitetura

Estudo do **OBS Studio 32.2.2** (`deps/obs-studio`, tag `32.2.2`, licença GPL v2+) e o desenho do StreamX feito a partir dele.

## 1. Como o OBS Studio está organizado

```
obs-studio/
├─ libobs/            núcleo em C: fontes, cenas, áudio, vídeo, encoders, outputs, services, hotkeys
├─ libobs-d3d11/      backend gráfico Direct3D 11 (Windows)
├─ libobs-winrt/      Windows Graphics Capture (WGC) usado pela captura de janela/monitor
├─ plugins/           tudo que captura, filtra, codifica e transmite é plugin (.dll em obs-plugins/64bit)
├─ frontend/          a interface Qt 6 (OBSApp, OBSBasic, widgets, wizards) — o "OBS" que o usuário vê
├─ deps/              bibliotecas internas (libcaption, w32-pthreads, json11…)
└─ shared/            código comum entre frontend e plugins
```

O ponto principal é a separação entre motor e interface. O `libobs` não sabe nada de Qt; o `frontend/` é só um cliente da API C do libobs. O StreamX segue esse modelo: **libobs e os plugins oficiais compilados sem modificação**, e uma interface nova.

### Ciclo de vida do libobs (como o frontend faz)

| Passo | Chamada | Onde no OBS |
|---|---|---|
| Inicializar | `obs_startup(locale, module_config_path, profiler)` | `frontend/OBSApp.cpp:1178` |
| Vídeo | `obs_reset_video(&obs_video_info)` (resolução base/saída, FPS, formato NV12, d3d11) | `frontend/widgets/OBSBasic.cpp:1521` |
| Áudio | `obs_reset_audio2(&obs_audio_info2)` (48 kHz, estéreo) | `OBSBasic.cpp:1702` |
| Plugins | `obs_load_all_modules2()` → `obs_post_load_modules()` | `OBSApp.cpp:2044` |
| Prévia | `obs_display_create(&gs_init_data{hwnd…})` + `obs_display_add_draw_callback` → `obs_render_main_texture()` | `frontend/widgets/OBSQTDisplay.cpp:157` |
| Saída de vídeo | `obs_set_output_source(0, scene_source)` (canal 0 = programa) | — |
| Encerrar | liberar outputs/encoders/fontes → `obs_shutdown()` | — |

## 2. Captura (plugins Windows)

| ID do tipo | Plugin | Técnica |
|---|---|---|
| `monitor_capture` | win-capture | DXGI Desktop Duplication ou WGC (Windows 10 1903+) |
| `window_capture` | win-capture | BitBlt ou WGC |
| `game_capture` | win-capture | **Injeção de hook** (`graphics-hook64.dll` via `inject-helper64.exe`) em D3D9/10/11/12, OpenGL e Vulkan, com cópia de textura compartilhada. Os offsets vêm de `get-graphics-offsets64.exe` |
| `dshow_input` | win-dshow | webcams e placas de captura (DirectShow) |
| `wasapi_input_capture` | win-wasapi | microfone |
| `wasapi_output_capture` | win-wasapi | áudio do sistema (loopback) |
| `wasapi_process_output_capture` | win-wasapi | **áudio de um aplicativo específico** (Windows 10 2004+). Permite separar jogo, Discord, música… |
| `image_source`, `color_source`, `text_gdiplus`, `ffmpeg_source` | image-source, obs-text, obs-ffmpeg | imagem, cor, texto, arquivo de mídia |

A captura de jogo precisa que os arquivos `data/obs-plugins/win-capture/*` (hooks, injetor e offsets) acompanhem o app. Antitrapaças podem bloquear a injeção; nesse caso, a alternativa é a captura de janela (WGC).

## 3. Cenas, fontes e filtros

- Tudo é `obs_source_t`: fontes de entrada, filtros, transições e a própria cena (`obs_scene_t` é uma fonte com uma lista de `obs_sceneitem_t`, cada item com posição, escala, rotação, recorte e visibilidade).
- **Filtros** são fontes anexadas com `obs_source_filter_add`. Vídeo: `color_filter`, `chroma_key_filter`, `crop_filter`, `sharpness_filter`, `scale_filter`, `mask_filter`… Áudio: `noise_suppress_filter` (RNNoise/Speex), `gain_filter`, `noise_gate_filter`, `compressor_filter`, `limiter_filter`, `expander_filter`.
- **Transições** (`obs-transitions`): `cut_transition`, `fade_transition`, `swipe_transition`, `slide_transition`, `wipe_transition`, `fade_to_color_transition`, `obs_stinger_transition`. A troca de cena é feita com `obs_transition_start(trans, OBS_TRANSITION_MODE_AUTO, ms, cena_nova)`, com a transição definida como fonte do canal 0.
- Configurações persistem como `obs_data_t` (JSON): `obs_save_source` / `obs_load_source`.

## 4. Pipeline de áudio e vídeo

```
fontes ──► cena ──► canal 0 ──► obs_video (GPU, d3d11) ──► textura NV12 ──┬► prévia (display)
                                                                         ├► encoder de vídeo (textura direto p/ NVENC/AMF/QSV)
fontes de áudio ──► mixer (6 faixas, bitmask por fonte) ──► encoders AAC ─┘
                                                 encoders ──► outputs (arquivo / RTMP / replay buffer)
```

- **Áudio:** cada fonte tem volume, balanço, monitoramento e `obs_source_set_audio_mixers(bitmask)`, que define em quais das 6 faixas ela entra. É isso que permite gravar **multifaixa** (ex.: faixa 1 = tudo, 2 = microfone, 3 = jogo, 4 = Discord).
- **Vídeo:** renderizado na GPU. Os encoders `*_tex` (ex.: `obs_nvenc_h264_tex`) recebem a textura sem cópia para a CPU, o que dá o menor impacto em FPS de jogo.

## 5. Codificação

| Encoder | ID | Observação |
|---|---|---|
| NVIDIA NVENC | `obs_nvenc_h264_tex`, `obs_nvenc_hevc_tex`, `obs_nvenc_av1_tex` | plugin `obs-nvenc`; detecção feita pelo `obs-nvenc-test.exe` |
| AMD AMF | `h264_texture_amf`, `h265_texture_amf`, `av1_texture_amf` | plugin `obs-ffmpeg` (`texture-amf.cpp`) |
| Intel Quick Sync | `obs_qsv11_v2`, `obs_qsv11_hevc`, `obs_qsv11_av1` | plugin `obs-qsv11` |
| CPU | `obs_x264` | plugin `obs-x264` |
| Áudio | `ffmpeg_aac`, `ffmpeg_opus` | plugin `obs-ffmpeg` |

O StreamX lista só os encoders que existem de fato (`obs_enum_encoder_types` filtrado por disponibilidade) e nunca mostra opções que o hardware não suporta.

## 6. Gravação, transmissão e replay buffer

| Função | Output | Detalhe |
|---|---|---|
| Gravação | `ffmpeg_muxer` (MKV/MP4/MOV/FLV via `obs-ffmpeg-mux.exe` em processo separado) ou `mp4_output` (MP4 nativo e híbrido, à prova de queda) | multifaixa: um encoder de áudio por faixa com `obs_output_set_audio_encoder(out, enc, idx)` |
| Transmissão | `rtmp_output` + service `rtmp_custom` (servidor + chave) ou `rtmp_common` (lista de serviços) | RTMPS suportado |
| Replay buffer | `replay_buffer` | mantém os últimos N segundos (`max_time_sec`, `max_size_mb`) na RAM; o procedimento `save()` grava em disco **sem parar o buffer** e `get_last_replay(out path)` devolve o arquivo (`obs-ffmpeg-mux.c:961`) |

Os encoders podem ser **compartilhados** entre gravação, stream e replay, então é só uma codificação para tudo e o custo na GPU não se multiplica.

## 7. Dispositivos e plugins

- As propriedades das fontes (`obs_source_properties`) listam dispositivos dinamicamente: monitores, janelas, webcams, endpoints WASAPI.
- Os dispositivos desconectados são tratados pelos próprios plugins: o WASAPI reconecta quando o dispositivo volta e o dshow tem `deactivate_when_not_showing`. O StreamX só exibe o estado.
- Os plugins são carregados de `obs-plugins/64bit/*.dll`, com dados em `data/obs-plugins/<nome>/`.

## 8. Desempenho

- `obs_get_active_fps`, `obs_get_average_frame_time_ns`, `video_output_get_skipped_frames`, `obs_get_lagged_frames` e `obs_output_get_frames_dropped` alimentam os indicadores.
- A prioridade do processo (`SetPriorityClass`) e a resolução de saída menor que a base (downscale na GPU) reduzem o impacto em jogos.

---

## 9. Desenho do StreamX

```
STREAMX/
├─ deps/obs-studio     libobs + plugins oficiais (compilados sem alteração, GPL)
├─ src/
│  ├─ core/            Engine (wrapper RAII do libobs), SceneManager, AudioMixer, Recorder,
│  │                   Streamer, ClipIt (replay), Profiles, Hotkeys, PerfMonitor, Assistant, GameMode
│  ├─ control/         StreamX Control: servidor HTTP local + token
│  ├─ integration/     FreeCutBridge (abre gravações no FreeCut)
│  └─ ui/              Qt 6 Widgets: MainWindow, PreviewWidget, painéis, tema escuro/azul elétrico
├─ installer/          NSIS
└─ tests/              testes automatizados (gravação real + ffprobe)
```

**Regras:**
- A UI nunca chama o libobs diretamente; tudo passa por `core/`.
- Todo botão chama uma função real. Se o recurso não estiver disponível no hardware, o botão não aparece ou fica desabilitado com o motivo.
- Licença: **GPL v2 ou posterior** (exigência por usar o libobs), com o código-fonte público.
