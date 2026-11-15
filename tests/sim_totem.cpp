// Simulador de host: compila o sketch.ino REAL com mocks do Arduino e roda um
// roteiro de uso (presenca, botoes, transito, comandos MQTT, queda de rede).
// Rodar: g++ -std=c++17 -Imock -I../wokwi sim_totem.cpp -o sim && ./sim
#include "Arduino.h"
#include <algorithm>
#include "../wokwi/sketch.ino"

static int falhas = 0;
#define CHECK(cond, nome) do { if (cond) printf("  ok   %s\n", nome); else { printf("  FALHOU %s\n", nome); falhas++; } } while (0)
static void rodar(uint32_t ms) { uint32_t fim = millis() + ms; while (millis() < fim) loop(); }
static void tela(const char* titulo) {
  printf("\n--- %s  [luz %s | LED %s%s%s | buzzer %d Hz]\n", titulo, lcd.luz ? "on" : "OFF",
         sim::pinos[PIN_LED_R] ? "R" : "-", sim::pinos[PIN_LED_G] ? "G" : "-", sim::pinos[PIN_LED_B] ? "B" : "-", sim::toneFreq);
  for (auto& l : lcd.tela) printf("  |%s|\n", l);
}
static bool telaTem(const char* txt) { for (auto& l : lcd.tela) if (strstr(l, txt)) return true; return false; }
static void apertar(int pino) { sim::pinos[pino] = LOW; rodar(120); sim::pinos[pino] = HIGH; rodar(120); }
static int contar(const char* sufixo, const char* trecho = "") {
  int n = 0; for (auto& p : sim::publicados) if (p.first.size() >= strlen(sufixo) && p.first.compare(p.first.size() - strlen(sufixo), strlen(sufixo), sufixo) == 0 && p.second.find(trecho) != std::string::npos) n++;
  return n;
}

int main() {
  setup();
  rodar(1500); tela("1. boot, relogio fallback 17:55, ponto final Metro Conceicao");
  CHECK(telaTem("MET CONCEICAO  17:55") && telaTem("5757-51 > PEDREIRA") && telaTem("SAI EM 4 MIN") && telaTem("DEPOIS: 19 E 34 MIN"), "ponto final: 4 linhas fixas");
  char fotoTela[4][21]; memcpy(fotoTela, lcd.tela, sizeof fotoTela); rodar(9000);
  CHECK(memcmp(fotoTela[1], lcd.tela[1], 21) == 0 && memcmp(fotoTela[3], lcd.tela[3], 21) == 0, "nada rola/troca sozinho em 9 s (so os numeros mudam)");
  CHECK(sim::pinos[PIN_LED_R] && sim::pinos[PIN_LED_G], "LED amarelo com 4 min");
  CHECK(contar("/status", "\"online\":true") == 1, "publicou status retido ao conectar");

  sim::pinos[PIN_PIR] = HIGH; rodar(300); sim::pinos[PIN_PIR] = LOW; rodar(300);
  CHECK(contar("/evento", "presenca") == 1 && contPresenca[17] == 1, "PIR -> evento presenca + contador da hora 17");

  apertar(PIN_BTN_AUDIO); tela("2. botao de audio apertado");
  CHECK(contar("/tts", "Próximo ônibus: linha 5757-51, Pedreira, previsão de chegada em 4 minutos") == 1, "frase TTS igual a do documento");
  CHECK(sim::pinos[PIN_LED_B] && tocando, "LED azul e buzzer tocando");
  rodar(4000); CHECK(!tocando && sim::toneFreq == 0, "buzzer termina sozinho (4 bipes curtos)");

  sim::pinos[PIN_SW_DEMO] = LOW; rodar(3300); tela("3. demo acelerada (1 s = 1 min) -> onibus no ponto");
  CHECK(telaTem("NO PONTO - EMBARQUE"), "ponto final: onibus no ponto <= 2 min antes da saida");
  CHECK(!sim::pinos[PIN_LED_R] && sim::pinos[PIN_LED_G], "LED verde");
  sim::pinos[PIN_SW_DEMO] = HIGH;

  int32_t etaLivre = menorEta();
  sim::adc = 4095; rodar(600); tela("4. potenciometro no maximo (transito intenso)");
  CHECK(transitoPct == 100 && menorEta() >= etaLivre + 470 && sim::pinos[PIN_LED_R], "transito 100% aumenta a espera em ~8 min e muda o LED");
  sim::adc = 0; rodar(300);
  mqtt.injetar("TRANSITO=50"); rodar(300);
  CHECK(transitoPct == 50 && transitoRemoto, "comando MQTT TRANSITO=50 sobrepoe o potenciometro");
  mqtt.injetar("AVISO=linha com desvio"); rodar(3200); tela("5. aviso vindo do backend");
  CHECK(telaTem("! LINHA COM DESVIO"), "aviso fica fixo no cabecalho");
  mqtt.injetar("AVISO=a\"b\\c"); CHECK(strcmp(aviso, "ABC") == 0, "aviso com aspas/barra e filtrado (nao quebra o JSON)");
  mqtt.injetar("AVISO="); mqtt.injetar("TRANSITO=0");

  apertar(PIN_BTN_PONTO); rodar(600); tela("6. botao azul: totem agora em ponto de passagem (Av. Cupece)");
  CHECK(pontoAtual == 1 && !ponto().pontoFinal, "trocou para ponto de passagem");
  CHECK(contar("/evento", "troca_ponto") == 1 && contar("/status", "2203206") == 1, "evento + status do novo ponto");
  CHECK(telaTem("AV CUPECE 4734"), "cabecalho com nome curto do ponto de passagem");
  bool ordenado = true, formato = true, ledOk = true;
  for (int i = 0; i < 60; i++) {
    rodar(1000);
    for (int r = 1; r < 3; r++) if (prev[ordem[r - 1]].eta[0] > prev[ordem[r]].eta[0]) ordenado = false;
    for (int r = 1; r <= 3; r++) if (!(lcd.tela[r][4] == ' ' && lcd.tela[r][14] == ' ')) formato = false;
    int m = prev[ordem[0]].eta[0] / 60;
    if ((m <= 3) != (bool)sim::pinos[PIN_LED_G] || (m > 3) != (bool)sim::pinos[PIN_LED_R]) ledOk = false;
  }
  tela("7. ponto de passagem: 3 proximos onibus, colunas fixas");
  CHECK(ordenado && formato, "lista: 3 proximos em ordem, colunas sempre no lugar (60 s)");
  CHECK(ledOk, "LED sempre igual ao onibus da 1a linha");
  apertar(PIN_BTN_AUDIO); rodar(100);
  { std::string f = sim::publicados.back().first.find("/evento") != std::string::npos ? sim::publicados[sim::publicados.size() - 2].second : sim::publicados.back().second;
    for (auto& pp : sim::publicados) if (pp.first.find("/tts") != std::string::npos) f = pp.second;
    printf("  [tts] %s\n", f.c_str());
    CHECK(f.find("Próximos ônibus:") != std::string::npos && std::count(f.begin(), f.end(), ';') == 2, "audio na passagem fala os 3 proximos"); }
  rodar(9000);
  rodar(31000); tela("8. 30 s sem ninguem");
  CHECK(!lcd.luz, "tela apaga apos 30 s sem presenca");
  sim::pinos[PIN_PIR] = HIGH; rodar(200); sim::pinos[PIN_PIR] = LOW;
  CHECK(lcd.luz, "PIR acorda a tela");

  sim::mqttOk = false; rodar(500); size_t antes = sim::publicados.size();
  apertar(PIN_BTN_AUDIO); rodar(6000); tela("9. broker caiu: totem segue funcionando offline");
  CHECK(sim::publicados.size() == antes && pontoAtual == 1, "offline: nada publicado, tela continua");
  CHECK(contAudio[horaAtual()] >= 1, "offline: uso do botao continua sendo contado");
  sim::mqttOk = true; rodar(11000);
  CHECK(contar("/iob", "\"audio\":[") >= 2, "rede voltou: contadores IoB reenviados");

  sim::ntpOk = true; sim::ntp.tm_hour = 7; sim::ntp.tm_min = 30; sim::ntp.tm_sec = 0; rodar(2500);
  CHECK(horaAtual() == 7, "NTP acerta o relogio (07:30)");
  apertar(PIN_BTN_PONTO); sim::ntpOk = false; sim::pinos[PIN_PIR] = HIGH; rodar(100);
  relogioSeg = 1 * 3600 + 30 * 60; rodar(400); tela("10. madrugada 01:30 no ponto final");
  CHECK(telaTem("PROXIMO AS 05:00") && sim::pinos[PIN_LED_R] && !sim::pinos[PIN_LED_G], "madrugada: 'PROXIMO AS 05:00' e LED vermelho");

  // GPS do Olho Vivo: totem no ponto final, backend manda previsao real
  relogioSeg = 18 * 3600 + 2 * 60; trocarPonto(0); rodar(300);
  mqtt.injetar("5757-51=7,21,35;9999-99=1", "sbp-gs2026/totem/sbp-0001/gps"); rodar(400); tela("11. previsao por GPS (Olho Vivo)");
  CHECK(telaTem("SAI EM 7 MIN  (GPS)") && prev[0].gps, "GPS substitui a grade e o display avisa (GPS)");
  sim::adc = 4095; rodar(400);
  CHECK(prev[0].eta[0] / 60 == 6 || prev[0].eta[0] / 60 == 7, "com GPS o potenciometro de transito nao soma atraso");
  sim::adc = 0; rodar(300);
  mqtt.injetar("lixo;=;5757-51=abc,-3,999;;", "sbp-gs2026/totem/sbp-0001/gps"); rodar(300);
  CHECK(prev[0].gps && prev[0].eta[0] / 60 >= 6, "mensagem GPS com lixo e ignorada sem quebrar");
  rodar(61000);
  CHECK(!prev[0].gps && !telaTem("(GPS)"), "sem GPS por 60 s volta sozinho pra grade GTFS");
  mqtt.injetar("5757-51=3", "sbp-gs2026/totem/sbp-0001/gps"); rodar(300); trocarPonto(1); rodar(300);
  CHECK(!prev[ordem[0]].gps, "trocar de ponto descarta o GPS do ponto anterior");

  FILE* f = fopen("/tmp/mqtt_publicados.jsonl", "w");
  for (auto& p : sim::publicados) fprintf(f, "%s\t%s\n", p.first.c_str(), p.second.c_str());
  fclose(f);
  size_t maior = 0; for (auto& p : sim::publicados) if (p.second.size() > maior) maior = p.second.size();
  printf("\n%zu mensagens MQTT publicadas, maior payload %zu bytes (buffer 1024)\n", sim::publicados.size(), maior);
  CHECK(maior < 900, "payload cabe no buffer MQTT");
  printf(falhas ? "\n%d FALHA(S)\n" : "\nTODOS OS TESTES PASSARAM\n", falhas);
  return falhas ? 1 : 0;
}
