# SPEC-002: Auto-phase + calibração de ADC persistente por preset

- **Tipo**: `[SW]` apenas
- **Impacto**: Alto — phase errado = imagem suave/borrada; green tint é queixa recorrente no shmups
- **Esforço**: Médio

## Contexto

RetroTINK-5X e OSSC Pro fazem **auto-phase**: varrem todas as fases de amostragem e
escolhem a de maior nitidez. Na GBS, `phaseADC`/`phaseSP` são manuais. Já a
calibração de ADC (auto gain/offset) existe no boot mas "IF auto offset" é dev
option **não persistente** — usuários perdem a correção de green tint a cada boot.

## Requisitos

1. **Auto-phase**: rotina que varre `phaseADC` (0–N steps) medindo energia de alta
   frequência/transições na entrada (via debug readout do ADC ou métrica de
   estabilidade de clamp) e escolhe o melhor. Trigger manual (botão WebUI) +
   opcional pós-deteção de modo.
2. **Auto gain/offset de ADC por preset**: rodar calibração e **persistir** os
   valores no slot de preset (LittleFS), recarregando junto do preset.
3. **Rotina de mitigação de banding/jailbars**: modo "low noise" que varre
   SDRAM clock ±N e HTotal ±N buscando mínimo de artefato (ou tabela pré-testada
   por revisão de board V4.0/V5.0). Upstream issues #522/#529.
4. Expor na WebUI: botões "Auto Phase", "Calibrate ADC", indicador do phase atual.

## Abordagem técnica

- ADC calibração já existe em `gbs-control.ino` setup → extrair para função
  reutilizável; salvar em `src/storage/SlotManager.cpp` junto do preset blob.
- Auto-phase: métrica via status registers do TV5725 (há readouts de
  diagnóstico/PLL lock) ou, na pior hipótese, otimização por heurística de
  vsync/hlock stability; investigar `GBSPhase.cpp` (`updateSpDynamic` linhas 54-192).
- Persistência: estender formato de slot cuidando do bug de serialização
  char-a-char (`PreferencesManager.cpp` assume 1 dígito; `scanlineStrength` é byte).

## Critérios de aceite

- Auto-phase converge em <2 s sem intervenção.
- Green tint corrigido permanece após reboot por preset.
- Não regredir estabilidade de sync durante a varredura (rodar só com imagem estável).

## Referências

- RetroTINK-5X auto phase · OSSC Pro auto phase
- upstream issues #522 (banding), #529 (SDRAM clock), #72 (filtros)
- `src/video/VideoInput.cpp`, `src/gbs/GBSPhase.cpp`, `src/storage/`
