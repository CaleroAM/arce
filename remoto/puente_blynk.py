#!/usr/bin/env python3
"""
Puente remoto del dashboard ARCE (lectura pública + controles con PIN).

ESP32 -> Blynk.Cloud -> este puente (127.0.0.1:8090) -> Tailscale Funnel -> cualquier navegador.

- Sirve el MISMO dashboard que el ESP32 (se extrae de sketches/control_ph/web_ui.h).
  Los controles (iniciar/detener/umbral) exigen el PIN del equipo, enviado en la cabecera
  X-Pin; 5 fallos bloquean los controles 2 min. Las órdenes van a Blynk (V2, V1).
- Lee los datos de Blynk cada 2 s con UNA petición y los entrega en /api/estado con el
  mismo formato que el ESP32. El token se lee de secrets.h y nunca llega al navegador.

Uso:   python3 -I remoto/puente_blynk.py            (desde la carpeta del proyecto)
PIN de control: remoto/.pin (se genera solo; fuera de git) o variable ARCE_PIN
Publicar: tailscale funnel --bg --https=8443 http://127.0.0.1:8090
"""
import json
import os
import re
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRETS = os.path.join(RAIZ, "sketches", "control_ph", "secrets.h")
WEB_UI = os.path.join(RAIZ, "sketches", "control_ph", "web_ui.h")
SERVIDOR = "https://ny3.blynk.cloud/external/api"
HOST, PUERTO = "127.0.0.1", 8090
INTERVALO_S = 2.0

TOKEN = re.search(r'BLYNK_AUTH_TOKEN\s+"([^"]+)"', open(SECRETS, encoding="utf-8").read()).group(1)

# --- HTML: el mismo del ESP32, adaptado a vista remota de solo lectura ---
html = re.search(r'R"HTML\((.*)\)HTML"', open(WEB_UI, encoding="utf-8").read(), re.S).group(1)
OCULTAR = ['<section class="c span"><h2>Módulos</h2>',
           '<section class="c span"><h2>Pruebas de funciones</h2>']
for s in OCULTAR:
    html = html.replace(s, s.replace('<section ', '<section style="display:none" ', 1), 1)
html = html.replace('<a href="/update">Actualizar firmware</a>', '').replace('<a href="/simple">Vista sencilla</a>', '')
html = html.replace("Sin conexión con el ESP32. Reintentando…", "Sin datos de Blynk (¿el ESP32 está apagado?). Reintentando…")
html = html.replace('"ESP32 en línea"', '"datos en vivo"')
html = html.replace('$("kPu").textContent=d.pulsos;', '$("kPu").textContent=d.pulsos==null?"--":d.pulsos;')
html = html.replace('$("kUp").textContent=dur(d.uptime_s);', '$("kUp").textContent=d.uptime_s==null?"--":dur(d.uptime_s);')
AVISO = ('<div style="background:#0969da;color:#fff;padding:8px 12px;border-radius:8px;margin-bottom:12px;font-size:14px">'
         '🌐 Vista remota · datos vía Blynk.Cloud (cada ~2 s) · '
         'los <b>controles piden el PIN del equipo</b></div>')
html = html.replace('<div id="banner" class="banner">', AVISO + '<div id="banner" class="banner">', 1)
# Controles remotos: se redefine cmd() para enviar la orden por POST con el PIN en una cabecera.
CMD_JS = r'''<script>
function cmd(q,out){var p=null;try{p=sessionStorage.getItem("arcePin")}catch(e){}
 if(!p){p=prompt("PIN de control del equipo ARCE");if(!p){$(out).textContent="Cancelado";return}}
 $(out).textContent="Enviando…";
 fetch("/cmd?"+q,{method:"POST",headers:{"X-Pin":p},cache:"no-store"}).then(function(r){return r.text().then(function(t){
  try{if(r.status===401)sessionStorage.removeItem("arcePin");else if(r.ok)sessionStorage.setItem("arcePin",p)}catch(e){}
  $(out).textContent=t.trim()})}).catch(function(){$(out).textContent="Error de conexión"})}
</script></body>'''
html = html.replace('</script></body>', '</script>' + CMD_JS, 1)
HTML_BYTES = html.encode("utf-8")

# --- PIN de control (remoto/.pin, fuera de git) ---
RUTA_PIN = os.path.join(RAIZ, "remoto", ".pin")
if os.environ.get("ARCE_PIN"):
    PIN = os.environ["ARCE_PIN"].strip()
elif os.path.exists(RUTA_PIN):
    PIN = open(RUTA_PIN, encoding="utf-8").read().strip()
else:
    import secrets as _s
    PIN = f"{_s.randbelow(10**6):06d}"
    with open(RUTA_PIN, "w", encoding="utf-8") as fpin:
        fpin.write(PIN + "\n")
    os.chmod(RUTA_PIN, 0o600)
fallos = {"n": 0, "bloqueo_hasta": 0.0}
MAX_FALLOS, BLOQUEO_S = 5, 120

# --- Caché de datos de Blynk ---
estado = {"json": None, "error": "arrancando", "online": False, "t": 0.0}
cerrojo = threading.Lock()


def blynk(ruta):
    with urllib.request.urlopen(f"{SERVIDOR}/{ruta}", timeout=8) as r:
        return r.read().decode("utf-8")


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def sondeo():
    ultimo_online = 0.0
    online = False
    while True:
        try:
            pins = "&".join(f"V{i}" for i in range(10))
            v = json.loads(blynk(f"get?token={TOKEN}&{pins}"))
            if time.time() - ultimo_online > 10:
                online = blynk(f"isHardwareConnected?token={TOKEN}").strip() == "true"
                ultimo_online = time.time()
            d = {
                "ph": num(v.get("V0")), "umbral": num(v.get("V1")), "estado": v.get("V3") or "--",
                "voltaje": num(v.get("V4")), "bomba": str(v.get("V5")) in ("1", "1.0"),
                "dosis_ml": num(v.get("V6")), "color": v.get("V7") or "#888888",
                "mensaje": v.get("V8") or "", "adc": None, "pulsos": None, "ganancia": None,
                "consumo_total_ml": None, "wifi_sta": online, "blynk": online, "uptime_s": None,
                "ultima_sesion": None, "remoto": True,
            }
            if not online:
                d["ph"] = None
            with cerrojo:
                estado.update(json=json.dumps(d, ensure_ascii=False).encode("utf-8"), error=None, online=online, t=time.time())
        except Exception as e:  # red caída, Blynk sin respuesta, etc.
            with cerrojo:
                estado["error"] = str(e)
        time.sleep(INTERVALO_S)


class Manejador(BaseHTTPRequestHandler):
    def _enviar(self, codigo, tipo, cuerpo):
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(cuerpo)))
        self.end_headers()
        self.wfile.write(cuerpo)

    def do_GET(self):
        ruta = self.path.split("?", 1)[0]
        if ruta in ("/", "/index.html"):
            self._enviar(200, "text/html; charset=utf-8", HTML_BYTES)
        elif ruta == "/api/estado":
            with cerrojo:
                j, err, t = estado["json"], estado["error"], estado["t"]
            if j is None or time.time() - t > 15:
                self._enviar(503, "application/json", json.dumps({"error": err or "sin datos"}).encode())
            else:
                self._enviar(200, "application/json", j)
        elif ruta in ("/cmd", "/test", "/diag", "/update"):
            self._enviar(403, "text/plain; charset=utf-8", "En la vista remota los controles se usan con los botones (y el PIN)\n".encode())
        else:
            self._enviar(404, "text/plain; charset=utf-8", b"No encontrado\n")

    def do_POST(self):
        ruta, _, consulta = self.path.partition("?")
        if ruta != "/cmd":
            return self._enviar(404, "text/plain; charset=utf-8", b"No encontrado\n")
        ahora = time.time()
        if ahora < fallos["bloqueo_hasta"]:
            return self._enviar(429, "text/plain; charset=utf-8",
                                f"Controles bloqueados por intentos fallidos. Espera {int(fallos['bloqueo_hasta'] - ahora)} s\n".encode())
        if self.headers.get("X-Pin", "") != PIN:
            fallos["n"] += 1
            if fallos["n"] >= MAX_FALLOS:
                fallos.update(n=0, bloqueo_hasta=ahora + BLOQUEO_S)
            return self._enviar(401, "text/plain; charset=utf-8", "PIN incorrecto\n".encode())
        fallos["n"] = 0
        params = dict(p.split("=", 1) for p in consulta.split("&") if "=" in p)
        try:
            if params.get("c") == "start":
                blynk(f"update?token={TOKEN}&V2=1"); txt = "Orden enviada: iniciar dosificación (el ESP32 la rechaza si el pH ya está en el umbral)"
            elif params.get("c") == "stop":
                blynk(f"update?token={TOKEN}&V2=0"); txt = "Orden enviada: detener"
            elif "u" in params:
                u = float(params["u"])
                if not 4.5 <= u <= 7.0:
                    return self._enviar(400, "text/plain; charset=utf-8", "El umbral debe estar entre 4.5 y 7\n".encode())
                blynk(f"update?token={TOKEN}&V1={u:.1f}"); txt = f"Umbral enviado: {u:.1f}"
            else:
                return self._enviar(400, "text/plain; charset=utf-8", "Orden no reconocida\n".encode())
        except Exception as e:
            return self._enviar(502, "text/plain; charset=utf-8", f"No se pudo enviar a Blynk: {e}\n".encode())
        print(f"[{time.strftime('%H:%M:%S')}] orden remota: {txt}", flush=True)
        self._enviar(200, "text/plain; charset=utf-8", (txt + "\n").encode())

    def log_message(self, *args):  # sin registro por petición
        pass


if __name__ == "__main__":
    threading.Thread(target=sondeo, daemon=True).start()
    print(f"Puente ARCE en http://{HOST}:{PUERTO} (datos de Blynk cada {INTERVALO_S:.0f} s, controles con PIN)", flush=True)
    ThreadingHTTPServer((HOST, PUERTO), Manejador).serve_forever()
