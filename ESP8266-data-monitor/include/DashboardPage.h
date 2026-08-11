#pragma once

#include <Arduino.h>

const char kDashboardPage[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CJ Power Monitor</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif}body{margin:0;background:#121218;color:#eee}.wrap{max-width:1000px;margin:auto;padding:18px}h1{text-align:center}.status{padding:10px;border-radius:8px;background:#252531;margin-bottom:12px}.ok{color:#5ee58d}.bad{color:#ff6b78}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px}.card{background:#1d1d25;border:1px solid #454553;border-radius:9px;padding:12px}.label{color:#aaa;font-size:.82rem}.value{color:#ef3da9;font-size:1.15rem;margin-top:5px}.section{margin:14px 0}.keys .value{font-size:.9rem}.on{color:#5ee58d}.off{color:#777}</style></head>
<body><div class="wrap"><h1>CJ Power Data Monitor</h1><div id="status" class="status">Connecting...</div>
<div class="section"><h2>Data View</h2><div id="states" class="grid"></div></div>
<div class="section"><h2>Parameter View</h2><div id="params" class="grid"></div></div>
<div class="section keys"><h2>Function Keys</h2><div id="keys" class="grid"></div></div></div>
<script>
const card=(label,value,cls='')=>`<div class="card"><div class="label">${label}</div><div class="value ${cls}">${value}</div></div>`;
function stateName(type,v){const maps={drive:{0:'Ready',1:'Drive',2:'Reverse'},run:{0:'Stop',1:'Run',2:'MTPA'},gear:{1:'Gear1',2:'Gear2',3:'Gear3'},mode:{0:'Running',1:'Parked',2:'Halt'}};return (maps[type][v]??'Unknown')+` (${v})`}
function key(label,v){return card(label,v?'ON':'OFF',v?'on':'off')}
async function refresh(){try{const r=await fetch('/api/live',{cache:'no-store'}),d=await r.json();const age=d.ageMs;
document.getElementById('status').innerHTML=`<span class="${d.valid&&age<1000?'ok':'bad'}">${d.valid&&age<1000?'LIVE':'STALE'}</span> &nbsp; age ${age} ms &nbsp; packets ${d.stats.validResponses}/${d.stats.requests} &nbsp; errors ${d.stats.checksumErrors+d.stats.malformedFrames} &nbsp; timeouts ${d.stats.timeouts}`;
document.getElementById('states').innerHTML=card('Drive State',stateName('drive',d.driveStateRaw))+card('Running State',stateName('run',d.runningStateRaw))+card('Vehicle Gear',stateName('gear',d.vehicleGearRaw))+card('Vehicle Mode',stateName('mode',d.vehicleModeRaw));
document.getElementById('params').innerHTML=card('Motor Speed',d.motorSpeedRpm+' RPM')+card('Fault Code','0x'+d.faultCode.toString(16).padStart(8,'0').toUpperCase())+card('DC Current',d.dcCurrentA.toFixed(1)+' A')+card('DC Voltage',d.dcVoltageV.toFixed(1)+' V')+card('Phase Current',d.phaseCurrentRaw+' raw')+card('Motor Power',d.motorPowerRaw+' raw')+card('Throttle Voltage',d.throttleVoltageV.toFixed(2)+' V')+card('MCU Temperature',d.mcuTemperatureC+' C')+card('Motor Temperature',d.motorTemperatureC+' C')+card('Hall Value',d.hallValue)+card('P1',d.p1)+card('P2',d.p2);
document.getElementById('keys').innerHTML=key('Brake',d.brake)+key('Anti-theft',d.antiTheft)+key('Park',d.park)+key('Cruise',d.cruise)+key('Reverse',d.reverse)+key('Side Stand',d.sideStand)+key('BOOST',d.boost)+key('Accelerate Gear',d.accelerateGear)+key('Decelerate Gear',d.decelerateGear);
}catch(e){document.getElementById('status').innerHTML='<span class="bad">Dashboard connection failed</span>'}}refresh();setInterval(refresh,300);
</script></body></html>)HTML";
