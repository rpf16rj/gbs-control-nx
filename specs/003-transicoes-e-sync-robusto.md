# SPEC-003: Mascaramento de transição de modo + robustez de sync

- **Tipo**: `[SW]` apenas (itens de docs `[HW]` à parte)
- **Impacto**: Alto — endereça as 2 maiores reclamações: "garbage" em troca de modo e sync loss
- **Esforço**: Alto

## Contexto

- Trocar 240p↔480i/480p ou NTSC↔PAL reconfigura o chip inteiro e vaza "garbage"
  na tela (upstream #332). A GBS já é free-running (vantagem: sem blackout de
  re-sync como scalers burros), mas falta mascarar a saída durante a transição.
- Sync loss em telas brancas/strobe via SOG (upstream #447); Neo Geo MVS/AES
  (csync 264 linhas sem serration) falha — o fork `tceoo1/gbs-control` já tem fix
  (PLLAD_ICP + SP_POST_COAST + sync gate dedicado) com PR pendente upstream.
- Transições atuais são heurísticas com `newVideoModeCounter` 3–8 + delays
  (`SyncWatcher.cpp:576-670`) — plausível flicker; deveria ser FSM.
- RGBHV/bypass incompleto: TODOs do próprio autor em `GBSController.cpp` (modos
  14/15, HV sync, seleção SOG).

## Requisitos

1. **Output masking**: durante `applyPresets()`/reconfiguração, forçar saída black
   (mute VDS/OSD) e fazer fade-in após lock estável; alvo: zero "garbage" visível.
2. **FSM de transição de modo**: consolidar heurística dispersa de
   `SyncWatcher.cpp` numa máquina de estados explícita (detect → confirm → mask →
   apply → stabilize → unmask) sem `delay()` bloqueantes no loop.
3. **Merge do fix Neo Geo** do fork tceoo1 (avaliar compatibilidade com a
   refatoração nx).
4. **"TBC mode"** (perfil de fonte instável): alargar janelas PRE/POST_COAST,
   relaxar sync watcher — para VHS, RF-modulado, sinais off-spec (inspiração:
   TBC mode do RetroTINK-5X).
5. **Auto SOG level**: varrer `currentLevelSOG` até lock estável em telas claras
   (hoje é manual "SOG Level--").
6. **Detecção de interlace robusta** para fontes não-padrão (C128 80-col,
   Amiga lace) — heurística por contagem de linhas/field flag (upstream #558).
7. Investigar/fix **576i/50Hz SCART black screen** (fork issue #10 — provável
   interação preset PAL × conversor HDMI; ver SPEC-004 para a parte de timing).

## Abordagem técnica

- Masking: verificar registradores de blank/mute no `tv5725.h` (VDS display
  enable, OSD overlay black) ou forçar DAC output min durante transição.
- FSM nova em `src/video/SyncWatcher.cpp` — hoje a lógica tem duplicação
  reconhecida pelo autor ("note: this is all duplicated below. unify!",
  linhas 983/1100) — aproveitar para unificar.
- Remover os delays longos listados na auditoria (`SyncWatcher.cpp:494-512`,
  `gbs-control.ino:1721-1736`, `SyncWatcher.cpp:97-98` com `for(;;)`).

## Critérios de aceite

- Troca Sonic 2 (240p→480i em menus de 2 players) sem garbage visível.
- Neo Geo AES/MVS sincroniza sem hardware mod.
- Fonte PAL 576i produz imagem (fork #10) em VGA; via HDMI depende de SPEC-004.
- Zero watchdog/reboot durante transições rápidas repetidas.

## Referências

- upstream #332, #447, #558, #118/#216 · https://github.com/tceoo1/gbs-control
- nyanpasu64 blog (saga sync loss Wii) · RetroTINK-5X TBC mode
