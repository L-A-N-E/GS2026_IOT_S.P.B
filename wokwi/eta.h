// eta.h - S.B.P. Smart Bus Point
// Calculo da previsao de chegada (ETA) a partir da grade GTFS embutida.
// C++ puro, sem nada de Arduino: por isso da pra testar no PC (ver tests/).
#pragma once
#include <stdint.h>
#include "totem_data.h"

// Divisao inteira arredondando pra baixo (a do C arredonda pra zero,
// o que quebraria a conta perto da meia-noite, quando o numero fica negativo).
inline int32_t divPiso(int32_t a, int32_t b) {
  int32_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

// Atraso (em segundos) causado pelo transito.
// pct = 0..100. Quanto mais longe o onibus vem (offset), mais ele sofre.
// Os 8 min fixos cobrem o ponto final, onde o offset e zero mas o onibus
// ainda precisa voltar da viagem anterior.
inline int32_t atrasoPorTransito(uint16_t offsetMin, int pct) {
  int32_t base = (int32_t)offsetMin * 60 / 2 + 480;
  return base * pct / 100;
}

inline const char* nivelTransito(int pct) {
  if (pct < 34) return "LIVRE";
  if (pct < 67) return "MODERADO";
  return "INTENSO";
}

// Preenche out[] com quantos segundos faltam para as proximas chegadas da
// linha neste ponto. Retorna quantas previsoes achou (0 = sem servico).
//
// Como funciona: o GTFS diz "na hora H sai um onibus a cada X minutos".
// Entao as partidas da origem sao H:00, H:00+X, H:00+2X...
// A chegada programada neste ponto = partida + offset (tempo de viagem ate aqui).
// A gente varre as partidas a partir de um pouco antes de "agora" e guarda
// as primeiras cuja chegada PROGRAMADA ainda nao passou. So depois soma o
// atraso do transito no tempo que falta. Assim, mais transito sempre significa
// esperar mais (nunca "traz de volta" um onibus que ja passou).
// Olha ate 30h pra frente, entao atravessa a madrugada
// (ex.: 01:30 -> primeiro onibus das 05:00).
inline uint8_t proximasChegadas(const Linha& l, int32_t agoraSeg, int32_t atrasoSeg,
                                int32_t* out, uint8_t maxOut) {
  uint8_t n = 0;
  int32_t ate = (int32_t)l.offsetMin * 60;
  int32_t h0 = divPiso(agoraSeg - ate, 3600) - 1;
  for (int32_t h = h0; h < h0 + 30 && n < maxOut; h++) {
    uint8_t hw = l.headway[((h % 24) + 24) % 24];
    if (hw == 0) continue;                       // hora sem servico
    for (int32_t partida = h * 3600; partida < (h + 1) * 3600 && n < maxOut;
         partida += (int32_t)hw * 60) {
      int32_t chegada = partida + ate;
      if (chegada >= agoraSeg) out[n++] = chegada - agoraSeg + atrasoSeg;
    }
  }
  return n;
}

// Quantos onibus por hora passam neste ponto na hora informada (oferta).
// Usado no IoB pra comparar com a demanda (pessoas detectadas).
inline uint16_t onibusPorHora(const Ponto& p, uint8_t hora) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < p.qtdLinhas; i++) {
    uint8_t hw = p.linhas[i].headway[hora % 24];
    if (hw) total += 60 / hw;
  }
  return total;
}
