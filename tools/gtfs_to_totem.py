"""
gtfs_to_totem.py  -  S.B.P. Smart Bus Point
Gera o arquivo totem_data.h (usado pelo ESP32) a partir do GTFS oficial da SPTrans.

Por que existe: o ESP32 nao tem memoria nem internet garantida pra ler o GTFS
inteiro (70 MB). Entao este script "destila" so o que um totem precisa:
  - nome do ponto
  - linhas que param nele (numero + destino)
  - quanto tempo o onibus leva da origem ate esse ponto (offset)
  - de quanto em quanto tempo sai um onibus em cada hora do dia (headway)
  - pontos de referencia que a linha atravessa depois desse ponto

Uso:
  python gtfs_to_totem.py <pasta_gtfs> <stop_id> [<stop_id> ...] -o totem_data.h

Exemplo (os dois totens da demo):
  python gtfs_to_totem.py ./gtfs 3703094 2203206 -o ../wokwi/totem_data.h

Fonte dos dados: SPTrans - GTFS (https://www.sptrans.com.br/desenvolvedores/)
"""
import argparse
import csv
import os
import sys
import unicodedata
from collections import defaultdict

MAX_LINHAS_POR_PONTO = 12   # limite pra caber na memoria e na tela do totem
MAX_REFS = 3                # pontos de referencia por linha
PALAVRAS_REFERENCIA = ("METRO", "TERM", "HOSP", "SHOP", "ESTACAO", "CPTM",
                       "PARQUE", "PRACA", "UNIV", "FAC", "ESCOLA", "MERCADO")


def ascii_upper(txt):
    """O LCD 20x4 nao tem acento: 'Metrô Conceição' -> 'METRO CONCEICAO'."""
    txt = unicodedata.normalize("NFKD", txt).encode("ascii", "ignore").decode()
    return txt.upper().replace('"', "'")


ABREV = {"Jd.": "Jardim", "Term.": "Terminal", "Av.": "Avenida", "R.": "Rua",
         "Vl.": "Vila", "Cid.": "Cidade", "Pca.": "Praça", "Pça.": "Praça",
         "Shop.": "Shopping", "Hosp.": "Hospital", "Estr.": "Estrada",
         "Pq.": "Parque", "Sta.": "Santa", "Sto.": "Santo", "Eng.": "Engenheiro",
         "Prof.": "Professor", "Sen.": "Senador", "Cap.": "Capitão"}


def para_fala(txt):
    """Versao para o sintetizador de voz (TTS): mantem acento e expande
    abreviacoes, senao a voz le 'Jd.' como 'jota de ponto'."""
    return " ".join(ABREV.get(p, p) for p in txt.replace('"', "'").split())


ABREV_LCD = {"METRO": "MET", "ESTACAO": "EST", "TERMINAL": "TERM", "AVENIDA": "AV",
             "PRACA": "PCA", "SHOPPING": "SHOP", "JARDIM": "JD", "VILA": "VL"}


def curto(txt, limite):
    """Nome que cabe no LCD sem cortar no meio da palavra quando der:
    tira pontuacao, abrevia palavras comuns e so entao corta."""
    t = " ".join(ascii_upper(txt).replace(".", " ").replace(",", " ").split())
    if len(t) > limite:
        t = " ".join(ABREV_LCD.get(p, p) for p in t.split())
    return t[:limite].rstrip()


def hms_para_seg(hms):
    h, m, s = (int(x) for x in hms.split(":"))
    return h * 3600 + m * 60 + s


def ler_csv(pasta, nome):
    with open(os.path.join(pasta, nome), encoding="utf-8-sig") as f:
        yield from csv.DictReader(f)


def carregar(pasta):
    stops = {r["stop_id"]: r for r in ler_csv(pasta, "stops.txt")}
    trips = {r["trip_id"]: r for r in ler_csv(pasta, "trips.txt")}
    routes = {r["route_id"]: r for r in ler_csv(pasta, "routes.txt")}

    # sequencia de paradas de cada viagem
    seq = defaultdict(list)
    for r in ler_csv(pasta, "stop_times.txt"):
        seq[r["trip_id"]].append((int(r["stop_sequence"]), r["stop_id"],
                                  hms_para_seg(r["arrival_time"])))
    for t in seq:
        seq[t].sort()

    # headway (segundos) por hora do dia, por viagem
    freq = defaultdict(lambda: [0] * 24)
    for r in ler_csv(pasta, "frequencies.txt"):
        hora = (hms_para_seg(r["start_time"]) // 3600) % 24
        freq[r["trip_id"]][hora] = int(r["headway_secs"])
    return stops, trips, routes, seq, freq


def escolher_referencias(paradas_depois, stops):
    """Pega ate MAX_REFS paradas 'marcantes' (metro, terminal, hospital...)
    depois do ponto. Se nao tiver, escolhe ruas espalhadas no trajeto."""
    nomes, vistos = [], set()
    for sid in paradas_depois:
        n = ascii_upper(stops[sid]["stop_name"])
        if any(p in n for p in PALAVRAS_REFERENCIA) and n not in vistos:
            nomes.append(n); vistos.add(n)
    # completa com avenidas/ruas distintas ao longo do caminho
    if len(nomes) < MAX_REFS and paradas_depois:
        passo = max(1, len(paradas_depois) // (MAX_REFS + 1))
        for sid in paradas_depois[passo::passo]:
            n = ascii_upper(stops[sid]["stop_name"]).split(",")[0].strip()
            if n not in vistos:
                nomes.append(n); vistos.add(n)
            if len(nomes) >= MAX_REFS:
                break
    return nomes[:MAX_REFS]


def montar_ponto(stop_id, stops, trips, routes, seq, freq):
    if stop_id not in stops:
        sys.exit(f"stop_id {stop_id} nao existe no GTFS")
    linhas = []
    for trip_id, paradas in seq.items():
        ids = [p[1] for p in paradas]
        if stop_id not in ids:
            continue
        i = ids.index(stop_id)
        if i == len(ids) - 1:
            continue  # ultimo ponto da viagem: onibus so desembarca, nao serve pro totem
        t = trips[trip_id]
        r = routes[t["route_id"]]
        offset = paradas[i][2] - paradas[0][2]   # tempo origem -> este ponto
        hw = [round(h / 60) for h in freq[trip_id]]  # headway em minutos
        if not any(hw):
            continue
        linhas.append({
            "num": r["route_short_name"],
            "destino": ascii_upper(t["trip_headsign"]),
            "destino_fala": para_fala(t["trip_headsign"]),
            "destino_curto": curto(t["trip_headsign"], 9),
            "offset_min": round(offset / 60),
            "headway": hw,
            "refs": escolher_referencias(ids[i + 1:], stops),
            "cor": r.get("route_color") or "000000",
        })
    # prioriza as linhas com mais onibus por dia
    linhas.sort(key=lambda l: -sum(60 // h for h in l["headway"] if h))
    linhas = linhas[:MAX_LINHAS_POR_PONTO]
    s = stops[stop_id]
    return {
        "id": stop_id,
        "nome": ascii_upper(s["stop_name"]),
        "nome_fala": para_fala(s["stop_name"]),
        "nome_curto": curto(s["stop_name"], 14),
        "lat": float(s["stop_lat"]),
        "lon": float(s["stop_lon"]),
        "final": all(l["offset_min"] == 0 for l in linhas),
        "linhas": linhas,
    }


def gerar_header(pontos):
    out = []
    w = out.append
    w("// GERADO AUTOMATICAMENTE por tools/gtfs_to_totem.py - nao editar a mao")
    w("// Fonte: GTFS SPTrans (https://www.sptrans.com.br/desenvolvedores/)")
    w("#pragma once")
    w("#include <stdint.h>\n")
    w(f"#define MAX_REFS {MAX_REFS}\n")
    w("struct Linha {")
    w("  const char* num;          // ex: 5757-51")
    w("  const char* destino;      // letreiro do onibus (sem acento, pro LCD)")
    w("  const char* destinoFala;  // mesmo texto com acento, pro audio (TTS)")
    w("  const char* destinoCurto; // ate 9 letras, pra lista do LCD")
    w("  uint16_t offsetMin;       // minutos da origem ate este ponto")
    w("  uint8_t headway[24];      // intervalo (min) por hora do dia; 0 = sem servico")
    w("  const char* refs[MAX_REFS];")
    w("};\n")
    w("struct Ponto {")
    w("  const char* id;")
    w("  const char* nome;         // sem acento, pro LCD")
    w("  const char* nomeFala;     // com acento, pro audio (TTS)")
    w("  const char* nomeCurto;    // ate 14 letras, cabe ao lado do relogio no LCD")
    w("  float lat, lon;")
    w("  bool pontoFinal;          // true = ponto final (todas as linhas saem daqui)")
    w("  uint8_t qtdLinhas;")
    w("  const Linha* linhas;")
    w("};\n")
    for p in pontos:
        var = f"LINHAS_{p['id']}"
        w(f"// {p['nome']} ({len(p['linhas'])} linhas)")
        w(f"const Linha {var}[] = {{")
        for l in p["linhas"]:
            hw = ",".join(str(h) for h in l["headway"])
            refs = ",".join(f'"{x}"' for x in l["refs"]) or '""'
            while refs.count('"') < MAX_REFS * 2:
                refs += ',""'
            w(f'  {{"{l["num"]}", "{l["destino"]}", "{l["destino_fala"]}", "{l["destino_curto"]}", {l["offset_min"]}, {{{hw}}}, {{{refs}}}}},')
        w("};\n")
    w(f"const uint8_t QTD_PONTOS = {len(pontos)};")
    w("const Ponto PONTOS[] = {")
    for p in pontos:
        w(f'  {{"{p["id"]}", "{p["nome"]}", "{p["nome_fala"]}", "{p["nome_curto"]}", {p["lat"]:.6f}, {p["lon"]:.6f}, '
          f'{"true" if p["final"] else "false"}, {len(p["linhas"])}, LINHAS_{p["id"]}}},')
    w("};")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gtfs")
    ap.add_argument("stop_ids", nargs="+")
    ap.add_argument("-o", "--saida", default="totem_data.h")
    a = ap.parse_args()

    dados = carregar(a.gtfs)
    pontos = [montar_ponto(s, *dados) for s in a.stop_ids]
    with open(a.saida, "w", encoding="utf-8") as f:
        f.write(gerar_header(pontos))
    for p in pontos:
        tipo = "PONTO FINAL" if p["final"] else "PONTO DE PASSAGEM"
        print(f"[ok] {p['id']} {p['nome']} - {tipo} - {len(p['linhas'])} linhas: "
              + ", ".join(l["num"] for l in p["linhas"]))
    print(f"-> {a.saida}")


if __name__ == "__main__":
    main()
