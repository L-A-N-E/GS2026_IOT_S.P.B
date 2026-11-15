"""
gtfs_to_mapa.py  -  S.B.P. Smart Bus Point
Gera painel/dados_linhas.js (aba "Explorar" do painel) a partir do GTFS da SPTrans.

O que faz:
  - pega TODAS as linhas da cidade, com os dois sentidos;
  - para cada sentido: destino, paradas em ordem, intervalo por hora e o
    desenho do trajeto (shapes.txt);
  - simplifica o desenho (algoritmo de Douglas-Peucker): o shapes.txt tem
    60 MB porque marca um ponto a cada poucos metros. Para desenhar no mapa,
    um ponto a cada curva basta;
  - grava tudo num .js (e nao .json) porque o painel abre direto do arquivo
    (duplo clique) e, nesse modo, o navegador nao deixa ler .json.

Uso:
  python gtfs_to_mapa.py <pasta_gtfs> -o ../painel/dados_linhas.js

Fonte: SPTrans - GTFS (https://www.sptrans.com.br/desenvolvedores/)
"""
import argparse
import csv
import json
import math
import os
from collections import defaultdict

TOLERANCIA_M = 12   # erro maximo aceito ao simplificar o trajeto, em metros
ESCALA = 1e5        # coordenadas com 5 casas decimais (~1 m de precisao)


def ler(pasta, nome):
    with open(os.path.join(pasta, nome), encoding="utf-8-sig") as f:
        yield from csv.DictReader(f)


def seg(hms):
    h, m, s = (int(x) for x in hms.split(":"))
    return h * 3600 + m * 60 + s


def dist_ponto_reta(p, a, b):
    """Distancia (m) do ponto p ao segmento a-b, aproximando a Terra como plana
    (vale para distancias de cidade)."""
    kx = 111320 * math.cos(math.radians(a[0]))
    ky = 110540
    px, py = (p[1] - a[1]) * kx, (p[0] - a[0]) * ky
    bx, by = (b[1] - a[1]) * kx, (b[0] - a[0]) * ky
    L = bx * bx + by * by
    t = 0 if L == 0 else max(0, min(1, (px * bx + py * by) / L))
    return math.hypot(px - t * bx, py - t * by)


def simplificar(pts, tol):
    """Douglas-Peucker iterativo: mantem so os pontos que mudam o desenho."""
    if len(pts) < 3:
        return pts
    manter = [False] * len(pts)
    manter[0] = manter[-1] = True
    pilha = [(0, len(pts) - 1)]
    while pilha:
        i, j = pilha.pop()
        dmax, k = 0, -1
        for x in range(i + 1, j):
            d = dist_ponto_reta(pts[x], pts[i], pts[j])
            if d > dmax:
                dmax, k = d, x
        if dmax > tol:
            manter[k] = True
            pilha += [(i, k), (k, j)]
    return [p for p, m in zip(pts, manter) if m]


def delta(pts):
    """[lat,lon,lat,lon...] em inteiros, cada um como diferenca do anterior.
    Numeros pequenos = arquivo bem menor."""
    out, la, lo = [], 0, 0
    for lat, lon in pts:
        a, b = round(lat * ESCALA), round(lon * ESCALA)
        out += [a - la, b - lo]
        la, lo = a, b
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gtfs")
    ap.add_argument("-o", "--saida", default="dados_linhas.js")
    a = ap.parse_args()

    routes = {r["route_id"]: r for r in ler(a.gtfs, "routes.txt")}
    trips = list(ler(a.gtfs, "trips.txt"))

    seq = defaultdict(list)
    for r in ler(a.gtfs, "stop_times.txt"):
        seq[r["trip_id"]].append((int(r["stop_sequence"]), r["stop_id"], seg(r["arrival_time"])))

    hw = defaultdict(lambda: [0] * 24)
    for r in ler(a.gtfs, "frequencies.txt"):
        hw[r["trip_id"]][(seg(r["start_time"]) // 3600) % 24] = round(int(r["headway_secs"]) / 60)

    shapes_usados = {t["shape_id"] for t in trips}
    shapes = defaultdict(list)
    for r in ler(a.gtfs, "shapes.txt"):
        if r["shape_id"] in shapes_usados:
            shapes[r["shape_id"]].append((int(r["shape_pt_sequence"]), float(r["shape_pt_lat"]), float(r["shape_pt_lon"])))

    paradas_usadas = set()
    linhas = defaultdict(lambda: {"s": []})
    pts_antes = pts_depois = 0
    for t in trips:
        if t["trip_id"] not in seq:
            continue
        r = routes[t["route_id"]]
        p = sorted(seq[t["trip_id"]])
        sh = [(la, lo) for _, la, lo in sorted(shapes.get(t["shape_id"], []))]
        simp = simplificar(sh, TOLERANCIA_M)
        pts_antes += len(sh); pts_depois += len(simp)
        ids = [x[1] for x in p]
        paradas_usadas.update(ids)
        L = linhas[t["route_id"]]
        L["n"] = r["route_short_name"]
        L["nome"] = r["route_long_name"]
        L["cor"] = "#" + (r.get("route_color") or "0082BA")
        L["s"].append({
            "dest": t["trip_headsign"],
            "dir": int(t["direction_id"]),
            "paradas": ids,
            "min": [round((x[2] - p[0][2]) / 60) for x in p],   # minutos da origem ate cada parada
            "hw": hw[t["trip_id"]],
            "tracado": delta(simp),
        })
    for L in linhas.values():
        L["s"].sort(key=lambda s: s["dir"])

    paradas = {}
    for r in ler(a.gtfs, "stops.txt"):
        if r["stop_id"] in paradas_usadas:
            paradas[r["stop_id"]] = [r["stop_name"], round(float(r["stop_lat"]), 5), round(float(r["stop_lon"]), 5)]

    dados = {"fonte": "GTFS SPTrans", "escala": ESCALA,
             "linhas": sorted(linhas.values(), key=lambda l: l["n"]), "paradas": paradas}
    with open(a.saida, "w", encoding="utf-8") as f:
        f.write("// GERADO por tools/gtfs_to_mapa.py - dados GTFS SPTrans (https://www.sptrans.com.br/desenvolvedores/)\n")
        f.write("window.DADOS_LINHAS = ")
        json.dump(dados, f, ensure_ascii=False, separators=(",", ":"))
        f.write(";\n")
    print(f"[ok] {len(linhas)} linhas, {sum(len(l['s']) for l in linhas.values())} sentidos, {len(paradas)} paradas")
    print(f"     trajetos: {pts_antes} pontos -> {pts_depois} ({100 * pts_depois / max(1, pts_antes):.0f}%)")
    print(f"-> {a.saida} ({os.path.getsize(a.saida) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
