# SPEC-006: OSD na TV (STV9426) + controle físico/IR

- **Tipo**: `[HW+SW]` (OSD na TV exige chip STV9426 ou similar; IR exige receptor)
- **Impacto**: Médio — maior lacuna de UX vs clones comerciais (gbsc-pro já tem OSD on-TV + IR + 36 slots)
- **Esforço**: Alto

## Contexto

Fork issue #17 pede abrir/navegar menu OSD na TV via controle IR. O clone
RetroScaler gbsc-pro (fw derivada) já implementa: OSD na TV via STV9426 com 4
temas, IR remote, 36 slots de perfil, menu Developer no OSD com valores ao vivo
(HTotal, SDRAM clock, SOG phase). O menu OLED do nx (biblioteca PSHarold,
`src/menu/`) já é modular — `OLEDMenuManager`/`OSDManager` existem; falta o path
de render no vídeo de saída e o input IR.

Hoje o OLED redesenha framebuffer inteiro via I2C a cada 50 ms
(`OLED_MENU_REFRESH_INTERVAL_IN_MS`, `OLEDMenuManager.cpp:378-405`) competindo com
o barramento I2C do TV5725 — risco real de interferência com o scaler.

## Requisitos

1. **OSD on-TV**: integração com STV9426 (chip OSD i2c usado pelo gbsc-pro) ou
   overlay via framebuffer do próprio TV5725 (pesquisa: `src/menu/osd.h` existe —
   verificar o que já desenha). Menu mínimo: resolução, preset, scanlines,
   posição/scaling, dev values ao vivo.
2. **IR remote**: receptor IR em GPIO livre (ESP32 tem pinos sobrando); mapear
   teclas para os mesmos handlers do encoder/OLED; protocolo NEC mínimo.
3. **Encoder/OLED**: reduzir refresh para dirty-rect ou 100–150 ms; debounce de
   ISR mais curto (hoje 120–150 ms rotação / 500 ms push — `RotaryEncoder.cpp`);
   revisar `IRAM_ATTR` + `millis()` no ESP32.
4. `OLEDMenuManager::panicAndDisable` (`OLEDMenuManager.h:75-86`) usa `while(1)`
   sem watchdog feed — substituir por falha graceful + log.
5. Reaproveitar `HAVE_BUTTONS` morto (`Config.h:22`, código chamado não existe)
   ou remover de vez.

## Abordagem técnica

- Estudar `RetroScaler/gbsc-pro` e forks (20-Percent-Cooler, brisma) e
  `mpalpha/gbs-control` (branch odv-gbs-c-compat) — menus OSD/OLED portáveis.
- IR: biblioteca IRremoteESP8266 (madura nos dois alvos); input → mesma fila de
  comandos da SPEC-008 (elimina race de strings globais).
- OSD via TV5725: investigar `osd.h` e regs de OSD/window; fallback STV9426 i2c
  (mesmo barramento — cuidar contenção, item 3).

## Critérios de aceite

- Navegar preset/resolução/scanlines pela TV com controle IR, sem PC/celular.
- OLED não degrada I2C do scaler (medir: sync stability com menu aberto).
- Documentado no PINOUT.md: wiring STV9426/IR/encoder.

## Referências

- fork issue #17 · https://github.com/RetroScaler/gbsc-pro · https://github.com/mpalpha/gbs-control
- upstream PR #527 (menu OLED/OSD PSHarold, parcialmente mergeado)
- `src/menu/*`, `src/hardware/RotaryEncoder.cpp`
