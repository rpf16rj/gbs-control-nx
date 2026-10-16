# SPEC-005: Deinterlacing avançado — film mode, MAD tuning e scanlines fora de 240p

- **Tipo**: `[SW]` apenas (viabilidade de film mode depende de pesquisa de registradores)
- **Impacto**: Médio-Alto — 480i é o ponto fraco restante (PS2, Saturn 480i, DVDs)
- **Esforço**: Alto

## Contexto

O datasheet do TV5725 anuncia deinterlacer "3D motion adaptive com diagonal edge
detection", **detecção de film mode 3:2/2:2 e still-image** — hoje inexplorados pelo
firmware (há precedente: rama descobriu bits ocultos de clamp SOG não documentados).
Modos atuais: Bob + Motion Adaptive com coeficientes hard-coded e `delay(60)` no
enable (`Deinterlacing.cpp:9-27`). Artefatos MAD reportados (upstream #529/#558).
Scanlines só funcionam em 240p — pedido de scanlines em 480i/p e força/intensidade
configurável (upstream #395/#462; nicole.express aponta a limitação).

## Requisitos

1. **Pesquisa de registradores**: localizar no register definition do TV5725 os
   campos de film-mode detection (3:2/2:2), still-image e noise level do MAD;
   criar aliases tipados em `src/gbs/tv5725.h` e testar em hardware.
2. Se viável: **inverse telecine automático** (modo Film) — enorme para PS2 30fps
   estável e conteúdo 480i telecined; fallback MAD se não existir.
3. **Thresholds do MAD expostos**: motion sensitivity + noise threshold como
   opções nomeadas na WebUI (perfis "estático/anime" vs "esporte/movimento").
4. **Scanlines generalizadas**: avaliar mecanismo atual (`src/video/Scanlines.cpp`);
   se via blank/overlay de saída → estender a 480p/passthrough; expor
   intensidade/espessura. Avaliar **hybrid scanlines** (modular por luminância,
   OSSC fw ≥0.88) — só se o mecanismo for ganho por linha.
5. Remover `delay(60)` do enable e parametrizar coeficientes em tabela.

## Abordagem técnica

- Etapa 0 obrigatória: leitura do register definition PDF + teste de bits
  desconhecidos via serial (`printInfo`/debug readout já existem em
  `src/utils/DebugHelpers.cpp`).
- Documentar achados negativos — se film mode não existir/acessível, o spec vira
  apenas MAD tuning + scanlines.

## Critérios de aceite

- Resposta documentada sobre existência dos registradores de film mode.
- Se existirem: conteúdo telecined 480i exibido sem combing e sem judder de weave.
- Scanlines funcionais em pelo menos um modo além de 240p, com intensidade ajustável.
- Nenhum delay bloqueante novo introduzido no loop.

## Referências

- TV5725 datasheet http://www.tvia.com/en/show.aspx?id=34&mid=334 · register PDF (SPEC-004)
- upstream #395/#462/#529/#558 · https://nicole.express/2021/gbs-is-under-control.html
- RetroTINK-5X motion-adaptive + inverse telecine · OSSC hybrid scanlines
