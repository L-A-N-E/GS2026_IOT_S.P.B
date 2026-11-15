# CLAUDE.md — S.B.P. (Smart Bus Point) · FIAP Global Solution 2026.2 "CitizenTech"

Contexto para quem continuar este projeto (pessoa ou IA). Leia antes de mexer.

## O projeto
- **Grupo:** Alice Santos Bulhões (RM554499), Eduardo Oliveira Cardoso Madid (RM556349), Nicolas Haubricht Hainfellner (RM556259), Guilherme da Cunha Melo (RM555310).
- **Solução:** S.B.P., um ponto de ônibus inteligente em São Paulo com totem acessível (display, botão de voz), app e site, ligados por QR Code.
- **Cidadão beneficiado:** passageiro diário, pessoas com deficiência visual, idosos e quem está sem celular ou internet.
- **Esta pasta** tem as entregas do Eduardo em **Physical Computing: IoT & IoB** (pronta) e vai receber a de **Cybersecurity** (não começada).
- **Prazo:** 19/11, às 23h55, no Teams. Comunicação em português (BR), informal.

### Regras gerais da GS
- **IA:** pode ser usada como copiloto, desde que os prompts sejam registrados em `prompts.md` e cada integrante saiba explicar cada linha.
- **Dados:** nada de dados pessoais reais. Só dados simulados ou públicos, sempre com a fonte.
- **Escopo:** nada de projeto paralelo; tudo precisa servir à solução S.B.P.
- **Estilo:** simples primeiro, README claro.

## Estado: IoT & IoB (PRONTO e testado pelo Eduardo no Wokwi)
Entregáveis da disciplina: link do Wokwi, link do vídeo (até 4 min) e `integrantes.txt`.
**Falta:** salvar o Wokwi, gravar o vídeo (`ROTEIRO_VIDEO.md`) e colar os 2 links no `integrantes.txt`.

| Caminho | O que é |
|---|---|
| `wokwi/sketch.ino` | Firmware ESP32 do totem: LCD 20x4 I2C, botão de áudio, buzzer, PIR, LED RGB, potenciômetro (trânsito), botão de troca de ponto, chave de demo acelerada. Comunicação por MQTT (broker.hivemq.com, prefixo `sbp-gs2026/totem/sbp-0001`) |
| `wokwi/eta.h` | Cálculo da previsão de chegada pela grade GTFS. C++ puro, testável fora do ESP32 |
| `wokwi/totem_data.h` | **Gerado** por `tools/gtfs_to_totem.py`. Pontos 3703094 (Metrô Conceição, ponto final) e 2203206 (Av. Cupecê 4734, passagem) |
| `wokwi/diagram.json`, `libraries.txt` | Circuito e bibliotecas do Wokwi |
| `tools/gtfs_to_totem.py` | GTFS SPTrans → `totem_data.h`, para qualquer parada |
| `tools/gtfs_to_mapa.py` | GTFS → `painel/dados_linhas.js`: a cidade inteira, com trajetos simplificados por Douglas-Peucker |
| `painel/index.html` | Aba "Totem ao vivo" (Ponto Virtual, voz, IoB, comandos) + aba "Explorar linhas" (mapa Leaflet/OSM, busca, trajeto, paradas, "onde estou", ônibus GPS) |
| `servidor_sbp.py` | "Backend" local: serve o painel em http://localhost:8000 (o OSM bloqueia `file://`), faz proxy da API Olho Vivo (token só no `.env`) e roda a ponte GPS → totem pelo tópico `/gps` (precisa de `paho-mqtt`) |
| `tests/` | `test_eta.cpp`, `sim_totem.cpp` + `mock/` (firmware inteiro no PC), `test_painel.js`, `test_explorar.js` e `test_gps_painel.js` (Playwright), `test_servidor.py`, `olhovivo_falso.py` (API Olho Vivo simulada, com ônibus andando no trajeto real) |
| `README.md`, `ROTEIRO_VIDEO.md`, `prompts.md`, `integrantes.txt` | Documentação e entregáveis |

### Decisões que NÃO devem ser desfeitas sem conversar
- **LCD:** layout fixo, nada rola nem troca sozinho, a pedido do usuário. No ponto de passagem aparecem só os 3 próximos ônibus. O LED se refere sempre ao 1º ônibus da lista.
- **Trânsito:** o atraso é somado **depois** de escolher a partida pela grade. Se somar antes, a previsão diminui com mais trânsito; foi um bug real, coberto por teste.
- **GPS (`/gps`):** é válido por 60 s. É ignorado na demo acelerada e descartado ao trocar de ponto. Com GPS ativo, o potenciômetro não soma atraso.
- **Token:** nunca vai para o firmware, o Wokwi, o navegador ou o git. O servidor escuta só em 127.0.0.1 e nunca serve arquivos que começam com ponto.
- **Honestidade:** a previsão sem token é a **grade programada** (estimativa), não GPS. A API falsa é **simulação** e precisa ser dita como tal no vídeo.

### Pendência conhecida: API Olho Vivo
O código segue a documentação oficial (POST `/Login/Autenticar?token=`, que devolve um cookie; `/Previsao/Parada`, `/Linha/Buscar`, `/Posicao/Linha`).
- O login responde **`false`** com o app "homologado e em uso", inclusive com uma chave nova. O `curl` também deu `false`, então é do lado da SPTrans (talvez seja só tempo para ativar).
- Diagnóstico: `python servidor_sbp.py --testar`.
- Risco a observar: a doc diz que a busca de paradas cobre só as de corredor. Se `/Previsao/Parada` vier vazio para o stop_id do GTFS, usar `/Parada/Buscar` para mapear o código.
- O usuário colou uma chave no chat: recomendar gerar outra.

### Como rodar e testar
```
python servidor_sbp.py                     # painel em http://localhost:8000/painel/ (token em .env, ver .env.exemplo)
python tests/olhovivo_falso.py 8765        # demo sem token; no outro terminal:
#   SPTRANS_API=http://127.0.0.1:8765/v2.1 SPTRANS_TOKEN=token-de-teste python servidor_sbp.py
cd tests
g++ -std=c++17 -I../wokwi test_eta.cpp -o test_eta && ./test_eta
g++ -std=c++17 -Imock -I../wokwi sim_totem.cpp -o sim && ./sim     # gera /tmp/mqtt_publicados.jsonl
node test_painel.js
python test_servidor.py
node test_explorar.js && node test_gps_painel.js                   # precisam de playwright + Leaflet/mqtt.js locais (VENDOR)
```
Ao mudar o firmware, rode `sim_totem.cpp` e peça para o usuário testar no Wokwi; o Wokwi não roda daqui.

**Nesta máquina (Windows do Eduardo, 14/11):** todos os 6 conjuntos de teste passaram. Ferramentas instaladas nesta data:
- `g++` (WinLibs via winget), **fora do PATH**: usar `C:\Users\dumad\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe` ou adicionar essa pasta ao PATH;
- Playwright + Chromium em `tests/node_modules`; vendor (Leaflet/mqtt.js) já extraído em `C:\tmp\vend\`;
- os testes usam `/tmp/...`, que no Windows vira `C:\tmp` (já criado).
- Os testes de navegador em Windows: `PYTHON=python node test_gps_painel.js`.

Cuidado com servidor antigo preso na porta 8000: um processo de sessão anterior ficou rodando com código velho e o `/api/posicao` vinha vazio; matar o processo e subir de novo resolveu. O `test_gps_painel.js` precisa da porta 8765 livre (derrubar a API falsa da demo antes, e subir de novo depois).

## A FAZER: Cybersecurity (não começado)
**O que o PDF pede (página "A Missão Técnica" + "Entregas e Critérios"), entrega em ZIP:**
1. **Código do novo módulo:** "configurar novas regras no motor de parsing" para detectar **PII** (CPF, dados de saúde, senhas/credenciais).
2. **Auditoria do protótipo:** evidências de **Taint Analysis** em ação, rastreando o dado sensível da entrada até uma saída insegura e bloqueando o vazamento.
3. **Dashboard atualizado:** vídeo + print do painel gerando o indicador **"Risco Cidadão"**.
4. **Tradução via IA:** transformar o achado técnico em linguagem que o cidadão entenda.

**Combinado com o usuário:**
- Não existe ferramenta base das aulas (não tem a ver com o SpecRecon do Ford Challenge): vai ser criada do zero, em Python.
- O "protótipo auditado" é o **código do próprio S.B.P.**: o backend do grupo (SOA/C#, de outro integrante, ainda sem repositório/stack confirmados) ou, se ele não ficar pronto, um mini-backend S.B.P. com falhas de propósito.
- Plano esboçado ("SBP-Guard"):
  - **Parser de PII:** CPF com dígito verificador; e-mail, telefone, senha/token/JWT; perfil de acessibilidade/deficiência como **dado sensível de saúde (LGPD art. 11)**.
  - **Taint:** fontes (request/form/MQTT) → saídas (log, print, resposta, arquivo, MQTT sem TLS), bloqueando quando não há mascaramento.
  - **IA:** LLM com fallback offline.
  - **Dashboard:** Streamlit ou a própria aba do painel, com Risco Cidadão de 0 a 100 = tipo de dado × severidade × vazou/bloqueado.
- **Material real que dá pra auditar já existe aqui:**
  - `servidor_sbp.py`: o token Olho Vivo, um caso de credencial (CWE-798/532);
  - os tópicos MQTT públicos e sem TLS;
  - o histórico do token colado no chat (bom exemplo de vazamento de credencial).
- **Perguntas ainda abertas para o usuário:** qual LLM/API key; stack e repositório do backend do grupo; se existe rubrica de Cyber no Teams.

## Preferências do Eduardo
- Respostas diretas, em PT-BR informal. Explicar o raciocínio antes de executar e avançar em passos, sem despejar tudo de uma vez.
- Quando mudar só alguns arquivos, mandar só esses e dizer em qual pasta cada um vai.
- Pode usar analogias (ele pediu uma em "termos de Fortnite").
