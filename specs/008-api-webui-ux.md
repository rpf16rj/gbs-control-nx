# SPEC-008: API estável, WebUI e UX de presets

- **Tipo**: `[SW]` apenas
- **Impacto**: Médio — destrava automação (Home Assistant já existe e usa protocolo frágil) e corrige issue #4
- **Esforço**: Médio

## Contexto

- Protocolo atual: `GET /sc?x` / `/uc?x` com **um caractere** por comando +
  WS :81 com frame binário ad-hoc de 6 bytes (`'#'+preset+slot+flags`, colide com
  `'@'`) — sem versão/schema; `webSocketEvent` comentado → WS é só broadcast.
- Integração Home Assistant (`hudsonbrendon/ha-gbs-control`) consome esse frame
  frágil — demanda real por API documentada.
- Fork issue #4: botões de resolução não respondem (v1.2.3) — provável regressão
  WS/handler; investigar junto.
- Slots são blob binário 72×32 B parseado no cliente (`StructParser`,
  `webui/src/index.ts:12-78`); presets são dumps de registradores não portáveis;
  pedido antigo de import/export (upstream #105); `scanlineStrength` corrompe
  serialização char-a-char (`PreferencesManager.cpp`).

## Requisitos

1. **API REST/JSON versionada**: `GET /api/v1/status` (modo detectado, Hz medido,
   linhas/frame, HTotal/phase/SOG atuais — dados já existem em `rto->`),
   `POST /api/v1/command`, `GET/PUT /api/v1/preset/{slot}` — mantendo endpoints
   legados por compatibilidade.
2. **Fila de comandos thread-safe**: handlers HTTP só enfileiram; loop consome
   (elimina race conditions da auditoria item 6 e provavelmente a issue #4).
3. **Tela INFO estilo OSSC**: painel na WebUI com formato detectado, Hz real,
   linhas, HTotal, phase, SOG level, FTL lock status.
4. **Presets nomeados + import/export JSON** (LittleFS): compartilhável tipo
   perfis OSSC/RT4K; validação de schema; corrigir serialização multi-dígito.
5. **Persistir dev options úteis** por preset (IF auto offset, SDRAM clock,
   oversampling) — hoje resetam.
6. **Segurança mínima**: autenticação (token/senha opcional) em endpoints
   destrutivos (`/filesystem/*`, `/slot/remove`, OTA) + corrigir vazamento de
   credenciais via `/filesystem/download` + TLS/verificação no OTA ou
   documentação do risco + divisão por zero (`WebServer.cpp:580`).
7. Versão/commit exibidos na WebUI e OLED (upstream #574) — `FIRMWARE_VERSION`
   hoje é header com linkage interna (`FirmwareVersion.h:6`); gerar de build flag.

## Abordagem técnica

- JSON: ArduinoJson já é padrão no ecossistema; cuidado com heap ESP8266
  (~40 KB) — documentos pequenos/streaming, sem `String` concat.
- WS: versionar frame (`'#'`→magic+ver) ou migrar telemetria para JSON sobre WS;
  chamar `webSocket.loop()` também no ESP8266.
- WebUI: build já é TS (`npm run build` → `webui_html.h`); adicionar aba INFO
  sem inflar demais o HTML embutido.

## Critérios de aceite

- `curl http://gbs/api/v1/status` retorna JSON válido com modo/Hz/HTotal.
- 100 comandos rápidos via API não perdem nenhum (teste que reproduz #4).
- Exportar preset → JSON; importar em outra unidade → mesma imagem.
- Endpoints destrutivos exigem auth quando configurada.

## Referências

- fork issue #4 · upstream #105, #323/#422 (presetSlot char), #574
- https://github.com/hudsonbrendon/ha-gbs-control
- `src/web/WebServer.cpp`, `src/web/WebSocketHandler.cpp`, `webui/src/index.ts`
