/*
  S.B.P. - Smart Bus Point  |  Totem inteligente de ponto de onibus (ESP32)
  FIAP - Global Solution 2026.2 (CitizenTech) - Physical Computing: IoT & IoB

  O que este circuito faz na solucao do grupo:
    - E a maquete do "S.B.P. Station": o totem que fica na parada.
    - Mostra no display quando chega o proximo onibus, usando a grade REAL
      da SPTrans (GTFS) embutida em totem_data.h -> funciona ate sem internet.
    - Acessibilidade: botao tatil (verde) "fala" a previsao (bipes no buzzer
      + frase enviada por MQTT para ser lida em voz alta pelo Ponto Virtual).
    - Sensor de presenca (PIR) acorda a tela e conta a demanda do ponto.
    - LED RGB (sobre o proximo onibus, 1a linha do display): verde ate 3 min,
      amarelo 4 a 10, vermelho > 10.
    - Potenciometro simula o nivel de transito (atrasa a previsao).
    - MQTT: publica telemetria/eventos (IoT) e contadores por hora (IoB) e
      recebe comandos do backend (transito, aviso, troca de ponto).
    - GPS (API Olho Vivo da SPTrans): o backend (servidor_sbp.py) manda pelo
      topico .../gps a previsao real de cada linha. Se parar de chegar por
      60 s, o totem volta sozinho para a grade GTFS.

  Integrantes: Alice Santos Bulhoes (RM554499), Eduardo Oliveira Cardoso Madid
  (RM556349), Nicolas Haubricht Hainfellner (RM556259), Guilherme da Cunha
  Melo (RM555310)
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include "totem_data.h"   // pontos e linhas reais (gerado pelo tools/gtfs_to_totem.py)
#include "eta.h"          // calculo da previsao de chegada

// ---------------------------------------------------------------- pinos
#define PIN_BTN_AUDIO 4    // botao verde: acessibilidade (audio)
#define PIN_BTN_PONTO 18   // botao azul: troca o ponto onde o totem "esta instalado"
#define PIN_SW_DEMO   23   // chave: relogio real (NTP) x demo acelerada (1 s = 1 min)
#define PIN_BUZZER    25
#define PIN_PIR       27
#define PIN_LED_R     26
#define PIN_LED_G     33
#define PIN_LED_B     32
#define PIN_POT       34   // potenciometro: nivel de transito (0 a 100%)

// ---------------------------------------------------------------- rede
#define WIFI_SSID  "Wokwi-GUEST"
#define WIFI_SENHA ""
#define MQTT_HOST  "broker.hivemq.com"
#define MQTT_PORTA 1883
#define TOTEM_ID   "sbp-0001"
#define TOPICO     "sbp-gs2026/totem/" TOTEM_ID   // prefixo de todos os topicos

// ---------------------------------------------------------------- regras do totem (vem do documento do projeto)
const int32_t  URGENTE_MIN      = 3;      // ate 3 min = chegando (LED verde)
const int32_t  NO_PONTO_SEG     = 120;    // ponto final: onibus parado embarcando
const uint32_t TELA_DORME_MS    = 30000;  // 30 s sem ninguem -> apaga a luz da tela
#define MAX_LINHAS 12
#define QTD_ETA 3

LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiClient wifi;
PubSubClient mqtt(wifi);

// ---------------------------------------------------------------- estado
uint8_t pontoAtual = 0;                 // indice em PONTOS[]
double  relogioSeg = 17 * 3600.0 + 55 * 60;  // segundos desde 00:00 (fallback: 17:55)
bool    modoDemo = false;
int     transitoPct = 0;
bool    transitoRemoto = false;         // true = valor veio do backend por MQTT
int     potAnterior = -1;
char    aviso[21] = "";                 // aviso operacional vindo do backend

struct Previsao { int32_t eta[QTD_ETA]; uint8_t n; bool gps; };

// Ultima previsao por GPS recebida do backend (Olho Vivo), por linha do ponto atual
struct Gps { int16_t min[QTD_ETA]; uint8_t n; uint32_t recebido; };
Gps      gps[MAX_LINHAS];
const uint32_t GPS_VALIDO_MS = 60000;   // sem atualizacao por 60 s -> volta pra grade
Previsao prev[MAX_LINHAS];
uint8_t  ordem[MAX_LINHAS];             // linhas ordenadas da mais proxima pra mais distante

// IoB: contadores anonimos por hora do dia (nao guarda nada de pessoa nenhuma)
uint16_t contPresenca[24];
uint16_t contAudio[24];
bool     iobMudou = false;

bool     telaAcesa = true;
uint32_t ultimaPresenca = 0;

// fila de notas do buzzer (toca sem travar o resto do programa)
struct Nota { uint16_t freq; uint16_t ms; };
Nota     fila[48];
uint8_t  filaQtd = 0, filaPos = 0;
uint32_t notaFim = 0;
bool     tocando = false;

// ---------------------------------------------------------------- utilidades
const Ponto& ponto() { return PONTOS[pontoAtual]; }
uint8_t horaAtual() { return ((int32_t)relogioSeg / 3600) % 24; }

void hhmm(int32_t seg, char* out) {     // 67500 -> "18:45"
  seg = ((seg % 86400) + 86400) % 86400;
  snprintf(out, 6, "%02d:%02d", (int)(seg / 3600), (int)((seg % 3600) / 60));
}

// Escreve uma linha no LCD so se o texto mudou (evita a tela piscar)
char cacheLCD[4][21];
void linhaLCD(uint8_t linha, const char* txt) {
  char buf[21];
  snprintf(buf, sizeof(buf), "%-20.20s", txt);
  if (strcmp(buf, cacheLCD[linha]) == 0) return;
  strcpy(cacheLCD[linha], buf);
  lcd.setCursor(0, linha);
  lcd.print(buf);
}

void led(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r); digitalWrite(PIN_LED_G, g); digitalWrite(PIN_LED_B, b);
}

// ---------------------------------------------------------------- buzzer
void notaAdd(uint16_t freq, uint16_t ms) {
  if (filaQtd < 48) fila[filaQtd++] = {freq, ms};
}
void filaIniciar() { filaPos = 0; notaFim = 0; tocando = filaQtd > 0; }

void tocarFila() {
  if (!tocando || millis() < notaFim) return;
  if (filaPos >= filaQtd) {             // acabou
    noTone(PIN_BUZZER); tocando = false; filaQtd = 0; return;
  }
  Nota n = fila[filaPos++];
  if (n.freq) tone(PIN_BUZZER, n.freq); else noTone(PIN_BUZZER);
  notaFim = millis() + n.ms;
}

// ---------------------------------------------------------------- MQTT: publicar
void publicar(const char* sub, const char* msg, bool reter) {
  if (!mqtt.connected()) return;        // offline: o totem segue funcionando
  char topico[64];
  snprintf(topico, sizeof(topico), TOPICO "/%s", sub);
  mqtt.publish(topico, msg, reter);
}

void publicarStatus() {
  char j[260];
  snprintf(j, sizeof(j), "{\"online\":true,\"totem\":\"%s\",\"ponto\":\"%s\",\"nome\":\"%s\",\"final\":%s,\"lat\":%.6f,\"lon\":%.6f}",
           TOTEM_ID, ponto().id, ponto().nomeFala, ponto().pontoFinal ? "true" : "false",
           ponto().lat, ponto().lon);
  publicar("status", j, true);
}

void publicarEvento(const char* tipo) {
  char h[6], j[160];
  hhmm((int32_t)relogioSeg, h);
  snprintf(j, sizeof(j), "{\"tipo\":\"%s\",\"ponto\":\"%s\",\"hora\":%d,\"relogio\":\"%s\"}",
           tipo, ponto().id, horaAtual(), h);
  publicar("evento", j, false);
  Serial.printf("[evento] %s\n", j);
}

void publicarTelemetria() {
  char h[6], j[900];
  hhmm((int32_t)relogioSeg, h);
  int p = snprintf(j, sizeof(j),
    "{\"ponto\":\"%s\",\"relogio\":\"%s\",\"demo\":%s,\"transito\":%d,\"nivel\":\"%s\","
    "\"onibusHora\":%d,\"telaAcesa\":%s,\"aviso\":\"%s\",\"linhas\":[",
    ponto().id, h, modoDemo ? "true" : "false", transitoPct, nivelTransito(transitoPct),
    onibusPorHora(ponto(), horaAtual()), telaAcesa ? "true" : "false", aviso);
  uint8_t qtd = ponto().qtdLinhas < 6 ? ponto().qtdLinhas : 6;   // as 6 mais proximas
  for (uint8_t k = 0; k < qtd; k++) {
    const Linha& l = ponto().linhas[ordem[k]];
    const Previsao& pv = prev[ordem[k]];
    if (p > (int)sizeof(j) - 120) break;                         // nao deixa estourar o buffer
    p += snprintf(j + p, sizeof(j) - p, "%s{\"n\":\"%s\",\"d\":\"%s\",\"g\":%d,\"eta\":[", k ? "," : "", l.num, l.destinoFala, pv.gps ? 1 : 0);
    for (uint8_t e = 0; e < pv.n; e++)
      p += snprintf(j + p, sizeof(j) - p, "%s%d", e ? "," : "", (int)(pv.eta[e] / 60));
    p += snprintf(j + p, sizeof(j) - p, "]}");
  }
  snprintf(j + p, sizeof(j) - p, "]}");
  publicar("telemetria", j, false);
}

// IoB: manda os contadores das 24 h. Como vai o vetor inteiro, nada se perde
// se o totem ficou offline: quando a rede volta, o painel recebe tudo.
void publicarIoB() {
  char j[420];
  int p = snprintf(j, sizeof(j), "{\"ponto\":\"%s\",\"hora\":%d,\"presencas\":[", ponto().id, horaAtual());
  for (uint8_t h = 0; h < 24; h++) p += snprintf(j + p, sizeof(j) - p, "%s%d", h ? "," : "", contPresenca[h]);
  p += snprintf(j + p, sizeof(j) - p, "],\"audio\":[");
  for (uint8_t h = 0; h < 24; h++) p += snprintf(j + p, sizeof(j) - p, "%s%d", h ? "," : "", contAudio[h]);
  snprintf(j + p, sizeof(j) - p, "]}");
  publicar("iob", j, true);
  iobMudou = false;
}

// ---------------------------------------------------------------- previsoes
void calcularPrevisoes() {
  const Ponto& pt = ponto();
  uint8_t qtd = pt.qtdLinhas < MAX_LINHAS ? pt.qtdLinhas : MAX_LINHAS;
  for (uint8_t i = 0; i < qtd; i++) {
    ordem[i] = i;
    // 1) tem previsao por GPS recente? usa ela (ja inclui o transito de verdade)
    //    Na demo acelerada o relogio nao e o real, entao o GPS nao vale.
    uint32_t idade = millis() - gps[i].recebido;
    if (!modoDemo && gps[i].recebido && gps[i].n && idade < GPS_VALIDO_MS) {
      prev[i].n = gps[i].n; prev[i].gps = true;
      for (uint8_t k = 0; k < gps[i].n; k++) {
        int32_t e = (int32_t)gps[i].min[k] * 60 - (int32_t)(idade / 1000);   // desconta o tempo desde que chegou
        prev[i].eta[k] = e < 0 ? 0 : e;
      }
      continue;
    }
    // 2) senao, grade GTFS + transito simulado
    int32_t atraso = atrasoPorTransito(pt.linhas[i].offsetMin, transitoPct);
    prev[i].n = proximasChegadas(pt.linhas[i], (int32_t)relogioSeg, atraso, prev[i].eta, QTD_ETA);
    prev[i].gps = false;
  }
  // ordena pela chegada mais proxima (insertion sort: sao no maximo 12 linhas)
  for (uint8_t i = 1; i < qtd; i++) {
    uint8_t x = ordem[i]; int8_t j = i - 1;
    int32_t ex = prev[x].n ? prev[x].eta[0] : INT32_MAX;
    while (j >= 0 && (prev[ordem[j]].n ? prev[ordem[j]].eta[0] : INT32_MAX) > ex) {
      ordem[j + 1] = ordem[j]; j--;
    }
    ordem[j + 1] = x;
  }
}

bool temPrevisao() { return prev[ordem[0]].n > 0; }
int32_t menorEta() { return prev[ordem[0]].eta[0]; }

// O LED sempre fala do PROXIMO onibus = primeira linha de onibus do display
void atualizarLed() {
  if (tocando)                 { led(0, 0, 1); return; }   // azul: falando
  if (!temPrevisao())          { led(1, 0, 0); return; }
  int32_t e = menorEta();
  if (e / 60 <= URGENTE_MIN)   led(0, 1, 0);               // verde: ate 3 min (mesma conta do texto)
  else if (e / 60 <= 10)       led(1, 1, 0);               // amarelo: 4 a 10 min
  else                         led(1, 0, 0);               // vermelho: mais de 10 min
}

// ---------------------------------------------------------------- telas
// Regra de ouro do display: nada rola, nada troca sozinho. Cada linha do LCD
// tem sempre o mesmo significado, so os numeros mudam.
//
//  PONTO FINAL (1 linha de onibus)        PONTO DE PASSAGEM (varias linhas)
//  +--------------------+                 +--------------------+
//  |MET CONCEICAO  18:03|  ponto + hora   |AV CUPECE 4734 18:03|
//  |5757-51 > PEDREIRA  |  linha          |5012 VL GUACUR  3MIN|  <- o LED fala deste
//  |SAI EM 4 MIN        |  previsao       |607C JD MIRIAM  5MIN|
//  |DEPOIS: 19 E 34 MIN |  seguintes      |5702 REFUGIO S  7MIN|
//  +--------------------+                 +--------------------+

// "5012-10" -> "5012": no ponto o destino ja diferencia o sentido
void numCurto(const char* num, char* out) {
  uint8_t i = 0;
  while (num[i] && num[i] != '-' && i < 4) { out[i] = num[i]; i++; }
  out[i] = 0;
}

// minutos no formato de 5 letras da lista: "AGORA", " 3MIN", "12MIN", "05:00"
void etaCurto(const Previsao& pv, char* out) {
  if (!pv.n)                   snprintf(out, 6, "  ---");
  else if (pv.eta[0] < 60)     snprintf(out, 6, "AGORA");
  else if (pv.eta[0] < 100*60) snprintf(out, 6, "%2dMIN", (int)(pv.eta[0] / 60));
  else                         hhmm((int32_t)relogioSeg + pv.eta[0], out);     // longe: mostra o horario
}

void cabecalho() {
  char buf[32], h[6];
  if (aviso[0]) {                                          // aviso do backend fica fixo no lugar do nome
    snprintf(buf, sizeof(buf), "! %s", aviso);
  } else {
    hhmm((int32_t)relogioSeg, h);
    snprintf(buf, sizeof(buf), "%-14.14s %5s", ponto().nomeCurto, h);
  }
  linhaLCD(0, buf);
}

void telaPontoFinal() {
  char buf[32], h[6], h2[6];
  const Linha& l = ponto().linhas[ordem[0]];
  const Previsao& pv = prev[ordem[0]];
  snprintf(buf, sizeof(buf), "%s > %s", l.num, l.destino);
  linhaLCD(1, buf);

  if (!pv.n) {
    linhaLCD(2, "SEM SERVICO AGORA");
    linhaLCD(3, "");
    return;
  }
  if (pv.eta[0] <= NO_PONTO_SEG) {                          // onibus parado no ponto final
    linhaLCD(2, "NO PONTO - EMBARQUE");
    if (pv.eta[0] < 60) snprintf(buf, sizeof(buf), "SAINDO AGORA");
    else                snprintf(buf, sizeof(buf), "SAI EM %d MIN", (int)(pv.eta[0] / 60));
    linhaLCD(3, buf);
    return;
  }
  if (pv.eta[0] >= 100 * 60) { hhmm((int32_t)relogioSeg + pv.eta[0], h); snprintf(buf, sizeof(buf), "PROXIMO AS %s", h); }
  else                        snprintf(buf, sizeof(buf), "SAI EM %d MIN%s", (int)(pv.eta[0] / 60), pv.gps ? "  (GPS)" : "");
  linhaLCD(2, buf);

  if (pv.n < 3)                    buf[0] = 0;
  else if (pv.eta[2] >= 100 * 60) { hhmm((int32_t)relogioSeg + pv.eta[1], h); hhmm((int32_t)relogioSeg + pv.eta[2], h2); snprintf(buf, sizeof(buf), "DEPOIS %s E %s", h, h2); }
  else                             snprintf(buf, sizeof(buf), "DEPOIS: %d E %d MIN", (int)(pv.eta[1] / 60), (int)(pv.eta[2] / 60));
  linhaLCD(3, buf);
}

void telaPassagem() {
  char buf[32], n[6], e[6];
  uint8_t qtd = ponto().qtdLinhas < MAX_LINHAS ? ponto().qtdLinhas : MAX_LINHAS;
  for (uint8_t r = 0; r < 3; r++) {                         // os 3 proximos onibus, do mais perto pro mais longe
    if (r >= qtd) { linhaLCD(r + 1, ""); continue; }
    const Linha& l = ponto().linhas[ordem[r]];
    numCurto(l.num, n);
    etaCurto(prev[ordem[r]], e);
    snprintf(buf, sizeof(buf), "%-4s %-9.9s %5s", n, l.destinoCurto, e);
    linhaLCD(r + 1, buf);
  }
}

void desenhar() {
  cabecalho();
  if (ponto().pontoFinal) telaPontoFinal(); else telaPassagem();
}

// ---------------------------------------------------------------- acessibilidade: botao de audio
// Num totem de verdade aqui entraria um modulo de voz (ex.: DFPlayer + alto-falante).
// Na simulacao: (1) a frase vai por MQTT e o Ponto Virtual le em voz alta;
// (2) o buzzer toca um codigo: bipe longo = 10 min, bipe curto = 1 min.
void falar() {
  char frase[420];
  int p = 0;
  notaAdd(880, 120); notaAdd(0, 60); notaAdd(1175, 160); notaAdd(0, 400);   // "atencao"

  if (!temPrevisao()) {
    snprintf(frase, sizeof(frase), "Ponto %s. Sem ônibus previsto no momento.", ponto().nomeFala);
    notaAdd(220, 600);
  } else {
    int32_t e = menorEta();
    int minutos = e / 60;
    if (ponto().pontoFinal) {                                         // ponto final: 1 linha so
      const Linha& l = ponto().linhas[ordem[0]];
      if (e < 60)
        snprintf(frase, sizeof(frase), "Ponto %s. O ônibus da linha %s, %s, está no ponto e já está saindo.",
                 ponto().nomeFala, l.num, l.destinoFala);
      else if (e <= NO_PONTO_SEG)
        snprintf(frase, sizeof(frase), "Ponto %s. O ônibus da linha %s, %s, está no ponto. Embarque liberado, saída em %d minuto%s.",
                 ponto().nomeFala, l.num, l.destinoFala, minutos, minutos == 1 ? "" : "s");
      else if (minutos >= 100) {
        char h[6]; hhmm((int32_t)relogioSeg + e, h);
        snprintf(frase, sizeof(frase), "Ponto %s. Próximo ônibus: linha %s, %s, previsto para as %s.", ponto().nomeFala, l.num, l.destinoFala, h);
      } else
        snprintf(frase, sizeof(frase), "Ponto %s. Próximo ônibus: linha %s, %s, previsão de chegada em %d minuto%s.",
                 ponto().nomeFala, l.num, l.destinoFala, minutos, minutos == 1 ? "" : "s");
    } else {                                                          // passagem: os mesmos 3 do display
      p = snprintf(frase, sizeof(frase), "Ponto %s. Próximos ônibus:", ponto().nomeFala);
      uint8_t qtd = ponto().qtdLinhas < 3 ? ponto().qtdLinhas : 3;
      for (uint8_t k = 0; k < qtd && p < (int)sizeof(frase) - 90; k++) {
        const Linha& l = ponto().linhas[ordem[k]];
        const Previsao& pv = prev[ordem[k]];
        if (!pv.n) continue;
        int m = pv.eta[0] / 60;
        if (pv.eta[0] < 60) p += snprintf(frase + p, sizeof(frase) - p, " linha %s, %s, chegando agora;", l.num, l.destinoFala);
        else                p += snprintf(frase + p, sizeof(frase) - p, " linha %s, %s, em %d minuto%s;", l.num, l.destinoFala, m, m == 1 ? "" : "s");
      }
      if (p > 0 && frase[p - 1] == ';') frase[p - 1] = '.';
    }
    // codigo sonoro do PRIMEIRO onibus (o mesmo do LED): bipe longo = 10 min, curto = 1 min
    if (e < 60 || (ponto().pontoFinal && e <= NO_PONTO_SEG)) { for (uint8_t i = 0; i < 3; i++) { notaAdd(1568, 100); notaAdd(0, 80); } }
    else if (minutos >= 100) notaAdd(220, 600);
    else {
      for (int i = 0; i < minutos / 10; i++) { notaAdd(440, 500); notaAdd(0, 150); }
      for (int i = 0; i < minutos % 10; i++) { notaAdd(988, 120); notaAdd(0, 130); }
    }
  }
  filaIniciar();
  Serial.printf("[audio] %s\n", frase);
  publicar("tts", frase, false);
  contAudio[horaAtual()]++; iobMudou = true;
  publicarEvento("audio");
}

// ---------------------------------------------------------------- entradas
void acordarTela() {
  ultimaPresenca = millis();
  if (!telaAcesa) { lcd.backlight(); telaAcesa = true; }
}

void trocarPonto(uint8_t novo) {
  pontoAtual = novo % QTD_PONTOS;
  memset(gps, 0, sizeof(gps));                     // GPS era do ponto anterior
  calcularPrevisoes();
  publicarStatus();
  publicarEvento("troca_ponto");
}

bool pirAntes = false, btnAudioAntes = false, btnPontoAntes = false;
uint32_t tBtn = 0;

void lerEntradas() {
  // PIR: conta cada vez que alguem aparece (borda de subida), nao o tempo parado
  bool pir = digitalRead(PIN_PIR);
  if (pir && !pirAntes) {
    acordarTela();
    contPresenca[horaAtual()]++; iobMudou = true;
    publicarEvento("presenca");
  }
  if (pir) ultimaPresenca = millis();
  pirAntes = pir;

  if (millis() - tBtn > 60) {                       // debounce simples
    bool a = digitalRead(PIN_BTN_AUDIO) == LOW;     // botoes usam pull-up: apertado = LOW
    bool p = digitalRead(PIN_BTN_PONTO) == LOW;
    if (a && !btnAudioAntes) { tBtn = millis(); acordarTela(); if (!tocando) falar(); }
    if (p && !btnPontoAntes) { tBtn = millis(); acordarTela(); filaQtd = 0; notaAdd(660, 80); filaIniciar(); trocarPonto(pontoAtual + 1); }
    if (a != btnAudioAntes || p != btnPontoAntes) tBtn = millis();   // qualquer mudanca (apertar ou soltar) rearma o debounce
    btnAudioAntes = a; btnPontoAntes = p;
  }

  if (telaAcesa && millis() - ultimaPresenca > TELA_DORME_MS) {   // economia de energia
    lcd.noBacklight(); telaAcesa = false;
  }

  // transito: potenciometro manda, a nao ser que o backend tenha enviado um valor
  int pot = map(analogRead(PIN_POT), 0, 4095, 0, 100);
  if (potAnterior < 0) potAnterior = pot;
  if (abs(pot - potAnterior) > 3) { potAnterior = pot; transitoRemoto = false; }
  if (!transitoRemoto) transitoPct = pot;
}

// ---------------------------------------------------------------- relogio
uint32_t ultimoTick = 0, tNtp = 0;
void atualizarRelogio() {
  uint32_t agora = millis();
  uint32_t dt = agora - ultimoTick; ultimoTick = agora;
  modoDemo = digitalRead(PIN_SW_DEMO) == LOW;
  relogioSeg += dt / 1000.0 * (modoDemo ? 60 : 1);   // demo: 1 segundo vale 1 minuto
  if (relogioSeg >= 86400) relogioSeg -= 86400;

  // modo real: acerta pelo horario da internet (NTP) a cada 2 s, se tiver rede
  if (!modoDemo && agora - tNtp > 2000) {
    tNtp = agora;
    struct tm t;
    if (WiFi.status() == WL_CONNECTED && getLocalTime(&t, 0))
      relogioSeg = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  }
}

// ---------------------------------------------------------------- MQTT: receber comandos do backend
// topico .../cmd   payloads:  TRANSITO=0..100 | PONTO=<indice> | AVISO=<texto> (vazio limpa)
// topico .../gps  payload: "5757-51=4,19,34;607C-10=2,9"  (minutos ate as proximas chegadas)
void receberGps(const char* msg) {
  uint8_t atualizadas = 0;
  const char* c = msg;
  while (*c) {
    char nome[12]; uint8_t i = 0;
    while (*c && *c != '=' && *c != ';' && i < sizeof(nome) - 1) nome[i++] = *c++;
    nome[i] = 0;
    Gps g = {{0}, 0, millis()};
    if (*c == '=') {
      c++;
      while (*c && *c != ';') {
        char* fim;
        long v = strtol(c, &fim, 10);
        if (fim == c) { c++; continue; }                     // nao era numero: pula o caractere
        c = fim;
        if (g.n < QTD_ETA && v >= 0 && v <= 600) g.min[g.n++] = (int16_t)v;   // so minutos plausiveis
        if (*c == ',') c++;
      }
    }
    if (*c == ';') c++;
    for (uint8_t k = 0; k < ponto().qtdLinhas && k < MAX_LINHAS; k++)
      if (g.n && strcmp(ponto().linhas[k].num, nome) == 0) { gps[k] = g; atualizadas++; }
  }
  Serial.printf("[gps] %d linha(s) com previsao do Olho Vivo\n", atualizadas);
}

void aoReceber(char* topico, byte* dados, unsigned int tam) {
  if (strstr(topico, "/gps")) {
    static char g[400];
    if (tam >= sizeof(g)) tam = sizeof(g) - 1;
    memcpy(g, dados, tam); g[tam] = 0;
    receberGps(g);
    return;
  }
  char msg[64];
  if (tam >= sizeof(msg)) tam = sizeof(msg) - 1;
  memcpy(msg, dados, tam); msg[tam] = 0;
  Serial.printf("[cmd] %s\n", msg);

  if (strncmp(msg, "TRANSITO=", 9) == 0) {
    transitoPct = constrain(atoi(msg + 9), 0, 100); transitoRemoto = true;
  } else if (strncmp(msg, "PONTO=", 6) == 0) {
    trocarPonto(atoi(msg + 6));
  } else if (strncmp(msg, "AVISO=", 6) == 0) {
    uint8_t i = 0;
    for (const char* c = msg + 6; *c && i < 18; c++)          // LCD: so ASCII maiusculo
      if (*c >= 32 && *c < 127 && *c != '"' && *c != '\\') aviso[i++] = toupper(*c);   // sem aspas/barra: nao quebra o JSON
    aviso[i] = 0;
  }
  acordarTela();
}

uint32_t tMqtt = 0, tTele = 0, tIob = 0;
bool jaTentouMqtt = false;

void rede() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (jaTentouMqtt && millis() - tMqtt < 10000) return;      // tenta de novo a cada 10 s
    jaTentouMqtt = true; tMqtt = millis();
    char id[24];
    snprintf(id, sizeof(id), TOTEM_ID "-%04x", (unsigned)random(0xffff));
    // "testamento": se o totem cair, o broker avisa sozinho que ele ficou offline
    if (mqtt.connect(id, NULL, NULL, TOPICO "/status", 1, true, "{\"online\":false}")) {
      mqtt.subscribe(TOPICO "/cmd");
      mqtt.subscribe(TOPICO "/gps");
      publicarStatus(); publicarIoB();
      Serial.println("[mqtt] conectado");
    }
    return;
  }
  mqtt.loop();
  if (millis() - tTele >= 5000) { tTele = millis(); publicarTelemetria(); }
  if (iobMudou || millis() - tIob >= 30000) { tIob = millis(); publicarIoB(); }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_AUDIO, INPUT_PULLUP);
  pinMode(PIN_BTN_PONTO, INPUT_PULLUP);
  pinMode(PIN_SW_DEMO, INPUT_PULLUP);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);

  lcd.init(); lcd.backlight();
  linhaLCD(0, "       S.B.P.");
  linhaLCD(1, "  SMART BUS POINT");
  linhaLCD(2, "  TOTEM " TOTEM_ID);
  linhaLCD(3, "CONECTANDO WIFI...");
  led(0, 0, 1);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  for (uint8_t i = 0; i < 50 && WiFi.status() != WL_CONNECTED; i++) delay(200);   // espera ate 10 s
  bool online = WiFi.status() == WL_CONNECTED;
  linhaLCD(3, online ? "WIFI OK" : "SEM REDE: MODO GTFS");
  Serial.println(online ? "[wifi] conectado" : "[wifi] sem rede - seguindo offline com a grade GTFS");

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com");   // horario de Brasilia
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceber);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(3);
  delay(800);

  ultimoTick = millis(); ultimaPresenca = millis();
  calcularPrevisoes();
}

uint32_t tTela = 0;
void loop() {
  atualizarRelogio();
  lerEntradas();
  tocarFila();
  if (millis() - tTela >= 250) {
    tTela = millis();
    calcularPrevisoes();
    atualizarLed();
    desenhar();
  }
  rede();
  delay(10);
}
