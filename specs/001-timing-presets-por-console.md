# SPEC-001: Presets de timing ótimos por console (optimal sampling)

- **Tipo**: `[SW]` apenas
- **Impacto**: Alto — elimina a queixa #1 de "imagem borrada/não pixel-perfect" vs OSSC
- **Esforço**: Alto (curadoria de timings + UI)

## Contexto

O OSSC virou referência porque cada console tem dot clock específico e ele permite
ajustar `H.samplerate`, `H.synclen/backporch` e fase por console (perfis FirebrandX).
A GBS possui os mesmos controles via registradores do TV5725 — PLL divider (HTotal),
`phaseADC`, `phaseSP` — mas os presets atuais (`presets/*.h`) são genéricos por
resolução, não por console. `autoBestHtotal` já existe como dev option.

## Requisitos

1. Tabela de timings ótimos por console/modo, mapeando samplerate conhecido →
   PLL divider + HTotal + backporch + phase inicial:
   - SNES/NES: 341 · Master System: 342 · Genesis H32: 342 / H40: 427.5
   - PS1: 341.3 / 426.6 / 487.6 / 682.6 (por largura de modo)
   - Saturn: 320→427 / 352→455 / 704×480i→910 · Neo Geo: timing próprio
   - Fontes: FirebrandX OSSC profiles, junkerhq Optimal_timings.
2. Seleção via WebUI (dropdown "Console preset") e/ou slot de preset dedicado;
   opcionalmente auto-seleção por contagem de linhas/largura detectada.
3. Integração com `autoBestHtotal` para refinamento local (busca fina equivalente
   ao `H.s.rate frac` do OSSC).
4. Presets devem coexistir com os matched presets NTSC/PAL existentes.

## Abordagem técnica

- Estrutura de dados `ConsoleTiming { id, name, linesExpected, htotal, samplerateDiv,
  backporch, phaseAdc, phaseSp }` em novo arquivo `src/gbs/ConsoleTimings.h`.
- Aplicar após `writeProgramArrayNew()` dentro de `doPostPresetLoadSteps()`
  (`GBSController.cpp:690`), antes do clamp/phase final.
- Persistir escolha em `userOptions`/slot (ver SPEC-008 para import/export).
- Comando serial/WebUI novo (protocolo de 1 char está saturado — considerar
  endpoint `/console_preset?id=` ou migração para API JSON da SPEC-008).

## Critérios de aceite

- SNES 256×224 e Genesis 320×224 exibem pixel clock casado (sem shimmer horizontal).
- Saturn hi-res e PS1 multi-resolução trocam de preset corretamente.
- Fallback para preset genérico quando console não reconhecido.

## Referências

- https://www.firebrandx.com/osscprofiles.html · https://junkerhq.net/xrgb/index.php/Optimal_timings
- `src/gbs/GBSPresets.cpp`, `src/gbs/GBSPhase.cpp`, `presets/*.h`
