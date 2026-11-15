// Testes da logica de previsao (eta.h) contra a grade real do GTFS.
// Rodar: g++ -std=c++17 -I../wokwi test_eta.cpp -o test_eta && ./test_eta
#include <cstdio>
#include <cstdlib>
#include <string>
#include "eta.h"
static int falhas = 0;
#define CHECK(cond, nome) do { if (cond) printf("  ok   %s\n", nome); else { printf("  FALHOU %s\n", nome); falhas++; } } while (0)
static int32_t hms(int h, int m, int s = 0) { return h * 3600 + m * 60 + s; }

int main() {
  const Linha& conc = PONTOS[0].linhas[0];           // 5757-51 no Metro Conceicao (ponto final)
  int32_t e[3]; uint8_t n;

  n = proximasChegadas(conc, hms(18, 0), 0, e, 3);   // 18h: intervalo de 15 min
  CHECK(n == 3 && e[0] == 0 && e[1] == 900 && e[2] == 1800, "18:00 -> agora, 15 e 30 min");
  n = proximasChegadas(conc, hms(18, 1), 0, e, 3);
  CHECK(e[0] == 14 * 60, "18:01 -> 14 min");
  n = proximasChegadas(conc, hms(17, 50), 0, e, 3);  // 17h: intervalo 12 -> ultima 17:48, proxima 18:00
  CHECK(e[0] == 10 * 60 && e[1] == 25 * 60, "17:50 -> 10 min (virada de faixa 12->15)");
  n = proximasChegadas(conc, hms(1, 30), 0, e, 3);   // madrugada: sem servico ate 05:00
  CHECK(n == 3 && e[0] == 210 * 60, "01:30 -> primeiro onibus as 05:00");
  n = proximasChegadas(conc, hms(23, 50), 0, e, 3);  // vira o dia: 23:40 passou, proximo 00:00
  CHECK(e[0] == 10 * 60, "23:50 -> 00:00 do dia seguinte");
  n = proximasChegadas(conc, hms(18, 1), atrasoPorTransito(0, 100), e, 3);
  CHECK(e[0] == 22 * 60, "transito 100% no ponto final as 18:01: 14 min da grade + 8 de atraso");
  bool mono = true;                                  // mais transito nunca pode diminuir a espera
  for (int p = 0; p < QTD_PONTOS; p++) for (int i = 0; i < PONTOS[p].qtdLinhas; i++) for (int t = 0; t < 86400; t += 300) {
    int32_t ant = -1;
    for (int pct = 0; pct <= 100; pct += 10) {
      proximasChegadas(PONTOS[p].linhas[i], t, atrasoPorTransito(PONTOS[p].linhas[i].offsetMin, pct), e, 3);
      if (e[0] < ant) mono = false;
      ant = e[0];
    }
  }
  CHECK(mono, "mais transito nunca diminui a espera (todas as linhas, 24h)");

  const Linha* l51 = nullptr;                        // mesma linha vista da Av. Cupece (52 min depois)
  for (int i = 0; i < PONTOS[1].qtdLinhas; i++)
    if (std::string(PONTOS[1].linhas[i].num) == "5757-51") l51 = &PONTOS[1].linhas[i];
  CHECK(l51 && l51->offsetMin == 52, "Cupece: 5757-51 passa 52 min apos sair da Conceicao");
  n = proximasChegadas(*l51, hms(18, 52), 0, e, 3);
  CHECK(e[0] == 0, "Cupece 18:52 -> onibus das 18:00 chegando");
  n = proximasChegadas(*l51, hms(0, 10), 0, e, 3);   // onibus de ONTEM ainda a caminho: 23:20->00:12, 23:40->00:32
  CHECK(e[0] == 2 * 60 && e[1] == 22 * 60, "Cupece 00:10 -> onibus que sairam ontem 23:20 e 23:40 (2 e 22 min)");

  Linha vazia = {"0000-00", "X", "X", "X", 0, {0}, {"", "", ""}};
  CHECK(proximasChegadas(vazia, hms(12, 0), 0, e, 3) == 0, "linha sem servico -> 0 previsoes");
  CHECK(onibusPorHora(PONTOS[0], 18) == 4, "Conceicao 18h: 4 onibus/hora");
  CHECK(divPiso(-1, 3600) == -1 && divPiso(-3600, 3600) == -1 && divPiso(7199, 3600) == 1, "divPiso");

  for (int p = 0; p < QTD_PONTOS; p++)               // varredura: 24h, todo minuto, todas as linhas
    for (int i = 0; i < PONTOS[p].qtdLinhas; i++)
      for (int t = 0; t < 86400; t += 60) {
        n = proximasChegadas(PONTOS[p].linhas[i], t, 0, e, 3);
        if (n != 3 || e[0] < 0 || e[0] > e[1] || e[1] > e[2] || e[0] > 6 * 3600) { printf("  FALHOU varredura p%d l%d t%d\n", p, i, t); falhas++; }
      }
  printf("  ok   varredura 24h x todas as linhas (ordem crescente, nunca negativo)\n");
  printf(falhas ? "\n%d FALHA(S)\n" : "\nTODOS OS TESTES PASSARAM\n", falhas);
  return falhas ? 1 : 0;
}
