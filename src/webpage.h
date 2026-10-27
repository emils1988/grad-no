#pragma once
#include <Arduino.h>

// Weboberfläche (eine Seite, kein externes CSS/JS – funktioniert auch ohne Internet)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Grad no!</title>
<style>
:root{--bg:#0e0f14;--card:#171922;--line:#262935;--text:#e8e8ee;--mute:#8b8e9c;--amber:#ffb000;--red:#ff5a4e;--green:#4fc86e;--blue:#0a64b9}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:14px 16px;border-bottom:2px solid var(--amber);background:#13151d;position:sticky;top:0;z-index:5}
header h1{font-size:18px;margin:0;font-weight:650;letter-spacing:.2px}header .sub{margin-left:auto;color:var(--mute);font-size:13px}
main{max-width:860px;margin:0 auto;padding:16px;display:grid;gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}
.card h2{font-size:15px;margin:0 0 12px;color:var(--amber);font-weight:600;text-transform:uppercase;letter-spacing:.6px}
label{display:block;color:var(--mute);font-size:13px;margin:0 0 4px}
input,select{width:100%;background:#0f1118;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:9px 10px;font:inherit}
input:focus,select:focus{outline:none;border-color:var(--amber)}
input[type=checkbox]{width:auto;margin-right:8px;accent-color:var(--amber)}
input[type=range]{padding:0;accent-color:var(--amber)}
button{background:#262a38;color:var(--text);border:1px solid #333849;border-radius:8px;padding:8px 13px;font:inherit;cursor:pointer}
button:hover{border-color:var(--amber)}button.pri{background:var(--amber);color:#111;border-color:var(--amber);font-weight:600}
button.danger{border-color:#5a2a2a;color:#ffb3ad}button.sm{padding:5px 9px;font-size:13px}
.row{display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap}.row>*{flex:1;min-width:120px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(max-width:560px){.grid2{grid-template-columns:1fr}}
.muted{color:var(--mute);font-size:13px}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
td,th{padding:7px 6px;border-bottom:1px solid var(--line);text-align:left}th{color:var(--mute);font-weight:500;font-size:12px}
td.t{color:var(--amber);white-space:nowrap}td.d{color:var(--red);white-space:nowrap}td.m{text-align:right;white-space:nowrap}
.badge{display:inline-block;padding:2px 7px;border-radius:5px;background:#a81923;color:#fff;font-size:13px;white-space:nowrap}
.badge.bus{background:#4b4e5c}.badge.s{background:var(--blue)}
.cancel td{color:var(--red)!important;text-decoration:line-through}
.chips{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px}
.chip{padding:5px 11px;border-radius:999px;border:1px solid var(--line);background:#0f1118;cursor:pointer;font-size:13px}
.chip.on{border-color:var(--amber);color:var(--amber)}
.st{border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:10px;background:#12141c}
.st .head{display:flex;gap:8px;align-items:center;margin-bottom:10px}.st .head input{flex:1;font-weight:600}
.st .id{color:var(--mute);font-size:12px;white-space:nowrap}
.res{display:flex;justify-content:space-between;align-items:center;padding:7px 4px;border-bottom:1px solid var(--line)}
#savebar{position:sticky;bottom:12px;display:none;justify-content:space-between;align-items:center;background:#2a2410;border:1px solid var(--amber);border-radius:12px;padding:10px 14px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#222634;border:1px solid var(--line);padding:9px 16px;border-radius:9px;display:none;z-index:9}
progress{width:100%;height:10px;accent-color:var(--amber)}
a{color:var(--amber)}
</style></head><body>
<header><h1>Grad no!</h1><span class="sub" id="hdr">…</span></header>
<main>

<section class="card">
  <h2>Live-Vorschau</h2>
  <div class="chips" id="chips"></div>
  <div id="board" class="muted">Lade …</div>
  <div class="row" style="margin-top:10px"><span class="muted" id="boardInfo"></span>
    <button class="sm" style="flex:0" onclick="refresh()">Jetzt abfragen</button></div>
</section>

<section class="card">
  <h2>Haltestellen</h2>
  <div id="stations"></div>
  <div class="row">
    <div><label>Haltestelle suchen (Bahnhof oder Bushaltestelle)</label>
      <input id="q" placeholder="z.B. Villach Hbf" onkeydown="if(event.key==='Enter')search()"></div>
    <button style="flex:0" onclick="search()">Suchen</button>
  </div>
  <div id="results"></div>
  <p class="muted">Bis zu 8 Haltestellen. Am Display rechts/links tippen zum Weiterschalten.
  <b>Gehzeit</b> blendet Abfahrten aus, die du nicht mehr erreichst. <b>Richtung</b> filtert nach Zielen (Komma-getrennt, Teilwörter reichen).</p>
</section>

<section class="card">
  <h2>Anzeige</h2>
  <div class="grid2">
    <div><label>Abfrage alle … Sekunden</label><input type="number" id="interval" min="20" max="600"></div>
    <div><label>Ausrichtung</label><select id="rotation"><option value="1">Querformat (USB rechts)</option><option value="3">Querformat gedreht (USB links)</option></select></div>
    <div><label>Helligkeit Tag: <span id="bdv"></span></label><input type="range" id="brightDay" min="5" max="255"></div>
    <div><label>Helligkeit Nacht: <span id="bnv"></span> (0 = aus, Tippen weckt)</label><input type="range" id="brightNight" min="0" max="255"></div>
    <div><label>Nacht von (Uhr)</label><input type="number" id="nightFrom" min="0" max="23"></div>
    <div><label>Nacht bis (Uhr)</label><input type="number" id="nightTo" min="0" max="23"></div>
  </div>
  <p><label style="display:flex;align-items:center"><input type="checkbox" id="touchMirror">Touch vertauscht (links/rechts verkehrt)</label>
  <label style="display:flex;align-items:center"><input type="checkbox" id="invert">Farben invertieren (falls Display „negativ“ aussieht)</label></p>
</section>

<div id="savebar"><span>Ungespeicherte Änderungen</span><button class="pri" onclick="save()">Speichern</button></div>

<section class="card">
  <h2>System</h2>
  <div id="sys" class="muted">…</div>
  <h3 style="font-size:14px;margin:16px 0 8px">Firmware-Update</h3>
  <div class="row"><div><input type="file" id="fw" accept=".bin"></div>
    <button style="flex:0" class="pri" onclick="upload()">Hochladen</button></div>
  <progress id="prog" value="0" max="100" style="display:none;margin-top:8px"></progress>
  <p class="muted">Datei aus PlatformIO: <code>.pio/build/cyd/firmware.bin</code></p>
  <h3 style="font-size:14px;margin:16px 0 8px">Passwort (Benutzer: admin)</h3>
  <div class="row"><div><input type="password" id="pw" placeholder="neues Passwort"></div>
    <button style="flex:0" onclick="setPw()">Ändern</button></div>
  <div class="row" style="margin-top:16px">
    <button onclick="post('/api/reboot','Neustart …')">Neustart</button>
    <button class="danger" onclick="if(confirm('WLAN-Zugangsdaten löschen? Danach startet der Setup-Hotspot „GradNo-Setup“.'))post('/api/wifireset','WLAN zurückgesetzt')">WLAN zurücksetzen</button>
    <button onclick="window.open('/api/raw')">Debug: Rohdaten</button>
  </div>
</section>
</main>
<div id="toast"></div>

<script>
let cfg=null, dirty=false, preview=0, lastResults=[];
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2600)}
function setDirty(){dirty=true;$('savebar').style.display='flex'}
async function api(url,opt){const r=await fetch(url,opt);let j={};try{j=await r.json()}catch(e){}if(!r.ok)throw new Error(j.error||('HTTP '+r.status));return j}
async function post(url,msg){try{await api(url,{method:'POST'});toast(msg)}catch(e){toast(e.message)}}

const FILTERS=['Alle Verkehrsmittel','Nur Züge','Nur Bus/Tram/U-Bahn'];
const settingIds=['interval','rotation','brightDay','brightNight','nightFrom','nightTo'];

async function load(){
  cfg=await api('/api/config');
  preview=cfg.current||0;
  for(const id of settingIds)$(id).value=cfg[id];
  $('touchMirror').checked=cfg.touchMirror;$('invert').checked=cfg.invert;
  $('bdv').textContent=cfg.brightDay;$('bnv').textContent=cfg.brightNight;
  renderStations();loadBoard();loadSys();
}
for(const id of ['interval','rotation','brightDay','brightNight','nightFrom','nightTo','touchMirror','invert'])
  $(id).addEventListener('input',()=>{setDirty();$('bdv').textContent=$('brightDay').value;$('bnv').textContent=$('brightNight').value});

function renderStations(){
  const box=$('stations');
  if(!cfg.stations.length){box.innerHTML='<p class="muted">Noch keine Haltestelle – unten suchen und hinzufügen.</p>'}
  else box.innerHTML=cfg.stations.map((s,i)=>`
   <div class="st">
    <div class="head"><input value="${esc(s.name)}" oninput="cfg.stations[${i}].name=this.value;setDirty();renderChips()">
     <span class="id">ID ${esc(s.id)}</span>
     <button class="sm" onclick="move(${i},-1)" ${i==0?'disabled':''}>↑</button>
     <button class="sm" onclick="move(${i},1)" ${i==cfg.stations.length-1?'disabled':''}>↓</button>
     <button class="sm danger" onclick="del(${i})">✕</button></div>
    <div class="row">
     <div><label>Verkehrsmittel</label><select onchange="cfg.stations[${i}].filter=+this.value;setDirty()">
      ${FILTERS.map((f,k)=>`<option value="${k}" ${s.filter==k?'selected':''}>${f}</option>`).join('')}</select></div>
     <div><label>Richtung (optional)</label><input value="${esc(s.direction)}" placeholder="z.B. Villach, Klagenfurt" oninput="cfg.stations[${i}].direction=this.value;setDirty()"></div>
     <div style="max-width:120px"><label>Gehzeit (min)</label><input type="number" min="0" max="60" value="${s.walk||0}" oninput="cfg.stations[${i}].walk=+this.value;setDirty()"></div>
    </div></div>`).join('');
  renderChips();
}
function renderChips(){
  $('chips').innerHTML=cfg.stations.map((s,i)=>`<span class="chip ${i==preview?'on':''}" onclick="showOn(${i})">${esc(s.name)}</span>`).join('');
}
function move(i,d){const a=cfg.stations;[a[i],a[i+d]]=[a[i+d],a[i]];setDirty();renderStations()}
function del(i){cfg.stations.splice(i,1);if(preview>=cfg.stations.length)preview=0;setDirty();renderStations()}

async function search(){
  const q=$('q').value.trim();if(q.length<2)return;
  $('results').innerHTML='<p class="muted">Suche …</p>';
  try{
    const j=await api('/api/search?q='+encodeURIComponent(q));
    if(!j.results.length){$('results').innerHTML='<p class="muted">Nichts gefunden.</p>';return}
    lastResults=j.results;
    $('results').innerHTML=j.results.map((r,i)=>`<div class="res"><span>${esc(r.name)} <span class="muted">(${esc(r.id)})</span></span>
      <button class="sm" onclick="add(lastResults[${i}])">+ Hinzufügen</button></div>`).join('');
  }catch(e){$('results').innerHTML='<p class="muted">'+esc(e.message)+'</p>'}
}
function add(r){
  if(cfg.stations.length>=8){toast('Maximal 8 Haltestellen');return}
  if(cfg.stations.some(s=>s.id==r.id)){toast('Schon in der Liste');return}
  cfg.stations.push({id:r.id,name:r.name,filter:0,direction:'',walk:0});
  $('results').innerHTML='';$('q').value='';setDirty();renderStations();toast(r.name+' hinzugefügt – Speichern nicht vergessen');
}

async function save(){
  for(const id of settingIds)cfg[id]=+$(id).value;
  cfg.touchMirror=$('touchMirror').checked;cfg.invert=$('invert').checked;
  cfg.current=Math.min(preview,Math.max(0,cfg.stations.length-1));
  try{await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
    dirty=false;$('savebar').style.display='none';toast('Gespeichert');setTimeout(loadBoard,2500)}
  catch(e){toast('Fehler: '+e.message)}
}

async function showOn(i){
  if(dirty){toast('Bitte zuerst speichern');return}
  preview=i;renderChips();
  try{await api('/api/show?i='+i,{method:'POST'})}catch(e){}
  setTimeout(loadBoard,1500);
}
async function refresh(){await post('/api/refresh','Wird abgefragt …');setTimeout(loadBoard,2500)}

function badgeClass(l){l=l.toLowerCase();if(/^(bus|ast|tram|str|nachtbus|citybus|rufbus)|^u ?\d/.test(l))return'bus';if(/^s ?\d/.test(l))return's';return''}
async function loadBoard(){
  if(!cfg||!cfg.stations.length){$('board').innerHTML='';return}
  try{
    const b=await api('/api/board?i='+preview);
    if(b.current!==undefined&&b.current!=preview&&!dirty){preview=b.current;renderChips();return loadBoard()}
    const now=Date.now()/1000;
    if(!b.deps.length){$('board').innerHTML='<p class="muted">'+(b.error?esc(b.error):(b.age<0?'Noch nicht abgefragt':'Keine Abfahrten'))+'</p>'}
    else $('board').innerHTML='<table><tr><th>Zeit</th><th></th><th>Linie</th><th>Ziel</th><th>Gl./Steig</th><th style="text-align:right">in</th></tr>'+
      b.deps.map(d=>{const m=d.when?Math.floor((d.when-now)/60):null;
        return `<tr class="${d.cancelled?'cancel':''}"><td class="t">${esc(d.time)}</td>
        <td class="d">${d.cancelled?'fällt aus':(d.delay>0?'+'+d.delay:'')}</td>
        <td><span class="badge ${badgeClass(d.line)}">${esc(d.line)}</span></td><td>${esc(d.dest)}</td>
        <td class="muted">${esc(d.platform)}</td><td class="m">${m===null?'':(m<=0?'jetzt':(m<60?m+' min':''))}</td></tr>`}).join('')+'</table>';
    $('boardInfo').textContent=b.age>=0?('Stand vor '+b.age+' s'+(b.ok?'':' – letzter Versuch: '+b.error)):'';
  }catch(e){$('board').innerHTML='<p class="muted">'+esc(e.message)+'</p>'}
}

async function loadSys(){
  try{const s=await api('/api/status');
    $('hdr').textContent=s.host+' · '+s.ip+' · v'+s.version;
    const up=s.uptime>86400?Math.floor(s.uptime/86400)+' d':Math.floor(s.uptime/3600)+' h '+Math.floor(s.uptime%3600/60)+' min';
    $('sys').innerHTML=`WLAN <b>${esc(s.ssid)}</b> (${s.rssi} dBm) · IP ${esc(s.ip)} · Laufzeit ${up}<br>
      Speicher frei ${Math.round(s.heap/1024)} kB (min. ${Math.round(s.minHeap/1024)} kB) · Firmware ${Math.round(s.sketch/1024)} kB / Platz ${Math.round(s.free/1024)} kB
      ${s.timeOk?'':'<br><span style="color:var(--red)">Uhrzeit noch nicht synchronisiert</span>'}`;
  }catch(e){}
}

async function setPw(){
  const p=$('pw').value;if(p.length<4){toast('Mindestens 4 Zeichen');return}
  try{await api('/api/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pass:p})});
    toast('Passwort geändert – Browser fragt gleich neu');$('pw').value=''}catch(e){toast(e.message)}
}

function upload(){
  const f=$('fw').files[0];if(!f){toast('Bitte .bin-Datei wählen');return}
  if(!f.name.endsWith('.bin')&&!confirm('Datei endet nicht auf .bin – trotzdem hochladen?'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();const p=$('prog');p.style.display='block';p.value=0;
  x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded/e.total*100};
  x.onload=()=>{if(x.status==200){toast('Update OK – Neustart, Seite lädt in 15 s neu');setTimeout(()=>location.reload(),15000)}else{toast(x.responseText||'Update fehlgeschlagen');p.style.display='none'}};
  x.onerror=()=>{toast('Verbindung abgebrochen');p.style.display='none'};
  x.open('POST','/update');x.send(fd);
}

window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue=''}});
load().catch(e=>toast(e.message));
setInterval(()=>{if(!document.hidden)loadBoard()},20000);
setInterval(()=>{if(!document.hidden)loadSys()},60000);
</script>
</body></html>
)rawliteral";
