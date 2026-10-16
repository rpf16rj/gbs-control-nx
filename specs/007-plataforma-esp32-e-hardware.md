# SPEC-007: Plataforma — ESP32 dual-core, variantes modernas e hardware adjacente

- **Tipo**: `[SW]` + documentação `[HW]`
- **Impacto**: Médio-Alto — WiFi estável é a missão do fork; variantes C3/S3 são os ESP baratos de hoje
- **Esforço**: Alto

## Contexto

- O autor declara plano de isolar WiFi/WebSocket num **core dedicado do ESP32**
  (ainda não feito). No ESP8266, `webSocket.loop()` nem é chamado
  (`WiFiManager.cpp` só o faz em `#ifdef ESP32`) — keepalive depende de broadcast.
- Fork issue #12: usuário já compilou para **ESP32-C3 supermini** com mudanças
  pontuais; README exclui S3/C3/C6/H2/S2 explicitamente — são os módulos mais
  baratos/disponíveis hoje.
- Fork issue #16 (+ upstream #657/#595): clockgen Si5351 com SDA/SCL invertidos
  em revisões antigas do **McBazel ODV GBS-C** — unidade pré-montada mais popular.
  Swap de pinos I2C por software funciona ~90% mas com fuzziness/tear sob FTL.
- Mid-res arcade **24 kHz** (Sega Model 1/2, Chihiro, PC 384p) quebra no
  downscale (upstream #452/#285).

## Requisitos

1. **ESP32 multicore**: migrar WiFi stack + WebSocket + AsyncWebServer para
   tarefa pinned no core 0 (FreeRTOS), vídeo/I2C no core 1; fila thread-safe de
   comandos (substitui os `String` globais com race — auditoria item 6).
   Manter ESP8266 single-core funcional via `#ifdef`.
2. **Variantes ESP32**: env `gbsc_esp32c3` (e avaliar S3) no platformio.ini —
   validar pinout (C3 tem menos GPIOs), ADC/timers usados, I2C a 400–700 kHz.
   Documentar limitações (C3: sem classic BT, menos pinos; ainda ok p/ I2C+WS).
3. **ODV clockgen workaround** (#16): detecção automática de Si5351 nos dois
   pares de pinos I2C + opção `CLOCKGEN_I2C_SWAP`; documentar quando hardmod é
   necessário (fio longo = instabilidade, upstream #332/#657).
4. **I2C robusto** (pré-requisito dos itens acima): verificar retorno de
   `endTransmission()`, validar `requestFrom`, não usar buffer não inicializado
   (`src/hardware/tw.h:66-134`); unificar cache de segmento
   (`GBSRegister.cpp:7-26` vs `curSeg` em `tw.h`).
5. **Preset mid-res 24kHz**: modo dedicado com ajuste vertical funcional no
   caminho de downscale (upstream #452/#285).
6. Documentar mods consagrados no PINOUT.md: Si5351, caps SOG C33/C35 1nF,
   terminação 100Ω, LM1881, fix R26 (brilho YPbPr), AIO board — hoje dispersos
   na wiki upstream.

## Critérios de aceite

- ESP32: WebUI responsiva durante transições de modo pesadas (spam de comandos
  não derruba WS — regressão da v1.2.2 mantida).
- Build `gbsc_esp32c3` compila e passa smoke test em hardware do reporter da #12.
- ODV antigo: Si5351 detectado e FTL estável, ou mensagem clara no log indicando
  revisão problemática.
- Zero leituras I2C com buffer inválido (instrumentar e contar erros em log debug).

## Referências

- fork issues #12, #16 · upstream #657, #595, #452, #285, #332
- https://ramapcsx2.github.io/gbs-control/Wiki/Hardware-Mod-Library.html
- `src/wifi/WiFiManager.cpp`, `src/hardware/tw.h`, `platformio.ini`
