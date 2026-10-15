# StreamX — licenças e componentes de terceiros

O StreamX é distribuído sob a **GNU General Public License v2 ou posterior** (arquivo `LICENSE.txt`), porque usa o libobs, que é GPL. Quem distribuir o StreamX deve oferecer o código-fonte correspondente.

| Componente | Uso no StreamX | Licença | Código-fonte |
|---|---|---|---|
| **libobs e plugins do OBS Studio 32.2.2** (obs.dll, libobs-d3d11, win-capture, win-wasapi, win-dshow, obs-ffmpeg, obs-nvenc, obs-x264, obs-outputs, obs-filters, obs-transitions, rtmp-services, image-source, obs-text, obs-qsv11…) | motor de captura, composição, codificação, gravação, transmissão e filtros. Compilado **sem modificações** a partir da tag `32.2.2` | GPL v2+ | https://github.com/obsproject/obs-studio/tree/32.2.2 |
| **Qt 6** (Core, Gui, Widgets, Network, Svg, Xml) | interface | LGPL v3 (ligação dinâmica; as DLLs podem ser substituídas) | https://download.qt.io/official_releases/qt/ · build das dependências do OBS: https://github.com/obsproject/obs-deps |
| **FFmpeg** (avcodec, avformat, avutil, avfilter, avdevice, swscale, swresample) | gravação (muxer), áudio AAC | GPL (build com x264) | https://ffmpeg.org · https://github.com/obsproject/obs-deps |
| **x264** | codificação por CPU | GPL v2+ | https://code.videolan.org/videolan/x264 |
| **libcurl, mbedTLS, librist, libsrt, jansson, RNNoise, Speex DSP, FreeType, zlib, LuaJIT (desativado)** | rede, RTMPS, supressão de ruído, texto | licenças MIT/BSD/Apache/zlib | https://github.com/obsproject/obs-deps |
| **Microsoft Visual C++ Runtime** (vcruntime140, msvcp140…) | runtime do C++ | Licença de redistribuição do Visual Studio | https://visualstudio.microsoft.com/license-terms/ |

Versões exatas usadas no instalador 1.0.0: **OBS Studio 32.2.2** (commit `ba2f32b`) e **obs-deps 2026-07-15** (`windows-deps-2026-07-15-x64` e `windows-deps-qt6-2026-07-15-x64`), baixados automaticamente pela build do OBS. Para recompilar tudo a partir do código-fonte, use `scripts/build-all.ps1` deste repositório.

Os arquivos de licença originais de cada componente acompanham o código-fonte nos links acima.

O StreamX é um projeto independente: não é afiliado ao OBS Project nem endossado por ele e não usa logotipos ou elementos gráficos do OBS. "OBS" e "OBS Studio" são marcas do OBS Project.
