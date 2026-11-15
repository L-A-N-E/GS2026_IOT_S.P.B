// GERADO AUTOMATICAMENTE por tools/gtfs_to_totem.py - nao editar a mao
// Fonte: GTFS SPTrans (https://www.sptrans.com.br/desenvolvedores/)
#pragma once
#include <stdint.h>

#define MAX_REFS 3

struct Linha {
  const char* num;          // ex: 5757-51
  const char* destino;      // letreiro do onibus (sem acento, pro LCD)
  const char* destinoFala;  // mesmo texto com acento, pro audio (TTS)
  const char* destinoCurto; // ate 9 letras, pra lista do LCD
  uint16_t offsetMin;       // minutos da origem ate este ponto
  uint8_t headway[24];      // intervalo (min) por hora do dia; 0 = sem servico
  const char* refs[MAX_REFS];
};

struct Ponto {
  const char* id;
  const char* nome;         // sem acento, pro LCD
  const char* nomeFala;     // com acento, pro audio (TTS)
  const char* nomeCurto;    // ate 14 letras, cabe ao lado do relogio no LCD
  float lat, lon;
  bool pontoFinal;          // true = ponto final (todas as linhas saem daqui)
  uint8_t qtdLinhas;
  const Linha* linhas;
};

// METRO CONCEICAO (1 linhas)
const Linha LINHAS_3703094[] = {
  {"5757-51", "PEDREIRA", "Pedreira", "PEDREIRA", 0, {60,0,0,0,0,20,12,12,10,15,15,15,15,15,15,15,12,12,15,12,15,15,15,20}, {"AV. BENIGNO CARRERA","LUIS DE MONTALVOR","AV. ANGELO CRISTIANINI"}},
};

// AV. CUPECE, 4734 (12 linhas)
const Linha LINHAS_2203206[] = {
  {"607C-10", "JD. MIRIAM", "Jardim Miriam", "JD MIRIAM", 70, {60,0,0,0,60,15,5,4,4,4,4,6,5,7,6,5,4,3,4,6,8,12,12,15}, {"ESTACAO DE TRANSF. JD. MIRIAM","PARADA BRAS DE ABREU","ASSEMBLEIA - C/B"}},
  {"5012-10", "VL. GUACURI", "Vila Guacuri", "VL GUACUR", 33, {30,0,0,0,60,12,6,5,6,7,8,10,10,10,10,8,6,6,6,10,10,12,15,20}, {"R. CARDOSO DE MELO NETO","AV. ALDO JOAO RINALDI","AV. ALDA"}},
  {"5757-10", "CID. JULIA", "Cidade Júlia", "CID JULIA", 59, {30,0,0,0,60,15,6,6,6,8,10,10,10,10,10,8,6,7,7,8,12,15,15,20}, {"CUPECE","R. ALCIDES ARGUEDAS","AV. ANGELO CRISTIANINI"}},
  {"516N-10", "PCA. D. GASTAO", "Praça D. Gastão", "PCA D GAS", 147, {0,0,0,0,15,7,5,7,10,7,10,10,10,10,10,8,6,8,8,10,15,20,20,60}, {"ESTACAO DE TRANSF. JD. MIRIAM","CUPECE","AV. ANGELO CRISTIANINI"}},
  {"5702-10", "REFUGIO STA. TEREZINHA", "Refúgio Santa Terezinha", "REFUGIO S", 37, {30,0,0,0,30,10,7,6,7,8,10,10,12,12,10,10,8,7,10,12,15,15,20,20}, {"AV. SEN. VITORINO FREIRE","AV. ANGELO CRISTIANINI","R. GUAICURI"}},
  {"5123-10", "JD. MIRIAM", "Jardim Miriam", "JD MIRIAM", 96, {60,0,0,0,0,20,10,6,6,10,10,12,12,12,12,10,8,8,8,10,12,12,15,15}, {"AV. CUPECE","PCA. BCHEL. FERNANDO BRAGA PEREIRA DA ROCHA","AV. GARCIA DE AVILA"}},
  {"5015-10", "JD. SAO JORGE", "Jardim São Jorge", "JD SAO JO", 28, {60,0,0,0,30,15,8,6,10,10,15,15,12,12,12,12,15,6,12,12,12,12,20,30}, {"AV. YERVANT KISSAJIKIAN","R. PEIXOTO MELO FILHO","R. BENTO JOSE DE MORAES"}},
  {"6358-10", "JD. LUSO", "Jardim Luso", "JD LUSO", 94, {30,0,0,0,0,20,12,10,7,10,10,12,12,12,12,10,7,7,10,15,15,15,15,20}, {"ASSEMBLEIA - C/B","R. PROF. FREITAS JULIAO","AV. ANGELO CRISTIANINI"}},
  {"5128-10", "JD. APURA", "Jardim Apurá", "JD APURA", 50, {20,0,0,0,60,20,12,7,7,10,15,15,15,15,12,12,10,10,10,12,15,12,15,15}, {"AV. ANGELO CRISTIANINI","ESTR. DO ALVARENGA","R. SALVADOR DALI"}},
  {"5129-10", "JD. MIRIAM", "Jardim Miriam", "JD MIRIAM", 80, {60,0,0,0,60,15,10,10,10,15,12,12,12,12,12,10,10,10,10,12,15,12,12,20}, {"ESTACAO DE TRANSF. JD. MIRIAM","AV. CUPECE",""}},
  {"5757-51", "PEDREIRA", "Pedreira", "PEDREIRA", 52, {60,0,0,0,0,20,12,12,10,15,15,15,15,15,15,15,12,12,15,12,15,15,15,20}, {"R. PROF. FREITAS JULIAO","AV. ANGELO CRISTIANINI","ESTR. DO ALVARENGA"}},
  {"5018-10", "SHOP. INTERLAGOS", "Shopping Interlagos", "SHOP INTE", 40, {0,0,0,0,60,20,12,12,12,20,20,15,15,15,15,15,15,15,15,15,20,15,20,20}, {"R. PAPA GREGORIO MAGNO","AV. AUGUSTO DE CASTRO","R. ANTONIO DO CAMPO"}},
};

const uint8_t QTD_PONTOS = 2;
const Ponto PONTOS[] = {
  {"3703094", "METRO CONCEICAO", "Metrô Conceição", "MET CONCEICAO", -23.635117, -46.640989, true, 1, LINHAS_3703094},
  {"2203206", "AV. CUPECE, 4734", "Avenida Cupece, 4734", "AV CUPECE 4734", -23.675470, -46.642507, false, 12, LINHAS_2203206},
};
