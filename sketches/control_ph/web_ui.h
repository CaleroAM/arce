// Dashboard propio del control de pH — servido en "/" por el ESP32 (red ARCE-prueba).
// Todo inline (HTML + CSS + JS vanilla): funciona sin Internet. Lee /api/estado cada 1 s.
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Control de pH — ARCE</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--tx:#1b1f24;--mu:#5b6470;--bd:#dde2e8;--ac:#0a66d8;--ok:#1a7f37;--bad:#cf222e;--warn:#b26b00;--sh:0 1px 3px rgba(0,0,0,.07)}
@media (prefers-color-scheme:dark){:root{--bg:#0e1116;--card:#171b22;--tx:#e6e9ee;--mu:#9aa4b0;--bd:#2a313b;--ac:#4c9bff;--ok:#3fb950;--bad:#ff6b6b;--warn:#e3a33b;--sh:none}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.w{max-width:980px;margin:0 auto;padding:16px}
header{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px;margin-bottom:12px}
h1{font-size:20px;margin:0}
.pills{display:flex;gap:6px;flex-wrap:wrap}
.pill{font-size:12px;padding:3px 10px;border-radius:99px;border:1px solid var(--bd);color:var(--mu);background:var(--card)}
.pill.on{color:var(--ok);border-color:var(--ok)}.pill.off{color:var(--bad);border-color:var(--bad)}
.g{display:grid;gap:12px;grid-template-columns:1fr}
@media (min-width:760px){.g{grid-template-columns:1fr 1fr}.span{grid-column:1/-1}}
.c{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;box-shadow:var(--sh);min-width:0}
.c h2{font-size:13px;text-transform:uppercase;letter-spacing:.04em;color:var(--mu);margin:0 0 10px}
.hero{display:flex;gap:14px;align-items:center;flex-wrap:wrap}
.phbox{width:150px;height:150px;border-radius:20px;display:flex;flex-direction:column;align-items:center;justify-content:center;transition:background .6s;border:3px solid rgba(0,0,0,.08)}
.phbox small{font-size:13px;opacity:.8}.phbox b{font-size:48px;line-height:1.05;font-variant-numeric:tabular-nums}
.st{flex:1;min-width:160px}
.st .e{font-size:22px;font-weight:700}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;background:var(--bd);margin-right:6px;vertical-align:middle}
.dot.on{background:var(--ok);animation:p 1s infinite}
@keyframes p{0%{box-shadow:0 0 0 0 rgba(26,127,55,.6)}100%{box-shadow:0 0 0 10px rgba(26,127,55,0)}}
.msg{color:var(--mu);margin-top:6px;word-break:break-word}
.scale{position:relative;height:22px;border-radius:6px;margin:22px 0 18px}
.mk{position:absolute;top:-8px;bottom:-8px;width:0;transform:translateX(-50%)}
.mk i{position:absolute;left:-7px;width:14px;height:14px}
.mk.ph i{top:-10px;border-left:7px solid transparent;border-right:7px solid transparent;border-top:10px solid var(--tx);height:0;width:0;left:-7px}
.mk.um{border-left:2px dashed var(--tx)}
.mk span{position:absolute;white-space:nowrap;font-size:11px;color:var(--mu);transform:translateX(-50%)}
.mk.ph span{top:-26px}.mk.um span{bottom:-20px}
.ticks{display:flex;justify-content:space-between;font-size:11px;color:var(--mu)}
canvas{width:100%;height:220px;display:block}
.leg{display:flex;gap:14px;font-size:12px;color:var(--mu);margin-top:6px;flex-wrap:wrap}
.leg i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:4px}
.kp{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
@media (min-width:560px){.kp{grid-template-columns:repeat(4,1fr)}}
.k{border:1px solid var(--bd);border-radius:10px;padding:10px}
.k small{color:var(--mu);font-size:12px}.k b{display:block;font-size:20px;font-variant-numeric:tabular-nums}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
button,.btn{font:inherit;border:0;border-radius:8px;padding:11px 16px;background:var(--ac);color:#fff;cursor:pointer;text-decoration:none}
button.sec{background:transparent;color:var(--ac);border:1px solid var(--ac)}
button.r{background:var(--bad)}button:disabled{opacity:.5;cursor:default}
input[type=number]{font:inherit;width:90px;padding:10px;border-radius:8px;border:1px solid var(--bd);background:var(--bg);color:var(--tx)}
.res{margin-top:8px;font:13px ui-monospace,Menlo,monospace;color:var(--mu);min-height:1.4em;word-break:break-word}
.err{font-size:30px;font-weight:700}
.err.good{color:var(--ok)}.err.mid{color:var(--warn)}.err.bad{color:var(--bad)}
.sesg{display:grid;grid-template-columns:repeat(2,1fr);gap:6px 14px;margin-top:8px;font-size:14px}
.sesg span{color:var(--mu)}
.lnk{font-size:12px;color:var(--mu)}.lnk a{color:var(--mu)}
footer{margin-top:14px;font-size:12px;color:var(--mu);display:flex;gap:12px;flex-wrap:wrap}
footer a{color:var(--mu)}
.banner{display:none;background:var(--bad);color:#fff;padding:8px 12px;border-radius:8px;margin-bottom:12px}
</style></head><body><div class="w">
<header><h1>Control de pH — ARCE</h1>
<div class="pills"><span id="pConn" class="pill">conectando…</span><span id="pWifi" class="pill">Wi-Fi</span><span id="pBlynk" class="pill">Blynk</span></div></header>
<div id="banner" class="banner">Sin conexión con el ESP32. Reintentando…</div>
<div class="g">
<section class="c span"><h2>Medición</h2>
<div class="hero"><div id="phbox" class="phbox"><small>pH</small><b id="ph">--</b></div>
<div class="st"><div><span id="dot" class="dot"></span><span id="bomba">Bomba apagada</span></div>
<div class="e" id="estado">--</div><div>Umbral de apagado: <b id="umb">--</b></div>
<div class="msg" id="msg"></div></div></div>
<div class="scale" id="scale"><div class="mk ph" id="mkPh"><i></i><span id="mkPhT">pH</span></div><div class="mk um" id="mkUm"><span id="mkUmT">umbral</span></div></div>
<div class="ticks"><span>0</span><span>2</span><span>4</span><span>6</span><span>7</span><span>8</span><span>10</span><span>12</span><span>14</span></div>
</section>
<section class="c span"><h2>pH en los últimos 5 minutos</h2><canvas id="cv"></canvas>
<div class="leg"><span><i style="background:var(--ac)"></i>pH</span><span><i style="background:var(--tx);height:0;border-top:2px dashed var(--tx)"></i>Umbral</span><span><i style="background:rgba(26,127,55,.35);height:10px"></i>Bomba encendida</span></div></section>
<section class="c"><h2>Control</h2>
<div class="row"><button id="bStart">Iniciar dosificación</button><button id="bStop" class="r">Detener</button></div>
<div class="row" style="margin-top:12px"><label for="inU">Umbral (4.5–7)</label><input id="inU" type="number" min="4.5" max="7" step="0.1"><button id="bU" class="sec">Fijar</button></div>
<div class="res" id="cRes"></div></section>
<section class="c"><h2>Última dosificación</h2><div id="ses">Aún no hay dosificaciones en esta sesión.</div></section>
<section class="c span"><h2>Indicadores</h2><div class="kp">
<div class="k"><small>Dosis de la sesión</small><b id="kMl">--</b></div>
<div class="k"><small>Pulsos</small><b id="kPu">--</b></div>
<div class="k"><small>Ganancia (pH/s de bomba)</small><b id="kGa">--</b></div>
<div class="k"><small>Consumo total</small><b id="kCo">--</b></div>
<div class="k"><small>Sonda (ADC)</small><b id="kAdc">--</b></div>
<div class="k"><small>Voltaje</small><b id="kV">--</b></div>
<div class="k"><small>Tiempo encendido</small><b id="kUp">--</b></div>
<div class="k"><small>Color indicador</small><b id="kCol">--</b></div></div></section>
<section class="c span"><h2>Módulos</h2><div class="kp">
<div class="k"><small>XN03 · sonda, pot, LED</small><b id="mX03">--</b></div>
<div class="k"><small>XN13 · relevador</small><b id="mX13">--</b></div>
<div class="k"><small>XN44 · buzzer</small><b id="mX44">--</b></div>
<div class="k"><small>Relevador confirma</small><b id="mRel">--</b></div></div></section>
<section class="c span"><h2>Pruebas de funciones</h2>
<div class="row"><button class="sec" data-t="sonido=inicio">Sonido inicio</button><button class="sec" data-t="sonido=fin">Sonido fin</button><button class="sec" data-t="sonido=rechazo">Sonido rechazo</button><button class="sec" data-t="colores=1">Recorrer colores</button></div>
<div class="lnk" style="margin-top:10px">Bomba: <a href="#" data-b="rele=100">pulso de relevador 100 ms</a> · <a href="#" data-b="cebar=300">cebar manguera 300 ms</a></div>
<div class="res" id="tRes"></div></section>
</div>
<footer><a href="/update">Actualizar firmware</a><a href="/simple">Vista sencilla</a><a href="/api/estado">API JSON</a></footer>
</div>
<script>
var COL=[[255,0,0],[230,20,20],[220,10,10],[255,90,0],[255,140,0],[255,200,0],[255,255,0],[0,200,0],[0,180,120],[0,90,255],[0,0,180],[75,0,130],[140,0,200],[148,0,211],[90,0,140]];
var H=[],MAXH=300,last=null,fails=0;
function $(i){return document.getElementById(i)}
function rgb(c){return "rgb("+c[0]+","+c[1]+","+c[2]+")"}
(function(){var s=[];for(var i=0;i<15;i++)s.push(rgb(COL[i])+" "+(i/14*100).toFixed(1)+"%");$("scale").style.background="linear-gradient(90deg,"+s.join(",")+")";})();
function lum(hex){if(!hex||hex.length<7)return 1;var r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);return(0.299*r+0.587*g+0.114*b)/255}
function f(v,d){return(v===null||v===undefined||isNaN(v))?"--":Number(v).toFixed(d)}
function pct(v){return Math.max(0,Math.min(100,v/14*100))+"%"}
function dur(s){s=s|0;var m=Math.floor(s/60);return m?m+" min "+(s%60)+" s":s+" s"}
function pill(id,on,txt){var e=$(id);e.className="pill "+(on?"on":"off");e.textContent=txt}
function render(d){
 var hx=d.color||"#888888";$("phbox").style.background=hx;$("phbox").style.color=lum(hx)>.6?"#111":"#fff";
 $("ph").textContent=f(d.ph,2);$("estado").textContent=d.estado||"--";$("umb").textContent=f(d.umbral,2);
 $("msg").textContent=d.mensaje||"";
 $("dot").className="dot"+(d.bomba?" on":"");$("bomba").textContent=d.bomba?"Bomba ENCENDIDA":"Bomba apagada";
 if(d.ph!==null){$("mkPh").style.display="";$("mkPh").style.left=pct(d.ph);$("mkPhT").textContent="pH "+f(d.ph,2)}else $("mkPh").style.display="none";
 $("mkUm").style.left=pct(d.umbral);$("mkUmT").textContent="umbral "+f(d.umbral,1);
 $("kMl").textContent=f(d.dosis_ml,1)+" mL";$("kPu").textContent=d.pulsos;
 $("kGa").textContent=d.ganancia?f(d.ganancia*1000,2):"--";$("kCo").textContent=f(d.consumo_total_ml,1)+" mL";
 $("kAdc").textContent=f(d.adc,1);$("kV").textContent=f(d.voltaje,3)+" V";$("kUp").textContent=dur(d.uptime_s);$("kCol").textContent=hx;
 pill("pWifi",d.wifi_sta,d.wifi_sta?"Wi-Fi: conectado":"Wi-Fi: sin Internet");pill("pBlynk",d.blynk,d.blynk?"Blynk: conectado":"Blynk: desconectado");
 var m=d.modulos||{};$("mX03").textContent="FW "+(m.xn03||"--");$("mX13").textContent="FW "+(m.xn13||"--");$("mX44").textContent="FW "+(m.xn44||"--");
 var rel=$("mRel");rel.textContent=(d.rele_confirmado?"✔ OK":"✖ "+(d.rele_respuesta||"sin respuesta"))+" · hace "+dur(d.rele_hace_s);rel.style.color=d.rele_confirmado?"":"#cf222e";
 var busy=d.estado!=="REPOSO";$("bStart").disabled=busy;$("bStop").disabled=!busy;
 var iu=$("inU");if(document.activeElement!==iu)iu.value=f(d.umbral,1);
 ses(d.ultima_sesion);
}
function ses(s){
 if(!s){$("ses").textContent="Aún no hay dosificaciones en esta sesión.";return}
 var e=s.error,a=e===null?null:Math.abs(e),cl=a===null?"":(e<-0.05?"bad":(a<=0.1?"good":"mid"));
 $("ses").innerHTML='<div class="err '+cl+'">'+(e===null?"--":(e>=0?"+":"")+e.toFixed(2))+' pH</div>'+
 '<div style="color:var(--mu)">error respecto al umbral · '+esc(s.motivo)+(s.exito?"":" (sin completar)")+'</div>'+
 '<div class="sesg"><span>pH inicial</span><b>'+f(s.ph_inicial,2)+'</b><span>pH final</span><b>'+f(s.ph_final,2)+'</b>'+
 '<span>Umbral</span><b>'+f(s.umbral,2)+'</b><span>Solución</span><b>'+f(s.ml,1)+' mL</b>'+
 '<span>Pulsos</span><b>'+s.pulsos+'</b><span>Duración</span><b>'+dur(s.duracion_s)+'</b></div>';
}
function esc(t){return String(t||"").replace(/[&<>"]/g,function(c){return{"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]})}
function draw(){
 var cv=$("cv"),r=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;
 if(cv.width!==w*r||cv.height!==h*r){cv.width=w*r;cv.height=h*r}
 var x=cv.getContext("2d");x.setTransform(r,0,0,r,0,0);x.clearRect(0,0,w,h);
 var cs=getComputedStyle(document.documentElement),cTx=cs.getPropertyValue("--tx").trim(),cMu=cs.getPropertyValue("--mu").trim(),cBd=cs.getPropertyValue("--bd").trim(),cAc=cs.getPropertyValue("--ac").trim();
 var L=34,R=8,T=8,B=20,pw=w-L-R,ph=h-T-B,now=Date.now(),t0=now-MAXH*1000;
 var lo=99,hi=-99;H.forEach(function(p){if(p.ph!==null){lo=Math.min(lo,p.ph);hi=Math.max(hi,p.ph)}if(p.u!==null){lo=Math.min(lo,p.u);hi=Math.max(hi,p.u)}});
 if(lo>hi){lo=4;hi=8}lo=Math.max(0,Math.floor((lo-.3)*2)/2);hi=Math.min(14,Math.ceil((hi+.3)*2)/2);if(hi-lo<1){hi=Math.min(14,lo+1)}
 function X(t){return L+(t-t0)/(MAXH*1000)*pw}function Y(v){return T+(hi-v)/(hi-lo)*ph}
 x.fillStyle="rgba(26,127,55,.22)";
 for(var i=0;i<H.length;i++){if(H[i].b){var xa=X(H[i].t),xb=i+1<H.length?X(H[i+1].t):X(now);x.fillRect(xa,T,Math.max(2,xb-xa),ph)}}
 x.strokeStyle=cBd;x.fillStyle=cMu;x.lineWidth=1;x.font="11px system-ui,sans-serif";x.textAlign="right";x.textBaseline="middle";
 var st=(hi-lo)>3?1:.5;for(var v=Math.ceil(lo/st)*st;v<=hi+1e-6;v+=st){var y=Y(v);x.beginPath();x.moveTo(L,y);x.lineTo(w-R,y);x.stroke();x.fillText(v.toFixed(st<1?1:0),L-5,y)}
 x.textAlign="center";x.textBaseline="top";["-5 min","-4","-3","-2","-1","ahora"].forEach(function(s,k){x.fillText(s,L+k/5*pw,h-B+4)});
 x.setLineDash([6,4]);x.strokeStyle=cTx;x.lineWidth=1.5;x.beginPath();var st2=false;
 H.forEach(function(p){if(p.u===null)return;var px=X(p.t),py=Y(p.u);if(!st2){x.moveTo(px,py);st2=true}else x.lineTo(px,py)});x.stroke();x.setLineDash([]);
 x.strokeStyle=cAc;x.lineWidth=2.2;x.beginPath();var s3=false;
 H.forEach(function(p){if(p.ph===null){s3=false;return}var px=X(p.t),py=Y(p.ph);if(!s3){x.moveTo(px,py);s3=true}else x.lineTo(px,py)});x.stroke();
 if(!H.length){x.fillStyle=cMu;x.textAlign="center";x.textBaseline="middle";x.fillText("Esperando datos…",L+pw/2,T+ph/2)}
}
function poll(){
 var ctl=new AbortController(),to=setTimeout(function(){ctl.abort()},2500);
 fetch("/api/estado",{cache:"no-store",signal:ctl.signal}).then(function(r){return r.json()}).then(function(d){
  clearTimeout(to);fails=0;last=d;$("banner").style.display="none";pill("pConn",true,"ESP32 en línea");
  H.push({t:Date.now(),ph:d.ph,u:d.umbral,b:d.bomba});var lim=Date.now()-MAXH*1000;while(H.length&&H[0].t<lim)H.shift();
  render(d);draw();
 }).catch(function(){clearTimeout(to);fails++;if(fails>=2){$("banner").style.display="block";pill("pConn",false,"sin conexión")}})
 .then(function(){setTimeout(poll,1000)});
}
function cmd(q,out){$(out).textContent="Enviando…";return fetch("/cmd?"+q,{cache:"no-store",redirect:"manual"}).then(function(){$(out).textContent="Orden enviada"}).catch(function(){$(out).textContent="Error al enviar la orden"})}
$("bStart").onclick=function(){cmd("c=start","cRes")};
$("bStop").onclick=function(){cmd("c=stop","cRes")};
$("bU").onclick=function(){var v=parseFloat($("inU").value);if(isNaN(v)||v<4.5||v>7){$("cRes").textContent="El umbral debe estar entre 4.5 y 7";return}cmd("u="+v.toFixed(1),"cRes")};
function test(q){$("tRes").textContent="Enviando…";fetch("/test?"+q,{cache:"no-store"}).then(function(r){return r.text()}).then(function(t){$("tRes").textContent=t}).catch(function(){$("tRes").textContent="Error de conexión"})}
document.querySelectorAll("[data-t]").forEach(function(b){b.onclick=function(){test(b.getAttribute("data-t"))}});
document.querySelectorAll("[data-b]").forEach(function(a){a.onclick=function(e){e.preventDefault();if(confirm("¿La manguera está en líquido? Esto enciende la bomba"))test(a.getAttribute("data-b"))}});
window.addEventListener("resize",draw);
poll();
</script></body></html>)HTML";
