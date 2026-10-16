# SPEC-004: Timings de saída CEA-861, aspect ratio e integer scaling

- **Tipo**: `[SW]` apenas
- **Impacto**: Alto — resolve a maior fonte de "sem imagem" com conversores VGA→HDMI e TVs modernas
- **Esforço**: Alto (programação de timing do VDS é a parte mais delicada do chip)

## Contexto

Conversores VGA→HDMI baratos e TVs rejeitam timings fora de CEA-861/VESA:
upstream #398, #494 (NTSC 59.3 Hz rejeitado — "snap 60/50" manual resolve),
#663 (768×576 PAL rejeitado), #261 (RGB full/limited washed out).
O fork tem issue #10 (576i/50Hz black screen — provável timing/EDID) e
issue #7 (pedido de 960p integer-scaled em janela dentro de 1080p).
Aspect ratio do preset 1080p é reconhecidamente imperfeito (upstream #388/#636).
RT5X/RT4K vendem exatamente "CEA-861 compliant + PAR transform + integer scaling".

## Requisitos

1. **Modo "HDMI-safe"**: presets com timings CEA-861 puros (720×480p, 720×576p,
   1280×720p50/60, 1920×1080p50/60 com pixel clocks exatos) e **snap automático**
   de framerate para 59.94/50.00 quando detectar (extensão do snap manual atual).
2. **Toggle RGB range full/limited** (offset/gain digital no VDS antes dos DACs)
   para conversores que assumem limited (upstream #261).
3. **Aspect ratio**: presets 4:3 exato por resolução + modo "fill 16:9"; corrigir
   AR do preset 1080p; generalizar "Prefer Full Height".
4. **Integer scaling / windowbox**: ajustar HTotal + V-scale a fatores inteiros
   (240p→960p 4x, →720p 3x); implementar **960p centrado com bordas em 1080p**
   (fork issue #7) via overscan/mask de saída se o VDS permitir blanking programável.
5. Resoluções adicionais: 1024×768 (PR upstream #668 pronto p/ portar), 1680×1050
   (upstream #629), 1080i/540p para HD CRTs (upstream #243 — Sony Hi-Scan/XBR).
6. Investigar **HScale>1023** (upstream #590) — cap impede integer horizontal ≤640px.

## Abordagem técnica

- Magic numbers atuais estão em `setOutModeHdBypass` (`GBSController.cpp:1593-1816`:
  `PLLAD_MD=2345/2474/2749`, `PLL648_CONTROL_01=0x75/0x35`) — primeiro passo é
  documentar/tipar esses valores (ver auditoria item 10) antes de derivar timings novos.
- Tabela data-driven de modos de saída (modeline: pixel clock, H/V sync, porch,
  polaridade) em vez de literais por função.
- Windowbox: pesquisar registradores VDS de active window/border no register
  definition do TV5725 (link abaixo) antes de prometer.

## Critérios de aceite

- Saturn PAL 576i → imagem em TV via conversor HDMI (fork #10).
- Sem "signal out of range" em TVs comuns nos modos HDMI-safe.
- 960p-em-1080p windowboxed: pixel-perfect 4x vertical, centralizado, ou declaração
  documentada de inviabilidade de hardware com evidência (regs testados).

## Referências

- upstream #261/#388/#398/#494/#590/#629/#636/#663, PR #668, #243
- fork issues #7, #10, #20 (audio drift: mitigação via FTL estável, não curável no GBS)
- TV5725 register definition PDF: https://ataribits.weebly.com/uploads/5/8/8/6/58865739/tv5725_registers_definition_v1.1.pdf
