"""
test_servidor.py - testa o servidor_sbp.py contra a API Olho Vivo falsa.
Rodar (da pasta tests/):  python test_servidor.py
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)
sys.path.insert(0, RAIZ)
import servidor_sbp as S  # noqa: E402

falhas = 0


def check(cond, nome):
    global falhas
    print(("  ok   " if cond else "  FALHOU ") + nome)
    falhas += 0 if cond else 1


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


# --- funcoes puras
check(S.minutos_entre("20:05", "20:09") == 4, "minutos_entre 20:05 -> 20:09 = 4")
check(S.minutos_entre("23:58", "00:03") == 5, "minutos_entre atravessa a meia-noite")
check(S.minutos_entre("20:05", "20:01") == 0, "chegada no passado vira 0")
prev = {"linhas": [{"linha": "5757-51", "chegadas": [{"min": 3}, {"min": 9}, {"min": 20}, {"min": 40}]},
                   {"linha": "x;y=1", "chegadas": [{"min": 1}]}, {"linha": "607C-10", "chegadas": []}]}
check(S.montar_gps(prev) == "5757-51=3,9,20", "montar_gps: formato do totem, max 3, descarta linha esquisita/vazia")

# --- servidor real contra API falsa
falsa = subprocess.Popen([sys.executable, os.path.join(AQUI, "olhovivo_falso.py"), "8765"], env=dict(os.environ, SBP_FAKE_AGORA="18:00"), stdout=subprocess.DEVNULL)
env = dict(os.environ, SPTRANS_API="http://127.0.0.1:8765/v2.1", SPTRANS_TOKEN="token-de-teste")
srv = subprocess.Popen([sys.executable, os.path.join(RAIZ, "servidor_sbp.py"), "--porta", "8799", "--sem-navegador", "--sem-ponte"],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
env_ruim = dict(env, SPTRANS_TOKEN="token-errado")
srv2 = subprocess.Popen([sys.executable, os.path.join(RAIZ, "servidor_sbp.py"), "--porta", "8798", "--sem-navegador", "--sem-ponte"],
                        env=env_ruim, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(4)   # a API falsa carrega o GTFS da cidade
try:
    B = "http://127.0.0.1:8799"
    c, t = get(B + "/painel/")
    check(c == 200 and "S.B.P." in t, "serve o painel por http")
    c, t = get(B + "/api/status"); j = json.loads(t)
    check(j["olhovivo"] is True, "login no Olho Vivo com o token do ambiente")
    c, t = get(B + "/api/previsao?parada=3703094"); j = json.loads(t)
    l = [x for x in j["linhas"] if x["linha"] == "5757-51"][0]
    mins = [x["min"] for x in l["chegadas"]]
    check(len(mins) >= 2 and mins == sorted(mins) and mins[0] <= 20, "previsao: horarios viram minutos, em ordem " + str(mins))
    check(l["destino"] == "PEDREIRA", "previsao: destino certo pelo sentido (%s)" % l["destino"])
    check(any(c["acessivel"] for c in l["chegadas"]) and "acessivel" in l["chegadas"][0], "previsao: informa onibus acessivel")
    c, t = get(B + "/api/previsao?parada=2203206"); j = json.loads(t)
    check(len(j["linhas"]) >= 10, "previsao funciona em qualquer parada (Cupece: %d linhas)" % len(j["linhas"]))
    c, t = get(B + "/api/previsao?parada=1111"); j = json.loads(t)
    check(c == 200 and j["linhas"] == [], "parada sem dados: lista vazia, sem erro")
    c, t = get(B + "/api/posicao?linha=5757-51"); j = json.loads(t)
    check(len(j["onibus"]) >= 4 and {o["sentido"] for o in j["onibus"]} == {1, 2}, "posicao: onibus nos 2 sentidos (%d)" % len(j["onibus"]))
    import olhovivo_falso as F
    perto = all(min(F.dist((o["lat"], o["lon"]), p) for S in F.SENTIDOS.values() if S["L"]["n"] == "5757-51" for p in S["pts"]) < 400 for o in j["onibus"])
    check(perto, "posicao: todos os onibus estao em cima do trajeto da linha")
    c, t = get(B + "/api/posicao?linha=5757-10"); j2 = json.loads(t)
    check({o["prefixo"] for o in j2["onibus"]}.isdisjoint({o["prefixo"] for o in j["onibus"]}), "posicao: nao mistura com a 5757-10")
    c, _ = get(B + "/api/posicao?linha=../../etc"); check(c == 400, "linha invalida -> 400")
    c, _ = get(B + "/api/previsao?parada=1;rm"); check(c == 400, "parada invalida -> 400")
    open(os.path.join(RAIZ, ".env.teste-nao-servir"), "w").write("SPTRANS_TOKEN=segredo")
    c, t = get(B + "/.env.teste-nao-servir"); check(c == 404 and "segredo" not in t, "arquivos com ponto (.env) nunca sao servidos")
    os.remove(os.path.join(RAIZ, ".env.teste-nao-servir"))
    c, t = get(B + "/api/status")
    check("token-de-teste" not in t, "o token nunca aparece nas respostas")
    c, t = get("http://127.0.0.1:8798/api/status"); j = json.loads(t)
    check(j["olhovivo"] is False and "recusado" in j["erro"], "token errado: avisa que foi recusado")
    c, t = get("http://127.0.0.1:8798/api/previsao?parada=3703094")
    check(c == 502 and "token-errado" not in t, "token errado: erro claro e sem vazar o token")
finally:
    for p in (srv, srv2, falsa):
        p.terminate()
    saida = srv.communicate(timeout=5)[0]
check("token-de-teste" not in saida, "o log do servidor nao imprime o token inteiro")
# --- ponte GPS -> totem, com um paho-mqtt de mentira (sem broker de verdade)
import threading, types  # noqa: E402
publicados = []
class ClienteFalso:
    def __init__(self, *a, **k): self.on_message = None
    def connect(self, *a): pass
    def subscribe(self, t): self.inscrito = t
    def loop_start(self):
        msg = types.SimpleNamespace(payload=json.dumps({"online": True, "ponto": "3703094"}).encode())
        self.on_message(self, None, msg)
    def loop_stop(self): pass
    def publish(self, t, m): publicados.append((t, m))
mod = types.ModuleType("paho.mqtt.client"); mod.Client = ClienteFalso
mod.CallbackAPIVersion = types.SimpleNamespace(VERSION2=2)
sys.modules.update({"paho": types.ModuleType("paho"), "paho.mqtt": types.ModuleType("paho.mqtt"), "paho.mqtt.client": mod})
class OvFalso:
    token = "x"
    def previsao_parada(self, p): return {"linhas": [{"linha": "5757-51", "chegadas": [{"min": 4}, {"min": 19}]}]} if p == "3703094" else {}
S.ov = OvFalso()
parar = threading.Event()
th = threading.Thread(target=S.ponte_totem, args=(parar,)); th.start(); time.sleep(0.5); parar.set(); th.join(3)
check(publicados == [("sbp-gs2026/totem/sbp-0001/gps", "5757-51=4,19")], "ponte: descobre o ponto do totem e publica o GPS no formato dele")

print("TODOS OS TESTES PASSARAM" if not falhas else "%d FALHA(S)" % falhas)
sys.exit(1 if falhas else 0)
