"""
servidor_sbp.py  -  S.B.P. Smart Bus Point  |  "backend" local da demonstracao

Faz 3 coisas:
  1. Serve o painel em http://localhost:8000/painel/
     (aberto como arquivo, file://, o mapa do OpenStreetMap fica bloqueado).
  2. Conversa com a API Olho Vivo da SPTrans (GPS dos onibus em tempo real).
     O token fica SO no arquivo .env deste computador. Nunca vai para o
     navegador, para o ESP32 nem para o GitHub.
  3. Ponte GPS -> totem: a cada 20 s pega a previsao por GPS do ponto onde o
     totem esta e manda pelo MQTT (topico .../gps). O totem passa a mostrar a
     previsao real e, se a ponte parar, volta sozinho para a grade GTFS.
     (precisa de: pip install paho-mqtt)

Como usar:
  1. Crie um arquivo .env na mesma pasta deste script com a linha:
        SPTRANS_TOKEN=seu_token_aqui
     (sem .env o servidor funciona do mesmo jeito, so que sem GPS)
  2. python servidor_sbp.py        (no Windows pode dar duplo clique em abrir_painel.bat)
  3. O navegador abre sozinho no painel.

Opcoes: --porta 8000  --sem-navegador  --sem-ponte
Variavel SPTRANS_API muda o endereco da API (usado nos testes com API falsa).
"""
import argparse
import http.cookiejar
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

PASTA = os.path.dirname(os.path.abspath(__file__))
# A SPTrans diz que a API e https, mas ja houve epocas em que so http respondia: tentamos os dois
API_BASES = [os.environ["SPTRANS_API"]] if os.environ.get("SPTRANS_API") else [
    "https://api.olhovivo.sptrans.com.br/v2.1", "http://api.olhovivo.sptrans.com.br/v2.1"]
API = API_BASES[0]
# Algumas APIs recusam o "Python-urllib" padrao; nos identificamos como um navegador comum
CABECALHOS = [("User-Agent", "Mozilla/5.0 (SBP-SmartBusPoint; FIAP GS)"), ("Accept", "application/json")]
MQTT_HOST, MQTT_PORTA = "broker.hivemq.com", 1883
TOPICO = "sbp-gs2026/totem/sbp-0001"
LETREIRO = re.compile(r"^[0-9A-Z]{4}-[0-9]{2}$")   # formato de linha da SPTrans, ex.: 5757-51
PARADA = re.compile(r"^[0-9]{1,12}$")


def ler_token():
    """Token do .env (ou da variavel de ambiente). Nunca e impresso inteiro."""
    tok = os.environ.get("SPTRANS_TOKEN", "")
    arq = os.path.join(PASTA, ".env")
    if not tok and os.path.exists(arq):
        for linha in open(arq, encoding="utf-8"):
            if linha.strip().startswith("SPTRANS_TOKEN="):
                tok = linha.split("=", 1)[1].strip().strip('"').strip("'")
    return tok


class OlhoVivo:
    """Cliente minimo da API Olho Vivo v2.1.
    Login: POST /Login/Autenticar?token=... devolve true e um cookie de sessao.
    Depois disso todas as chamadas usam esse cookie."""

    def __init__(self, token):
        self.token = token
        self.cookies = http.cookiejar.CookieJar()
        self.http = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))
        self.http.addheaders = CABECALHOS
        self.logado = False
        self.erro = "sem token (crie o arquivo .env)" if not token else ""
        self.trava = threading.Lock()
        self.cache = {}

    def _req(self, metodo, caminho, tentar_login=True):
        if not self.token:
            raise RuntimeError(self.erro)
        with self.trava:
            if not self.logado:
                self._login()
        req = urllib.request.Request(API + caminho, method=metodo, data=b"" if metodo == "POST" else None)   # API = a base que logou
        try:
            with self.http.open(req, timeout=10) as r:
                corpo = r.read().decode("utf-8")
        except urllib.error.HTTPError as e:
            if e.code == 401 and tentar_login:            # sessao expirou: loga de novo e tenta 1 vez
                self.logado = False
                return self._req(metodo, caminho, tentar_login=False)
            raise
        dados = json.loads(corpo) if corpo else None
        # sessao expirada: a API responde com mensagem de "autorizacao negada"
        if isinstance(dados, dict) and "Message" in dados and tentar_login:
            self.logado = False
            return self._req(metodo, caminho, tentar_login=False)
        return dados

    def _login(self):
        global API
        ultimo = ""
        for base in API_BASES:                     # tenta https, depois http
            req = urllib.request.Request(base + "/Login/Autenticar?token=" + urllib.parse.quote(self.token), method="POST", data=b"")
            try:
                with self.http.open(req, timeout=10) as r:
                    resposta = r.read().decode().strip().lower()
            except urllib.error.HTTPError as e:
                ultimo = "HTTP %d em %s" % (e.code, base.split("://")[0]); continue
            except Exception as e:
                ultimo = "%s em %s" % (type(e).__name__, base.split("://")[0]); continue
            if resposta != "true":
                self.erro = "token recusado pela SPTrans (confira o .env ou se o app ja foi aprovado)"
                raise RuntimeError(self.erro)
            API = base
            self.logado, self.erro = True, ""
            print("[olhovivo] login ok (%s)" % base.split("://")[0])
            return
        self.erro = "nao consegui falar com a API: " + ultimo
        raise RuntimeError(self.erro)

    def diagnostico(self):
        """python servidor_sbp.py --testar : mostra o que a API responde, sem mostrar o token."""
        print("token: %s" % ("...%s (%d caracteres)" % (self.token[-4:], len(self.token)) if self.token else "NAO ENCONTRADO (.env)"))
        for base in API_BASES:
            req = urllib.request.Request(base + "/Login/Autenticar?token=" + urllib.parse.quote(self.token), method="POST", data=b"")
            try:
                with self.http.open(req, timeout=10) as r:
                    print("%-45s -> HTTP %d, resposta: %s" % (base, r.status, r.read(60).decode(errors="replace")))
            except urllib.error.HTTPError as e:
                print("%-45s -> HTTP %d %s, corpo: %s" % (base, e.code, e.reason, e.read(120).decode(errors="replace").replace(self.token, "***")))
            except Exception as e:
                print("%-45s -> %s: %s" % (base, type(e).__name__, str(e).replace(self.token, "***")[:120]))

    def _cacheado(self, chave, segundos, fn):
        agora = time.time()
        if chave in self.cache and agora - self.cache[chave][0] < segundos:
            return self.cache[chave][1]
        v = fn()
        self.cache[chave] = (agora, v)
        return v

    def previsao_parada(self, parada):
        """Previsao por GPS de todos os onibus chegando na parada.
        A API devolve horarios ("t": "20:09"); aqui ja vira 'minutos que faltam'."""
        def busca():
            d = self._req("GET", "/Previsao/Parada?codigoParada=" + parada)
            if not d or not d.get("p"):
                return {"hora": (d or {}).get("hr"), "linhas": []}
            hr = d.get("hr") or time.strftime("%H:%M")
            linhas = []
            for l in d["p"].get("l") or []:
                chegadas = []
                for v in l.get("vs") or []:
                    chegadas.append({"min": minutos_entre(hr, v.get("t", hr)), "hora": v.get("t"), "prefixo": v.get("p"),
                                     "acessivel": bool(v.get("a")), "lat": v.get("py"), "lon": v.get("px")})
                chegadas.sort(key=lambda c: c["min"])
                # A doc chama lt0 de "destino" e lt1 de "origem", mas no exemplo dela lt0 e o terminal principal.
                # Pelo sentido nao tem erro: sl=1 vai do principal pro secundario (destino lt1); sl=2 volta (destino lt0).
                destino, origem = (l.get("lt1"), l.get("lt0")) if l.get("sl") == 1 else (l.get("lt0"), l.get("lt1"))
                linhas.append({"linha": l.get("c"), "sentido": l.get("sl"), "origem": origem, "destino": destino, "chegadas": chegadas})
            return {"hora": hr, "parada": d["p"].get("np"), "linhas": linhas}
        return self._cacheado("prev:" + parada, 15, busca)

    def posicao_linha(self, letreiro):
        """Posicao GPS de todos os onibus da linha (os dois sentidos)."""
        def busca():
            num, tipo = letreiro.split("-")
            codigos = self._cacheado("linha:" + letreiro, 3600, lambda: [
                c for c in (self._req("GET", "/Linha/Buscar?termosBusca=" + num) or [])
                if str(c.get("lt")) == num and str(c.get("tl")) == str(int(tipo))])
            onibus, hr = [], None
            for c in codigos:
                d = self._req("GET", "/Posicao/Linha?codigoLinha=" + str(c["cl"])) or {}
                hr = d.get("hr", hr)
                for v in d.get("vs") or []:
                    onibus.append({"sentido": c.get("sl"), "prefixo": v.get("p"), "acessivel": bool(v.get("a")),
                                   "lat": v.get("py"), "lon": v.get("px"), "atualizado": v.get("ta")})
            return {"hora": hr, "onibus": onibus}
        return self._cacheado("pos:" + letreiro, 8, busca)     # painel consulta a cada 10 s


def minutos_entre(agora, depois):
    """'20:05' -> '20:09' = 4. Atravessa a meia-noite ('23:58' -> '00:03' = 5)."""
    try:
        a = int(agora[:2]) * 60 + int(agora[3:5])
        b = int(depois[:2]) * 60 + int(depois[3:5])
    except (ValueError, TypeError):
        return 0
    d = b - a
    if d < -720:
        d += 1440
    return max(0, d)


ov = None


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=PASTA, **k)

    def log_message(self, fmt, *args):            # so loga a API, nao cada arquivo/tile
        if self.path.startswith("/api/"):
            sys.stderr.write("[http] %s\n" % (fmt % args))

    def _json(self, codigo, obj):
        corpo = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/":
            self.send_response(302); self.send_header("Location", "/painel/"); self.end_headers(); return
        # nunca servir o .env (nem nada que comece com ponto) pelo navegador
        if any(p.startswith(".") for p in u.path.split("/") if p):
            self.send_error(404); return
        if not u.path.startswith("/api/"):
            return super().do_GET()
        q = urllib.parse.parse_qs(u.query)
        try:
            if u.path == "/api/status":
                if ov.token and not ov.logado and not ov.erro:
                    try: ov._login()
                    except RuntimeError: pass
                return self._json(200, {"olhovivo": ov.logado, "erro": ov.erro})
            if u.path == "/api/previsao":
                p = (q.get("parada") or [""])[0]
                if not PARADA.match(p): return self._json(400, {"erro": "parada invalida"})
                return self._json(200, ov.previsao_parada(p))
            if u.path == "/api/posicao":
                l = (q.get("linha") or [""])[0].upper()
                if not LETREIRO.match(l): return self._json(400, {"erro": "linha invalida (formato 5757-51)"})
                return self._json(200, ov.posicao_linha(l))
            return self._json(404, {"erro": "rota desconhecida"})
        except Exception as e:                    # a mensagem nunca leva o token
            return self._json(502, {"erro": str(e) if isinstance(e, RuntimeError) else "falha ao consultar a API: " + type(e).__name__})


def montar_gps(prev, linhas_validas=None):
    """Monta a mensagem curta que o ESP32 entende:  '5757-51=4,19,34;607C-10=2,9'
    So linhas no formato certo e no maximo 12, pra caber no buffer do totem."""
    partes = []
    for l in prev.get("linhas", []):
        nome = l.get("linha") or ""
        if not LETREIRO.match(nome) or (linhas_validas and nome not in linhas_validas):
            continue
        mins = [str(min(c["min"], 240)) for c in l["chegadas"][:3]]
        if mins:
            partes.append(nome + "=" + ",".join(mins))
    return ";".join(partes[:12])


def ponte_totem(parar):
    """Assina o status do totem (pra saber em que ponto ele esta) e, a cada 20 s,
    publica a previsao GPS desse ponto no topico .../gps."""
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        print("[ponte] paho-mqtt nao instalado: o totem segue so com a grade GTFS (pip install paho-mqtt)")
        return
    estado = {"ponto": None}

    def ao_receber(c, u, msg):
        try:
            j = json.loads(msg.payload.decode("utf-8"))
            if j.get("online") and PARADA.match(str(j.get("ponto", ""))):
                if estado["ponto"] != j["ponto"]:
                    print("[ponte] totem esta no ponto", j["ponto"])
                estado["ponto"] = str(j["ponto"])
        except Exception:
            pass

    try:
        cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sbp-ponte-%04x" % (os.getpid() & 0xffff))
    except AttributeError:                        # paho-mqtt 1.x
        cli = mqtt.Client(client_id="sbp-ponte-%04x" % (os.getpid() & 0xffff))
    cli.on_message = ao_receber
    cli.connect(MQTT_HOST, MQTT_PORTA, 30)
    cli.subscribe(TOPICO + "/status")
    cli.loop_start()
    print("[ponte] ligada: GPS do Olho Vivo -> totem (topico %s/gps)" % TOPICO)
    while not parar.is_set():
        if estado["ponto"] and ov.token:
            try:
                msg = montar_gps(ov.previsao_parada(estado["ponto"]))
                cli.publish(TOPICO + "/gps", msg)
                print("[ponte] %s -> %s" % (estado["ponto"], msg or "(nenhum onibus com GPS agora)"))
            except Exception as e:
                print("[ponte] falhou:", e if isinstance(e, RuntimeError) else type(e).__name__)
        parar.wait(20)
    cli.loop_stop()


def main():
    global ov
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", type=int, default=8000)
    ap.add_argument("--sem-navegador", action="store_true")
    ap.add_argument("--sem-ponte", action="store_true")
    ap.add_argument("--testar", action="store_true", help="so testa o login no Olho Vivo e mostra a resposta")
    a = ap.parse_args()

    tok = ler_token()
    ov = OlhoVivo(tok)
    if a.testar:
        ov.diagnostico()
        return
    print("[olhovivo]", ("token carregado (...%s)" % tok[-4:]) if tok else "sem token: rodando so com a grade GTFS")

    srv = ThreadingHTTPServer(("127.0.0.1", a.porta), Handler)     # so este computador acessa
    url = "http://localhost:%d/painel/" % a.porta
    print("[painel] abra", url, " (Ctrl+C para parar)")
    parar = threading.Event()
    if not a.sem_ponte and tok:
        threading.Thread(target=ponte_totem, args=(parar,), daemon=True).start()
    if not a.sem_navegador:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        parar.set()
        srv.server_close()


if __name__ == "__main__":
    main()