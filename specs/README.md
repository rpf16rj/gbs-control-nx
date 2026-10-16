# Specs de Melhorias — gbs-control-nx

Pesquisa produzida por análise do código (fork vs upstream `ramapcsx2/gbs-control`),
benchmarking de scalers retro (OSSC/OSSC Pro, RetroTINK-2X/5X/4K) e levantamento de
issues/comunidade (GitHub upstream, shmups t=52172, reviews).

Cada spec segue o formato: **Contexto → Requisitos → Abordagem técnica → Critérios de aceite → Referências**.

Legenda: `[SW]` = somente software · `[HW]` = requer/modifica hardware ·
Impacto/Esforço: Alto / Médio / Baixo.

## Roadmap priorizado

### P0 — Quick wins de estabilidade e correção (dias, alto retorno)

Status: ✅ implementado · ⚠️ parcial · ⬜ pendente (builds `gbsc`/`gbsc_esp32`/`gbsc_esp32c3` OK; nada testado em hardware)

| Item | Status | Origem | Notas |
|---|---|---|---|
| Divisão por zero no callback OTA (`progress/(total/100)`) | ✅ | auditoria `WebServer.cpp`, `OTAUpdate.cpp` | % agora calculado com guarda `total>=100` |
| Endpoints destrutivos sem auth; credenciais Wi-Fi vazáveis via `/filesystem/download` | ⚠️ | `WebServer.cpp` | auth opcional via `/web_auth.txt` (Basic) + path traversal bloqueado; bloqueio de `wifi*`/`preferences` quando sem auth |
| OTA sem senha + `client.setInsecure()` (TLS off) | ⚠️ | `OTAUpdate.cpp` | ArduinoOTA ganha senha quando `/web_auth.txt` existe; `setInsecure()` permanece (limitação de heap para TLS) |
| `regRead` lê buffer não inicializado se I2C falhar; `rawWrite` ignora `endTransmission()` | ✅ | `tw.h` | zero-fill + checagem de retorno + contador de erros |
| Bug `presetSlot` char cru no frame WS quebra slots ≥'H' e zera toggles | ✅ | upstream #323/#422 | sanitizado para ASCII imprimível |
| PalForce60 ignorado quando Si5351 presente | ✅ | upstream #237/#250 | sync de clockgen pulado quando PalForce60 ativo |
| Clockgen regredido no upstream ("clock frequency uninitialized" loop) | ⚠️ | upstream #595 | guarda básica; revisar em hardware |
| Race condition: handlers AsyncWebServer mutam `String` globais e `uopt`/`rto` sem fila | ✅ | `CommandQueue.h` | filas bounded para comandos seriais/usuário; provável fix do issue #4 |
| Cache de segmento I2C duplicado (`lastSegment` × `curSeg`) | ⚠️ | `GBSRegister.cpp`, `tw.h` | revisado: convergem por design (F0 lazy); mantido, documentado |
| Fix GUI: botões de resolução não respondem (v1.2.3+) | ⚠️ | fork issue #4 | fila de comandos + WS loop no ESP8266 devem resolver; confirmar na UI |
| Doc de build Windows (issue #18) + versão/commit visível | ⚠️ | fork #18, upstream #574 | `FirmwareVersion.cpp` + log no boot; falta exibir na Web UI |

Implementado fora da tabela: `webSocket.loop()` no ESP8266; `panicAndDisable` não trava mais (watchdog-friendly); typo `scanlineStrength`; merge do fix Neo Geo do fork tceoo1 (PLLAD_ICP=5, POST_COAST=9, sticky gate, filtro 6); latch PLLAD após presets; env `gbsc_esp32c3` compilando (experimental).

### Segunda leva (parcialmente implementada)

| Spec | Item implementado | Status |
|---|---|---|
| 008 | `/api/v1/status` enriquecido (telemetria cacheada: htotal/vtotal/PLL/phase/SOG/Hz) + `/api/v1/cmd` + `/api/v1/ucmd` | ✅ |
| 008 | Painel "Status" no WebUI (seção System) + botões Auto Phase e TBC | ✅ |
| 002 | Auto-phase exposto: comando `uc '2'` + botão WebUI (reusa `optimizePhaseSP`) | ✅ parcial — métrica é estabilidade de HTOTAL, não nitidez real |
| 002/008 | IF auto offset (`uopt->ifAutoOffset`, pref #22) persistido | ✅ |
| 003 | Mascaramento de transição: `freezeVideo()` no início de `applyPresets` | ⚠️ cobre o caminho capture→SDRAM; garbage de reconfiguração do VDS em si não é mascarado |
| 003 | "TBC mode" (`uopt->tbcMode`, pref #21, comando `uc '9'`): coast/ignore alargados para fontes instáveis | ✅ |
| 006 | Controle IR (NEC): `src/hardware/IRRemote.cpp` + envs `gbsc_ir`/`gbsc_esp32_ir` (`-DHAVE_IR_REMOTE`) | ✅ compila; mapeamento p/ remotes "Car MP3", códigos desconhecidos logados p/ aprendizado |
| 007 | ODV clockgen SDA/SCL invertido: detecção automática no ESP32 via `Wire1` em pinos cruzados | ⚠️ ESP32 apenas (ESP8266 tem 1 bus I2C — hardmod necessário) |
| 007 | `gbsc_esp32c3` compila (ver `Config.h` p/ pinos) | ✅ experimental |

### P1 — Qualidade de imagem e sync (features de scaler)

| Spec | Resumo | Impacto/Esforço |
|---|---|---|
| [001](specs/001-timing-presets-por-console.md) | Presets ótimos por console (samplerate/phase/backporch estilo OSSC/FirebrandX) + auto-HTotal promovido | Alto / Alto |
| [002](specs/002-auto-phase-e-adc.md) | Auto-phase (estilo RetroTINK-5X) + auto gain/offset de ADC persistente por preset | Alto / Médio |
| [003](specs/003-transicoes-e-sync-robusto.md) | Mascaramento de transição 240p↔480i, "TBC mode", merge fix Neo Geo (tceoo1), auto SOG level, fix 576i/50Hz | Alto / Alto |
| [004](specs/004-saida-cea861-aspect-integer.md) | Timings CEA-861 + snap automático 59.94/50, RGB range toggle, aspect 4:3/16:9, integer scaling + windowbox 960p-em-1080p | Alto / Alto |
| [005](specs/005-deinterlace-film-mode-scanlines.md) | Explorar film-mode 3:2/2:2 do TV5725, thresholds do MAD expostos, scanlines fora de 240p | Médio-Alto / Alto |

### P2 — Plataforma, UX e ecossistema

| Spec | Resumo | Impacto/Esforço |
|---|---|---|
| [006](specs/006-osd-tv-e-controle-fisico.md) | OSD na TV via STV9426 (estilo gbsc-pro) + IR remote (fork #17) + reduzir refresh I2C do OLED | Médio / Alto |
| [007](specs/007-plataforma-esp32-e-hardware.md) | WiFi/WS em core dedicado no ESP32, suporte ESP32-C3/S3 (fork #12), workaround clockgen ODV (#16), mid-res 24kHz, HScale>1023 | Médio / Alto |
| [008](specs/008-api-webui-ux.md) | API REST/JSON estável (demanda Home Assistant), import/export presets, tela INFO estilo OSSC, persistir dev options, 36+ slots | Médio / Médio |

### Fora de escopo / somente documentar

- Áudio (não há caminho de hardware; issue #20 é drift do conversor HDMI — mitigar com FTL estável)
- Composite/S-Video nativo (precisa decoder externo, ex.: módulo AV do gbsc-pro)
- Saída 480i/240p CRT-out (demanda real — GBS-C Pro CRT vende isso — mas "requer programação estranha do ASIC" segundo rama; pesquisa de registradores VDS)
- Rotação tate, lag <1 frame, HDMI nativo, shaders/masks complexos
- Mods de hardware consagrados para documentar no PINOUT/README: Si5351, caps SOG C33/C35 1nF, LM1881 sync stripper, terminação 100Ω, fix R26 (brilho YPbPr), GBS-C AIO board

## Reclamações da comunidade → onde são endereçadas

| Reclamação | Spec |
|---|---|
| Tearing / tear line | 003 (FTL já existe; melhorar lock), 004 |
| "Garbage" em troca 240p↔480i / NTSC↔PAL | 003 |
| Deinterlace 480i mediano; C128 não detectado | 005 |
| Sync loss em flash branco (SOG) | 003 |
| Jailbars / banding | 002 (rotina de mitigação automática) |
| Conversor VGA→HDMI sem imagem em 50Hz, sem EDID, washed out | 004 (resolve fork #10) |
| Aspect ratio errado em 1080p; sem integer scaling | 004 (resolve fork #7) |
| WiFi/GUI instável | P0 + 007 + 008 |
| RGBHV problemático | 003 |
| Neo Geo MVS/AES sync | 003 (merge tceoo1) |
| Falta OSD na TV / controle remoto | 006 (resolve fork #17) |
| Presets não portáveis / não persistem | 002 + 008 |
| ESP32 modernos não suportados | 007 (resolve fork #12) |
| 1080i/540p para HD CRTs; 1024x768; 1680x1050 | 004/007 |
| API instável p/ automação | 008 |
| Mid-res arcade 24kHz quebrado | 007 |
