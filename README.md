# S.B.P. Station — o totem do Smart Bus Point (ESP32 no Wokwi)

Entrega de **Physical Computing: IoT & IoB** — FIAP Global Solution 2026.2 (CitizenTech).

## Ficha da CitizenTech

| | |
|---|---|
| **Nome** | S.B.P. — Smart Bus Point |
| **Cidade e bairro** | São Paulo — eixo Metrô Conceição ↔ Pedreira (zona sul) |
| **Cidadão beneficiado** | Quem depende do ônibus todo dia, principalmente pessoas com deficiência visual, idosos e quem está sem celular ou sem internet |
| **Problema, em uma frase** | Quem está no ponto não sabe quando o ônibus chega, e quem não enxerga ou não tem celular fica sem informação nenhuma |
| **Como funciona** | Um totem no ponto mostra e fala a previsão de chegada; um QR Code leva a mesma informação para o app ou para a web |
| **O que esta parte faz** | É a maquete funcional do totem: display, botão de áudio acessível, sensor de presença, LED de status e comunicação MQTT com o resto da solução |
| **Indicador de impacto** | Usos do botão de áudio por hora e pessoas atendidas no ponto (medidos pelo próprio totem) |
| **ODS** | 11 (meta 11.2: transporte acessível), 10 (redução das desigualdades), 9 (infraestrutura e inovação) |

## Como o circuito entra na solução do grupo

O documento do projeto descreve três produtos: totem, app e web. Este circuito **é o totem**. Ele não é um projeto paralelo:

- o `painel/index.html` é o protótipo do "Ponto Virtual" (a tela que o QR Code abre) e fala com o totem pelos mesmos tópicos MQTT que o backend e o app do grupo vão usar;
- usa a **grade real da SPTrans (GTFS)** das mesmas linhas que o app e o backend usam;
- implementa as regras do documento: ponto final x ponto de passagem, ônibus a **3 minutos ou menos** em destaque (LED verde), botão sonoro com a frase "Ponto [nome]. Próximo ônibus: linha [X], previsão de chegada em [Y] minutos";
- publica por **MQTT** o que o app/web precisam para o "Ponto Virtual" (espelho do totem) e recebe comandos do backend (trânsito, avisos);
- continua funcionando **sem internet**, porque a grade fica gravada no ESP32. Isso atende exatamente quem o projeto quer incluir.

```
GTFS SPTrans ──(tools/gtfs_to_totem.py)──> totem_data.h ──> ESP32 (totem)
                                                             │  ▲
                                           telemetria/eventos│  │comandos
                                                             ▼  │
                                                     broker MQTT (HiveMQ)
                                                             │  ▲
                                                             ▼  │
                                            painel/index.html (Ponto Virtual + IoB)
                                            e, no projeto completo, backend + app
```

## Componentes

| Componente | Pino | Papel no totem |
|---|---|---|
| ESP32 DevKit | — | processamento, WiFi, MQTT |
| LCD 20x4 I2C | SDA 21, SCL 22 | nome do ponto, relógio e previsões, sempre no mesmo lugar (ver layout abaixo) |
| Botão verde | 4 | **acessibilidade**: fala a previsão (num totem real teria relevo e Braille) |
| Buzzer | 25 | sinal sonoro: bipe longo = 10 min, bipe curto = 1 min, 3 bipes agudos = ônibus no ponto |
| Sensor PIR | 27 | detecta gente no ponto: acorda a tela e conta a demanda |
| LED RGB + 3 resistores 220 Ω | 26, 33, 32 | fala do **próximo ônibus** (1ª linha de ônibus do display): verde até 3 min · amarelo 4–10 min · vermelho > 10 min · azul = falando |
| Potenciômetro | 34 | simula o nível de trânsito, que atrasa a previsão |
| Botão azul | 18 | troca o ponto onde o totem está "instalado" (ponto final ↔ ponto de passagem) |
| Chave deslizante | 23 | relógio real (NTP) ↔ demo acelerada (1 segundo = 1 minuto) |

## O que o display mostra

Nada rola nem troca sozinho: cada linha do LCD tem sempre o mesmo significado e só os números mudam.

```
PONTO FINAL (1 linha)          PONTO DE PASSAGEM (várias linhas)
+--------------------+         +--------------------+
|MET CONCEICAO  18:03|         |AV CUPECE 4734 18:03|   ponto + hora
|5757-51 > PEDREIRA  |         |5012 VL GUACUR  3MIN|   <- o LED se refere a este
|SAI EM 4 MIN        |         |607C JD MIRIAM  5MIN|
|DEPOIS: 19 E 34 MIN |         |5702 REFUGIO S  7MIN|
+--------------------+         +--------------------+
```

No ponto de passagem aparecem os **3 próximos ônibus**, do mais perto para o mais longe. O documento do grupo prevê lista de todas as linhas e carrossel com destaque de 10 s; isso fica para a tela grande do totem real e para o app/web. Num LCD de 20x4, mostrar mais que 3 deixava a tela confusa. O botão de áudio fala os mesmos 3.

## Como rodar no Wokwi

1. Abra <https://wokwi.com/projects/new/esp32>.
2. Cole o conteúdo de `wokwi/sketch.ino` na aba `sketch.ino` e o de `wokwi/diagram.json` na aba `diagram.json`.
3. Na setinha ao lado das abas, escolha **New file...** e crie `totem_data.h`, `eta.h` e `libraries.txt`, colando o conteúdo de cada um.
4. Dê play. Depois de alguns segundos o display mostra o ponto **Metrô Conceição**, linha **5757-51 Pedreira**.
5. Abra `painel/index.html` no navegador (duplo clique) para ver o Ponto Virtual e o painel IoB.

O que testar:

| Ação no Wokwi | O que acontece |
|---|---|
| Clicar no sensor PIR e simular movimento | tela acende, +1 presença, evento no painel |
| Botão verde (ou tecla `A`) | LED azul, bipes, frase no Serial Monitor e lida em voz alta no painel |
| Girar o potenciômetro | previsão atrasa, LED muda de cor |
| Chave deslizante | liga a demo acelerada: dá pra ver a contagem regressiva até "ÔNIBUS NO PONTO" |
| Botão azul (ou tecla `P`) | totem passa para a Av. Cupecê, 4734 (12 linhas): mostra os 3 próximos ônibus |
| Ficar 30 s sem presença | luz do display apaga (economia de energia) |
| No painel: enviar aviso ou trânsito | o totem recebe por MQTT e mostra no display |

## GPS em tempo real (API Olho Vivo) e servidor local

O `servidor_sbp.py` faz o papel do **backend** na demonstração:

1. **Serve o painel** em `http://localhost:8000/painel/`. Aberto como arquivo (`file://`), o OpenStreetMap bloqueia o mapa.
2. **Fala com a API Olho Vivo da SPTrans** (posição GPS dos ônibus). A API é gratuita, com cadastro em sptrans.com.br/desenvolvedores. O login é feito com um token e devolve um cookie de sessão.
3. **Ponte GPS → totem:** a cada 20 s pega a previsão por GPS do ponto onde o totem está (ele descobre pelo tópico `/status`) e manda pelo MQTT no tópico `/gps`, no formato `5757-51=4,19,34;607C-10=2,9`.

No totem:
- linha com GPS recente usa a previsão real (o display mostra `(GPS)` no ponto final) e o potenciômetro de trânsito deixa de valer, porque o GPS já inclui o trânsito real;
- sem GPS por 60 s, ou na demo acelerada, ele volta sozinho para a grade GTFS;
- trocar de ponto descarta o GPS do ponto anterior.

No painel:
- etiqueta "GPS Olho Vivo: ligado" no topo;
- marca **GPS** nas linhas do Ponto Virtual que vêm do Olho Vivo;
- na aba Explorar, o botão **Ônibus agora (GPS)** mostra os ônibus da linha no mapa, atualizando a cada 15 s;
- clicar numa parada mostra a previsão real de chegada, com ♿ nos ônibus acessíveis.

**Como rodar:**
1. Copie `.env.exemplo` para `.env` e cole o token: `SPTRANS_TOKEN=...`
2. Windows: duplo clique em `abrir_painel.bat`. Linux/Mac: `./abrir_painel.sh`. Ou `python servidor_sbp.py`.
3. Para a ponte GPS → totem: `pip install paho-mqtt` (sem isso o painel funciona, mas o totem fica só na grade).

**Segurança do token:**
- o token fica só no `.env` deste computador, que está no `.gitignore`;
- ele nunca vai pro navegador, pro ESP32 nem pro Wokwi, que é público;
- o servidor só aceita conexões do próprio PC (`127.0.0.1`), nunca serve arquivos que começam com ponto (como o `.env`), valida linha e parada antes de chamar a API e não escreve o token nas respostas nem no log.

Sem token, tudo continua funcionando com a grade GTFS.

## Painel: aba "Explorar linhas"

Além do Ponto Virtual, o `painel/index.html` tem uma segunda aba com o **mapa da cidade inteira** (1.325 linhas, 22 mil paradas):

- busca por número da linha, destino ou nome da parada (com ou sem acento);
- trajeto real da linha desenhado no mapa, nos dois sentidos, com todas as paradas;
- lista das paradas em ordem, com "+X min" desde a origem e os pontos de referência (metrô, terminal, hospital, shopping…) em destaque;
- intervalo da linha na hora atual e horário de funcionamento;
- clicar numa parada mostra todas as linhas que passam nela;
- **Onde estou**: usa a localização do celular/PC e lista as paradas mais próximas;
- no Ponto Virtual, clicar no número de uma linha abre o mapa com o ponto do totem destacado.

É o protótipo do "Mapa de Referências Urbanas" e da aba "Explorar" do app descritos no documento do grupo.

**Dados:** `tools/gtfs_to_mapa.py` lê o GTFS (incluindo o `shapes.txt`, que tem o desenho de cada rota) e gera `painel/dados_linhas.js` (~4,5 MB). O desenho é simplificado com o algoritmo de Douglas-Peucker (erro máximo de 12 m): de 1,15 milhão de pontos sobram 162 mil (14%). Os dados vão num `.js`, e não num `.json`, porque o painel abre direto do arquivo e, nesse modo, o navegador não deixa ler `.json`.
`python tools/gtfs_to_mapa.py <pasta_gtfs> -o painel/dados_linhas.js`

O mapa usa **Leaflet** com o fundo do **OpenStreetMap**. Aberto pelo servidor local, usa o servidor do próprio OSM. Aberto como arquivo (`file://`), o OSM bloqueia, então o painel usa a **CARTO**, que tem os mesmos dados, e mostra um aviso para abrir pelo servidor. Precisa de internet para carregar as ruas; o arquivo `dados_linhas.js` tem que ficar na mesma pasta do `index.html`.

## IoT: mensagens MQTT

Broker público `broker.hivemq.com`, prefixo `sbp-gs2026/totem/sbp-0001`.

| Tópico | Direção | Conteúdo |
|---|---|---|
| `/status` (retido) | totem → nuvem | online/offline, ponto, coordenadas. Se o totem cair, o broker publica `{"online":false}` sozinho (last will) |
| `/telemetria` | totem → nuvem | a cada 5 s: relógio, trânsito, ônibus/hora, 6 linhas mais próximas com 3 previsões cada |
| `/evento` | totem → nuvem | `presenca`, `audio`, `troca_ponto` |
| `/gps` | backend → totem | previsão GPS do Olho Vivo: `linha=min,min,min;...` |
| `/tts` | totem → nuvem | frase de acessibilidade para leitura em voz alta |
| `/iob` (retido) | totem → nuvem | contadores por hora (24 posições) de presenças e de usos do áudio |
| `/cmd` | nuvem → totem | `TRANSITO=0..100`, `AVISO=texto`, `PONTO=indice` |

## IoB: o que o totem aprende sobre o comportamento no ponto

O totem conta, **por hora do dia**, quantas pessoas chegaram (PIR) e quantas vezes o botão de áudio foi usado. O painel cruza isso com a oferta de ônibus do GTFS e escreve uma leitura, por exemplo: *"O maior movimento foi às 18h (40 detecções). Nessa hora passam 4 ônibus, cerca de 10 detecções por ônibus. Demanda alta para a oferta: vale sugerir reforço."* (o aviso de reforço só aparece a partir de 8 detecções por ônibus)

Para quem serve: a pessoa no ponto ganha uma tela que só gasta energia quando tem gente; o poder público e a operadora ganham dado de demanda por ponto e por horário e a prova de que o recurso de acessibilidade é usado (o indicador de impacto do projeto).

**Privacidade:** o sensor detecta movimento, não identifica ninguém. Só saem contagens. Nenhum dado pessoal é coletado, guardado ou transmitido.

## Dados

- **Fonte:** GTFS oficial da SPTrans, baixado em <https://www.sptrans.com.br/desenvolvedores/> (arquivos de 09/11/2026).
- `tools/gtfs_to_totem.py` lê `stops`, `trips`, `routes`, `stop_times` e `frequencies` e gera `wokwi/totem_data.h` para **qualquer parada da cidade**:
  `python tools/gtfs_to_totem.py <pasta_gtfs> 3703094 2203206 -o wokwi/totem_data.h`
- Pontos da demonstração: `3703094` Metrô Conceição (ponto final da 5757-51) e `2203206` Av. Cupecê, 4734 (ponto de passagem, 12 linhas).

## Testes

O cálculo da previsão fica em `wokwi/eta.h`, em C++ puro, para poder ser testado fora do ESP32.

```
cd tests
g++ -std=c++17 -I../wokwi test_eta.cpp -o test_eta && ./test_eta          # previsão x grade real
g++ -std=c++17 -Imock -I../wokwi sim_totem.cpp -o sim && ./sim            # firmware inteiro com Arduino simulado
node test_painel.js                                                        # painel com as mensagens geradas acima
node test_explorar.js                                                      # aba Explorar no Chrome (precisa do playwright)
python test_servidor.py                                                    # servidor + ponte GPS contra a API Olho Vivo falsa
node test_gps_painel.js                                                    # painel pelo servidor, com GPS falso, no Chrome
```

`sim_totem.cpp` compila o `sketch.ino` de verdade com versões falsas das bibliotecas do Arduino e roda um roteiro: presença, botão de áudio, demo acelerada, trânsito, comandos MQTT, troca de ponto, carrossel, tela dormindo, queda do broker e madrugada.

## Limites desta maquete

- Sem o `servidor_sbp.py` rodando com token, a previsão vem da **grade programada** do GTFS mais o trânsito simulado no potenciômetro. Com ele, vem do GPS (API Olho Vivo). A integração foi escrita pela documentação oficial e testada contra uma API falsa (`tests/olhovivo_falso.py`). Com o token real, confira no primeiro uso.
- **Demo sem token:** a API falsa simula ônibus andando em cima do trajeto real de cada linha, saindo no intervalo da hora e cada um com um atraso próprio, em qualquer linha ou parada da cidade. Rode `python tests/olhovivo_falso.py 8765` e, em outro terminal, o servidor com `SPTRANS_API=http://127.0.0.1:8765/v2.1` e `SPTRANS_TOKEN=token-de-teste`. **É simulação: diga isso se mostrar no vídeo.**
- O buzzer não fala. A voz sai pelo painel web; num totem real seria um módulo de áudio com alto-falante.
- O LCD 20x4 não tem acento, mapa nem toque. Mapa, toque, lista completa de linhas, carrossel e QR Code ficam no app/web do grupo.
- O broker é público e sem senha, o que serve para demonstração. Em produção: broker próprio com TLS e autenticação por totem.
- Os contadores IoB são do totem, contam detecções de movimento (não pessoas únicas) e zeram quando ele reinicia.
- O documento do grupo fala em "voltar à tela padrão após 30 s sem toque". Como o LCD não tem toque, aqui os 30 s viraram economia de energia: sem movimento por 30 s, a luz do display apaga, e volta com presença ou com qualquer botão. O LED continua aceso.
- Se o broker cair com o WiFi ligado, cada tentativa de reconexão (a cada 10 s) pode travar o totem por até 3 s.
