"""
olhovivo_falso.py - imitacao da API Olho Vivo v2.1 para testes e demonstracao SEM token real.

Responde no formato da documentacao oficial da SPTrans, mas com onibus simulados
"de verdade": eles andam em cima do trajeto real de cada linha (painel/dados_linhas.js,
gerado do GTFS), saem no intervalo daquela hora e cada um tem um atraso proprio
(0 a 6 min), entao a previsao "GPS" fica parecida com a grade, mas nao igual.
Funciona para qualquer linha e qualquer parada da cidade.

Imita tambem o login: POST /Login/Autenticar?token=token-de-teste devolve true e
um cookie; sem o cookie, as consultas respondem 401.

Uso:  python olhovivo_falso.py 8765
      SBP_FAKE_AGORA=18:00 fixa o relogio (para os testes automaticos)
"""
import json
import math
import os
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOKEN = "token-de-teste"
SESSAO = "apiCredentials=sessao-falsa-123"
AQUI = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- dados do GTFS (os mesmos do mapa)
with open(os.path.join(AQUI, "..", "painel", "dados_linhas.js"), encoding="utf-8") as f:
    txt = f.read()
D = json.loads(txt[txt.index("=") + 1:].strip().rstrip(";"))
ESC = D["escala"]


def decodificar(t):
    pts, la, lo = [], 0, 0
    for i in range(0, len(t), 2):
        la += t[i]; lo += t[i + 1]
        pts.append((la / ESC, lo / ESC))
    return pts


def dist(a, b):
    kx = 111320 * math.cos(math.radians(a[0]))
    return math.hypot((b[1] - a[1]) * kx, (b[0] - a[0]) * 110540)


# cada sentido de cada linha vira um "codigo de linha" (cl), como no Olho Vivo
SENTIDOS = {}          # cl -> dict
POR_NUMERO = {}        # "5757" -> [cl, ...]
POR_PARADA = {}        # stop_id -> [cl, ...]
for i, L in enumerate(D["linhas"]):
    for k, s in enumerate(L["s"]):
        cl = (i + 1) * 10 + k + 1
        pts = decodificar(s["tracado"])
        acum = [0.0]
        for a, b in zip(pts, pts[1:]):
            acum.append(acum[-1] + dist(a, b))
        SENTIDOS[cl] = {"L": L, "s": s, "sl": k + 1, "pts": pts, "acum": acum}
        POR_NUMERO.setdefault(L["n"].split("-")[0], []).append(cl)
        for p in s["paradas"][:-1]:
            POR_PARADA.setdefault(p, []).append(cl)


def agora_min():
    fixo = os.environ.get("SBP_FAKE_AGORA")
    if fixo:
        return int(fixo[:2]) * 60 + int(fixo[3:5])
    t = time.localtime()
    return t.tm_hour * 60 + t.tm_min + t.tm_sec / 60


def hhmm(m):
    m = int(round(m)) % 1440
    return "%02d:%02d" % (m // 60, m % 60)


def ponto_no_trajeto(S, frac):
    """Posicao (lat, lon) a uma fracao 0..1 do comprimento do trajeto."""
    pts, acum = S["pts"], S["acum"]
    if not pts:
        return None
    alvo = max(0.0, min(1.0, frac)) * acum[-1]
    for j in range(1, len(acum)):
        if acum[j] >= alvo:
            seg = acum[j] - acum[j - 1] or 1
            f = (alvo - acum[j - 1]) / seg
            a, b = pts[j - 1], pts[j]
            return (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)
    return pts[-1]


def onibus(cl, futuro=90):
    """Onibus do sentido: os que estao rodando agora e os que vao sair nos proximos minutos.
    Partidas no intervalo (headway) da hora, como a grade; cada onibus tem um atraso de 0 a 6 min."""
    S = SENTIDOS[cl]
    s = S["s"]
    T = max(1, s["min"][-1])                       # duracao da viagem (min)
    agora = agora_min()
    lista = []
    dep = math.floor((agora - T - 10) / 60) * 60   # comeca a olhar umas horas antes
    while dep <= agora + futuro:
        hw = s["hw"][int(dep // 60) % 24]
        if not hw:
            dep = (math.floor(dep / 60) + 1) * 60
            continue
        prefixo = 10000 + (cl * 7 + int(dep)) % 89999
        atraso = (prefixo % 7)                       # 0..6 min, fixo por onibus
        andou = agora - dep - atraso                 # minutos de viagem ja feitos
        if andou < T:
            lista.append({"p": str(prefixo), "a": prefixo % 3 != 0, "dep": dep, "atraso": atraso, "andou": andou})
        dep += hw
    return S, T, agora, lista


def iso_utc():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _resp(self, cod, obj, cookie=None):
        b = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(cod)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        if cookie:
            self.send_header("Set-Cookie", cookie + "; path=/")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        if u.path.endswith("/Login/Autenticar"):
            ok = urllib.parse.parse_qs(u.query).get("token", [""])[0] == TOKEN
            return self._resp(200, ok, SESSAO if ok else None)
        self._resp(404, {})

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        if SESSAO not in (self.headers.get("Cookie") or ""):
            return self._resp(401, {"Message": "Authorization has been denied for this request."})
        hr = hhmm(agora_min())

        if u.path.endswith("/Linha/Buscar"):
            num = q.get("termosBusca", [""])[0].split("-")[0].upper()
            out = []
            for cl in POR_NUMERO.get(num, []):
                L = SENTIDOS[cl]["L"]
                dests = [x["dest"] for x in L["s"]]
                out.append({"cl": cl, "lc": len(dests) == 1, "lt": num, "sl": SENTIDOS[cl]["sl"],
                            "tl": int(L["n"].split("-")[1]), "tp": dests[0].upper(), "ts": dests[-1].upper()})
            return self._resp(200, out)

        if u.path.endswith("/Posicao/Linha"):
            try:
                cl = int(q.get("codigoLinha", ["0"])[0])
            except ValueError:
                cl = 0
            if cl not in SENTIDOS:
                return self._resp(200, {"hr": hr, "vs": []})
            S, T, agora, lista = onibus(cl, futuro=0)
            vs = []
            for o in lista:
                if o["andou"] < 0:
                    continue                          # ainda nao saiu do terminal
                pos = ponto_no_trajeto(S, o["andou"] / T)
                if pos:
                    vs.append({"p": o["p"], "a": o["a"], "ta": iso_utc(), "py": round(pos[0], 6), "px": round(pos[1], 6)})
            return self._resp(200, {"hr": hr, "vs": vs})

        if u.path.endswith("/Previsao/Parada"):
            cp = q.get("codigoParada", [""])[0]
            if cp not in POR_PARADA or cp not in D["paradas"]:
                return self._resp(200, {"hr": hr, "p": None})
            nome, lat, lon = D["paradas"][cp]
            linhas = []
            for cl in POR_PARADA[cp]:
                S, T, agora, lista = onibus(cl)
                i = S["s"]["paradas"].index(cp)
                ate_parada = S["s"]["min"][i]
                vs = []
                for o in lista:
                    chegada = o["dep"] + o["atraso"] + ate_parada
                    if chegada < agora:
                        continue                      # ja passou pela parada
                    pos = ponto_no_trajeto(S, max(0, o["andou"]) / T) if o["andou"] >= 0 else S["pts"][0]
                    vs.append({"p": o["p"], "t": hhmm(chegada), "a": o["a"], "ta": iso_utc(),
                               "py": round(pos[0], 6), "px": round(pos[1], 6), "_c": chegada})
                vs.sort(key=lambda v: v["_c"])
                for v in vs:
                    v.pop("_c")
                vs = vs[:4]
                if vs:
                    outros = [x["dest"] for x in S["L"]["s"] if x is not S["s"]]
                    destino = S["s"]["dest"].upper()
                    origem = (outros[0] if outros else S["s"]["dest"]).upper()
                    sl = S["sl"]
                    # convencao da API real: lt0 = terminal principal, lt1 = secundario
                    # sentido 1 vai do principal pro secundario; sentido 2 volta
                    lt0, lt1 = (origem, destino) if sl == 1 else (destino, origem)
                    linhas.append({"c": S["L"]["n"], "cl": cl, "sl": sl, "lt0": lt0, "lt1": lt1, "qv": len(vs), "vs": vs})
            return self._resp(200, {"hr": hr, "p": {"cp": int(cp), "np": nome.upper(), "py": lat, "px": lon, "l": linhas}})

        self._resp(404, {})


if __name__ == "__main__":
    porta = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print("[olhovivo falso] %d sentidos de linha, %d paradas - http://127.0.0.1:%d/v2.1  (token: %s)" % (len(SENTIDOS), len(POR_PARADA), porta, TOKEN))
    ThreadingHTTPServer(("127.0.0.1", porta), H).serve_forever()
