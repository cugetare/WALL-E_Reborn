'use strict';
const JointNames=['Left arm','Right arm','Neck tilt · SC15','Head tilt','Head pan','Left eye','Right eye','Left brow','Right brow'];
const ButtonNames=['A','B','X','Y','LB','RB','LT click','RT click','Select','Start','L3','R3','D-pad up','D-pad down','D-pad left','D-pad right'];
function validLimits(min,rest,max){return [min,rest,max].every(Number.isInteger)&&min>=0&&max<=1023&&min<rest&&rest<max}
function nextDraft(stored,draft){return draft??{minimum:stored.min,rest:stored.rest,maximum:stored.max,reverse:stored.inverted}}
function chordMask(indices){return indices.reduce((mask,index)=>mask|(1<<index),0)}
function safeMode(state){return state.ota?'Updating':state.stopped?'Stopped':state.service?'Service armed':'Play armed'}
function validMediaPath(path){return /^(sounds|profiles|recordings|firmware|tmp)(\/[A-Za-z0-9_.-]+)*$/.test(path||'')}
if(typeof module!=='undefined') module.exports={validLimits,nextDraft,chordMask,safeMode,validMediaPath};
if(typeof document!=='undefined') {
const $=id=>document.getElementById(id);
let key=sessionStorage.getItem('walle-key')||'';
const client=Array.from(crypto.getRandomValues(new Uint8Array(12)),x=>x.toString(16).padStart(2,'0')).join('');
let state=null,armed=false,requestPending=false,statusPending=false,gamepadPending=false,wasOffline=false;
let drafts={},calLoaded=-1,automationRules=[],catalogue=[],scanTimer=null,wifiTimer=null;
let sweepAbort=null,commandChain=Promise.resolve(),manualDrive=false,initialTab=true;
const headers=()=>({'X-WALLE-Key':key,'X-WALLE-Client':client});
function notice(text,error=false){$('notice').textContent=text;$('notice').classList.toggle('error',error)}
async function api(path,data,options={}) {
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),options.timeout||4000);
 try {
  const r=await fetch('/api/'+path,{method:data===undefined?'GET':'POST',headers:{...headers(),'Content-Type':'application/json'},
    body:data===undefined?undefined:JSON.stringify(data),signal:abort.signal,cache:'no-store'});
  const text=await r.text();if(!r.ok)throw new Error(text||'HTTP '+r.status);
  return r.headers.get('content-type')?.includes('application/json')?JSON.parse(text):text;
 } finally{clearTimeout(timer)}
}
async function textApi(path,data,options={}) {
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),options.timeout||4000);
 try {
  const r=await fetch('/api/'+path,{method:data===undefined?'GET':'POST',headers:{...headers(),'Content-Type':'application/json'},
    body:data===undefined?undefined:JSON.stringify(data),signal:abort.signal,cache:'no-store'});
  const text=await r.text();if(!r.ok)throw new Error(text||'HTTP '+r.status);return text;
 } finally{clearTimeout(timer)}
}
function cancelSequence(){if(sweepAbort)sweepAbort.abort();sweepAbort=null}
async function stop(silent=false){
 armed=false;manualDrive=false;cancelSequence();$('throttle').value=$('steering').value=0;
 try{const result=await api('command',{command:'stop'},{timeout:1000});if(!silent)notice(result)}
 catch(e){if(!silent)notice('Stop could not be confirmed; use the physical cutoff. '+e.message,true)}
}
async function command(text,report=false){
 const result=await api('command',{command:text,epoch:state?.epoch});if(report)notice(result);return result;
}
function action(id,fn){$(id).addEventListener('click',async()=>{const b=$(id);if(b.disabled)return;b.disabled=true;
 try{await fn()}catch(e){notice(e.message,true)}finally{b.disabled=false;updateEnabled()}})}
function tab(id){
 for(const b of document.querySelectorAll('[role=tab]')){const selected=b.getAttribute('aria-controls')===id;b.setAttribute('aria-selected',String(selected));b.tabIndex=selected?0:-1}
 for(const p of document.querySelectorAll('[role=tabpanel]'))p.hidden=p.id!==id;
}
document.querySelectorAll('[role=tab]').forEach((b,i,all)=>{
 b.addEventListener('click',()=>tab(b.getAttribute('aria-controls')));
 b.addEventListener('keydown',e=>{if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();const n=(i+(e.key==='ArrowRight'?1:all.length-1))%all.length;all[n].click();all[n].focus()}});
});
function updateEnabled(){
 const play=!!state&&!state.service&&!state.stopped&&armed&&!state.playing;
 const service=!!state&&state.service&&!state.stopped&&armed;
 document.querySelectorAll('[data-motion]').forEach(e=>e.disabled=!play);
 document.querySelectorAll('[data-service-motion]').forEach(e=>e.disabled=!service);
 $('armstatus').textContent=state?safeMode(state):'Offline';
}
function draft(){return drafts[+$('caljoint').value]}
function saveDraft(){
 drafts[+$('caljoint').value]={minimum:+$('minimum').value,rest:+$('rest').value,maximum:+$('maximum').value,reverse:$('reverse').checked};
 $('draftstatus').textContent='Unsaved draft — status refresh will not overwrite it.';
}
function loadDraft(){
 if(!state)return;
 const index=+$('caljoint').value,x=nextDraft(state.joints[index],drafts[index]);drafts[index]=x;
 for(const k of ['minimum','rest','maximum'])$(k).value=x[k];
 $('reverse').checked=x.reverse;$('raw').value=x.rest;calLoaded=index;
 $('actual').textContent=$('load').textContent='Not read';$('draftstatus').textContent='Draft retained until explicitly saved or discarded.';
}
for(const id of ['minimum','rest','maximum','reverse'])$(id).addEventListener('input',saveDraft);
$('caljoint').addEventListener('change',()=>{cancelSequence();loadDraft()});
async function refresh(){
 if(statusPending)return;statusPending=true;
 try{
  const next=await api('status');state=next;
  if(wasOffline){notice('Connection restored. Select a mode and rearm when safe.');wasOffline=false}
  if(next.stopped){armed=false;manualDrive=false;cancelSequence()}
  $('connection').textContent='Online';$('mode').textContent=safeMode(next);
  $('network').textContent=next.wifiMode==='station'?next.ssid+' · '+next.ip:'Setup AP · '+next.ip;
  $('battery').textContent=next.batteryOk?'Battery verified · 3S':'Battery absent / outside safe range';
  $('media').textContent=next.mediaOnline?'Media + SD online':'Media / SD unavailable';
  $('wifistatus').textContent=next.wifiMode==='station'?'Connected to '+next.ssid+' at '+next.ip:'Setup AP active';
  $('firmwarestatus').textContent='Version '+next.version+' · uptime '+Math.floor(next.uptimeMs/1000)+' s · free heap '+Math.round(next.heap/1024)+' KiB · HTTP stack free '+next.httpStackFree+' B';
  if(!$('ssid').value&&next.wifiMode==='station')$('ssid').value=next.ssid;
  $('axes').replaceChildren();
  next.joints.forEach((j,i)=>{const tr=document.createElement('tr');for(const value of [JointNames[i],j.id||'Unassigned',j.present?'Present':'Not verified',(next.calibratedMask&(1<<i))?'Saved':'Pending']){const td=document.createElement('td');td.textContent=value;tr.appendChild(td)}$('axes').appendChild(tr)});
  if(calLoaded<0)loadDraft();
  if(initialTab){tab(next.ready?'control':'commissioning');initialTab=false}
  updateEnabled();
 }catch(e){wasOffline=true;$('connection').textContent='Offline';armed=false;cancelSequence();updateEnabled();notice('Connection lost. Motion lease will expire; reconnect before rearming.',true)}
 finally{statusPending=false}
}
JointNames.forEach((name,index)=>{
 $('assignjoint').add(new Option(name,index));$('caljoint').add(new Option(name,index));
 const label=document.createElement('label');label.textContent=name;
 const range=document.createElement('input');range.type='range';range.min=0;range.max=100;range.value=50;range.dataset.motion='';
 range.addEventListener('input',()=>{if(armed)command('joint,'+index+','+(+range.value/100)).catch(e=>notice(e.message,true))});
 label.appendChild(range);$('jointcontrols').appendChild(label);
});
ButtonNames.forEach((name,index)=>{const label=document.createElement('label'),input=document.createElement('input');input.type='checkbox';input.value=index;label.append(input,document.createTextNode(' '+name));$('controllerbuttons').appendChild(label)});
action('connect',async()=>{key=$('devicekey').value.trim();await command('heartbeat');sessionStorage.setItem('walle-key',key);$('devicekey').value='';$('pairing').hidden=true;notice('Browser connected. Select a mode and rearm when safe.');await refreshCatalogue();await refreshFiles().catch(()=>{})});
$('stop').addEventListener('click',()=>stop());
action('service',async()=>{armed=false;cancelSequence();await command('service',true);await refresh()});
action('playmode',async()=>{armed=false;cancelSequence();await command('play-mode',true);await refresh()});
async function arm(service){
 if(!state||state.service!==service)await command(service?'service':'play-mode');
 await command('arm',true);armed=true;await refresh();
}
action('armservice',()=>arm(true));action('armplay',()=>arm(false));
async function feedback(joint=+$('caljoint').value){
 const x=await api('servos/telemetry',{joint});
 if(joint===+$('caljoint').value){$('actual').textContent=x.position;$('load').textContent=x.load+(x.load>800?' · OVERLOAD':'')}
 if(x.load>800){await stop();throw new Error('Load protection stopped the robot')}
 return x;
}
action('readfeedback',()=>feedback());
action('center',async()=>{await api('servos/center',{joint:+$('caljoint').value});notice('Centering slowly. Confirm measured position before fitting the horn.')});
async function move(position,joint=+$('caljoint').value){
 if(!Number.isInteger(position)||position<0||position>1023)throw new Error('Raw position must be an integer from 0 to 1023');
 if(!armed)throw new Error('Rearm service before moving');
 $('raw').value=position;await command('calib,move,'+joint+','+position);
}
action('move',()=>move(+$('raw').value));
$('raw').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();$('move').click()}});
document.querySelectorAll('[data-jog]').forEach(b=>b.addEventListener('click',()=>{commandChain=commandChain.then(async()=>{const x=await feedback();await move(Math.max(0,Math.min(1023,x.position+ +b.dataset.jog)))}).catch(e=>notice(e.message,true))}));
document.querySelectorAll('[data-capture]').forEach(b=>b.addEventListener('click',async()=>{try{const x=await feedback();$(b.dataset.capture).value=x.position;saveDraft();notice('Captured measured position '+x.position)}catch(e){notice(e.message,true)}}));
function delay(ms,signal){return new Promise((resolve,reject)=>{if(signal.aborted)return reject(new Error('Cancelled'));const id=setTimeout(resolve,ms);signal.addEventListener('abort',()=>{clearTimeout(id);reject(new Error('Cancelled'))},{once:true})})}
async function testPosition(position,signal,joint){
 if(signal.aborted)throw new Error('Cancelled');
 await move(position,joint);
 const started=Date.now();
 for(;;){await delay(180,signal);const x=await feedback(joint);if(Math.abs(x.position-position)<=3)return;if(Date.now()-started>20000){await stop();throw new Error('Target not reached within 20 seconds')}}
}
async function sequence(points){
 cancelSequence();const abort=new AbortController();sweepAbort=abort;const joint=+$('caljoint').value;
 try{for(const point of points)await testPosition(point,abort.signal,joint);notice('Measured target reached. Test complete.')}
 finally{if(sweepAbort===abort)sweepAbort=null}
}
document.querySelectorAll('[data-test]').forEach(b=>b.addEventListener('click',()=>sequence([+$(b.dataset.test).value]).catch(e=>notice(e.message,true))));
action('sweep',()=>sequence([+$('minimum').value,+$('maximum').value]));
action('savecal',async()=>{
 saveDraft();const d=draft();if(!validLimits(d.minimum,d.rest,d.maximum))throw new Error('Require 0 ≤ MIN < REST < MAX ≤ 1023');
 await stop(true);notice(await api('servos/save',{joint:+$('caljoint').value,min:d.minimum,rest:d.rest,max:d.maximum,inverted:d.reverse}));
 $('draftstatus').textContent='Saved to robot. Rearm before moving.';await refresh();
});
action('finishcal',async()=>{await stop(true);notice(await api('calibration/finish',{}));await refresh()});
action('scanservos',async()=>{
 await stop(true);notice(await api('servos/scan',{}));clearTimeout(scanTimer);
 const poll=async()=>{try{const x=await api('scan');$('scanstatus').textContent=x.busy?'Scanning IDs 1–253…':'Detected IDs: '+(x.ids.join(', ')||'none');if(x.busy){scanTimer=setTimeout(poll,400);return}$('sourceid').replaceChildren();x.ids.forEach(id=>$('sourceid').add(new Option('ID '+id,id)));await refresh()}catch(e){notice(e.message,true)}};
 await poll();
});
action('assign',async()=>{await stop(true);notice(await api('servos/assign',{joint:+$('assignjoint').value,source:+$('sourceid').value,target:+$('targetid').value}));$('targetid').value=Math.min(253,+$('targetid').value+1);await refresh()});
for(const id of ['throttle','steering']){
 $(id).addEventListener('input',()=>{manualDrive=true;$(id+'value').textContent=$(id).value});
 for(const event of ['pointerup','pointercancel','keyup'])$(id).addEventListener(event,()=>{manualDrive=false;$('throttle').value=$('steering').value=0;command('drive,0,0').catch(()=>{})});
}
async function refreshCatalogue(){
 if(!key)return;const x=await api('catalog');catalogue=x.profiles;automationRules=x.automations;
 $('profilelist').replaceChildren();$('automationprofile').replaceChildren();$('automationlist').replaceChildren();
 if(!x.profiles.length)$('profilelist').textContent='No committed profiles yet.';
 for(const p of x.profiles){
  $('automationprofile').add(new Option(p.name+' ['+p.id+']',p.id));
  const row=document.createElement('div');row.className='entry';const title=document.createElement('strong');title.textContent=p.name;
  const info=document.createElement('small');info.textContent=p.id+' · '+p.size+' bytes';row.append(title,info);
  for(const [label,action]of [['Play',async()=>{notice(await api('profiles/play',{id:p.id}))}],['Delete',async()=>{if(confirm('Delete '+p.name+' from SD?')){notice(await api('profiles/delete',{id:p.id}));await refreshCatalogue()}}]]){
   const b=document.createElement('button');b.textContent=label;b.addEventListener('click',()=>action().catch(e=>notice(e.message,true)));row.appendChild(b);
  }$('profilelist').appendChild(row);
 }
 for(const a of x.automations){
  const row=document.createElement('div');row.className='entry';const title=document.createElement('strong');title.textContent=a.name+' → '+a.profile;
  row.appendChild(title);
  for(const [label,action] of [['Edit',()=>editAutomation(a)],['Delete',async()=>{notice(await api('automations/delete',{id:a.id}));await refreshCatalogue()}]]){
   const b=document.createElement('button');b.textContent=label;b.addEventListener('click',()=>Promise.resolve(action()).catch(e=>notice(e.message,true)));row.appendChild(b);
  }$('automationlist').appendChild(row);
 }
}
function selectedFilePath(){const path=$('filepath').value.trim();if(!validMediaPath(path))throw new Error('Use only sounds, profiles, recordings, firmware or tmp paths');return path}
async function refreshFiles(){
 if(!key)return;const path=selectedFilePath();const x=await api('files?path='+encodeURIComponent(path),undefined,{timeout:6000});
 $('filestatus').textContent='Listing '+(x.path||path);$('filelist').replaceChildren();
 const entries=Array.isArray(x.entries)?x.entries:[];
 if(!entries.length){$('filelist').textContent='This folder is empty.';return}
 for(const e of entries){
  const row=document.createElement('div');row.className='entry';const title=document.createElement('strong');title.textContent=e.name||e.path||'unnamed';
  const itemPath=e.path||((x.path||path).replace(/\/$/,'')+'/'+(e.name||''));const type=e.type||'file';
  const info=document.createElement('small');info.textContent=type+' · '+(Number.isFinite(e.size)?e.size+' bytes':'size unknown');
  row.append(title,info);
  if(type==='dir'){
   const b=document.createElement('button');b.textContent='Open';b.addEventListener('click',()=>{$('filepath').value=itemPath;refreshFiles().catch(err=>notice(err.message,true))});row.appendChild(b);
  } else {
   const d=document.createElement('button');d.textContent='Download';d.addEventListener('click',()=>{location.href='/api/files/download?path='+encodeURIComponent(itemPath)});row.appendChild(d);
   if(itemPath.startsWith('sounds/')){const p=document.createElement('button');p.textContent='Play';p.addEventListener('click',()=>api('files/play',{path:itemPath}).then(notice).catch(err=>notice(err.message,true)));row.appendChild(p)}
  }
  const del=document.createElement('button');del.textContent='Delete';del.className='subtle';del.addEventListener('click',async()=>{if(confirm('Delete '+itemPath+' from media SD?')){notice(await api('files/delete',{path:itemPath}));await refreshFiles()}});
  row.appendChild(del);$('filelist').appendChild(row);
 }
}
action('refreshfiles',()=>refreshFiles());
$('filefolder').addEventListener('change',()=>{$('filepath').value=$('filefolder').value;$('uploadpath').value=$('filefolder').value+'/'});
$('mediafile').addEventListener('change',()=>{const f=$('mediafile').files[0];if(f&&!$('uploadpath').value.trim())$('uploadpath').value=$('filefolder').value+'/'+f.name.replace(/[^A-Za-z0-9_.-]/g,'_')});
action('uploadmediafile',async()=>{const f=$('mediafile').files[0],path=$('uploadpath').value.trim();if(!f)throw new Error('Choose a file first');if(!validMediaPath(path))throw new Error('Invalid destination path');
 await stop(true);const result=await upload('files/upload',f,{'X-File-Path':path},$('mediaprogress'));notice(result);$('filepath').value=path.includes('/')?path.slice(0,path.lastIndexOf('/')):path;await refreshFiles()});
function editAutomation(a){
 $('automationid').value=a.id;$('automationname').value=a.name;$('automationprofile').value=a.profile;
 for(const k of ['lt','rt','hold','cooldown'])$(k).value=a[k];
 $('neutral').checked=a.neutral;$('enabled').checked=a.enabled;
 document.querySelectorAll('#controllerbuttons input').forEach(e=>e.checked=!!(a.buttons&(1<<+e.value)));
}
action('newautomation',()=>editAutomation({id:Math.max(0,...automationRules.map(a=>a.id))+1,name:'',profile:catalogue[0]?.id||'',lt:920,rt:920,hold:600,cooldown:1000,neutral:true,enabled:true,buttons:0}));
action('saveautomation',async()=>{
 const buttons=chordMask(Array.from(document.querySelectorAll('#controllerbuttons input:checked'),e=>+e.value));
 notice(await api('automations',{id:+$('automationid').value,name:$('automationname').value,profile:$('automationprofile').value,buttons,
 lt:+$('lt').value,rt:+$('rt').value,hold:+$('hold').value,cooldown:+$('cooldown').value,neutral:$('neutral').checked,enabled:$('enabled').checked}));
 await refreshCatalogue();
});
action('learn',async()=>{const s=await api('controller');if(!s.connected)throw new Error('Enable the browser controller and hold the desired buttons');
 document.querySelectorAll('#controllerbuttons input').forEach(e=>e.checked=!!(s.buttons&(1<<+e.value)));$('lt').value=s.lt;$('rt').value=s.rt;notice('Captured current controller chord')});
function upload(path,file,extra,progress){
 return new Promise((resolve,reject)=>{const x=new XMLHttpRequest();x.open('POST','/api/'+path);x.timeout=180000;
 for(const [k,v]of Object.entries({...headers(),...extra}))x.setRequestHeader(k,v);
 x.upload.onprogress=e=>{if(e.lengthComputable)progress.value=e.loaded/e.total*100};
 x.onload=()=>x.status===200?resolve(x.responseText):reject(new Error(x.responseText||'Upload failed'));
 x.onerror=()=>reject(new Error('Connection lost; completion not confirmed'));x.ontimeout=()=>reject(new Error('Transfer timed out'));
 x.send(file)});
}
action('uploadprofile',async()=>{const f=$('profilefile').files[0];if(!f||f.size>65536)throw new Error('Choose a choreography JSON up to 64 KiB');
 await stop(true);const result=await upload('profiles/upload',f,{'X-Profile-ID':$('profileid').value,'X-Profile-Name':$('profilename').value},$('profileprogress'));notice(result);await refreshCatalogue()});
action('scanwifi',async()=>{
 await stop(true);notice(await api('wifi/scan',{}));clearTimeout(wifiTimer);
 const poll=async()=>{try{const x=await api('networks');if(x.busy){$('wifiresult').textContent='Scanning…';wifiTimer=setTimeout(poll,400);return}
 if(x.result)throw new Error('Wi-Fi scan failed; retry');$('ssidselect').replaceChildren(new Option('Select a network',''));
 x.networks.forEach(n=>$('ssidselect').add(new Option(n.ssid+' · '+n.rssi+' dBm',n.ssid)));$('wifiresult').textContent='Select a network, enter its password, then save.';
 }catch(e){$('wifiresult').textContent=e.message}};await poll();
});
$('ssidselect').addEventListener('change',()=>$('ssid').value=$('ssidselect').value);
action('savewifi',async()=>{await stop(true);$('wifiresult').textContent=await api('wifi',{ssid:$('ssid').value,password:$('password').value});$('password').value=''});
action('forgetwifi',async()=>{if(confirm('Forget Wi-Fi and restart in setup AP mode?')){await stop(true);$('wifiresult').textContent=await api('wifi/forget',{})}});
action('uploadfirmware',async()=>{const f=$('firmware').files[0];if(!f)throw new Error('Choose the signed application .bin');if(!confirm('Install firmware and restart WALL-E?'))return;
 await stop(true);$('otaresult').textContent=await upload('ota',f,{},$('otaprogress'))});
action('resetcal',async()=>{if(confirm('Erase all nine assignments and calibrations? Servo IDs themselves are not changed.')){await stop(true);notice(await api('calibration/reset',{}));drafts={};calLoaded=-1;await refresh()}});
action('exportcal',async()=>{
 const data=await api('calibration/backup');const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
 const a=document.createElement('a');a.href=url;a.download='walle-calibration.json';a.click();URL.revokeObjectURL(url);
});
action('importcal',async()=>{const f=$('importfile').files[0];if(!f)throw new Error('Select a calibration backup');if(!confirm('Restore assignments and limits? A fresh scan and rearm will be required.'))return;
 await stop(true);notice(await api('calibration/restore',JSON.parse(await f.text())));drafts={};calLoaded=-1;await refresh()});
setInterval(async()=>{
 if(!key||requestPending)return;requestPending=true;
 try{
  if(armed){await command('heartbeat');if(manualDrive)await command('drive,'+(+$('throttle').value/100)+','+(+$('steering').value/100))}
 }catch(e){armed=false;cancelSequence();notice('Control connection lost. Rearm after reconnecting. '+e.message,true)}
 finally{requestPending=false}
},100);
setInterval(async()=>{
 if(!key||!$('usegamepad').checked||gamepadPending)return;gamepadPending=true;
 try{
  const pads=navigator.getGamepads?.();const pad=pads&&Array.from(pads).find(Boolean);
  if(!pad){$('gamepadstatus').textContent='No controller reports. Press a controller button; your browser may require HTTPS.';return}
  if(pad.mapping!=='standard')throw new Error('Controller mapping is not standard; driving disabled.');
  $('gamepadstatus').textContent=pad.id;
  const val=i=>pad.buttons[i]?.value||0,axis=i=>Math.abs(pad.axes[i]||0)<.15?0:pad.axes[i];
  const buttons=chordMask(pad.buttons.slice(0,16).flatMap((b,i)=>b.pressed?[i]:[]));
  await api('controller',{buttons,lt:Math.round(val(6)*1000),rt:Math.round(val(7)*1000),lx:axis(0),ly:axis(1),rx:axis(2),ry:axis(3)});
  if(armed&&state&&!state.service&&!state.playing)await command('drive,'+(val(4)>.5?-axis(1):0)+','+(val(4)>.5?axis(0):0));
 }catch(e){$('gamepadstatus').textContent=e.message}
 finally{gamepadPending=false}
},100);
window.addEventListener('blur',()=>{if(armed)stop(true)});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&armed)stop(true)});
window.addEventListener('beforeunload',e=>{if(Object.keys(drafts).length&&$('draftstatus').textContent.startsWith('Unsaved')){e.preventDefault();e.returnValue=''}});
$('pairing').hidden=!!key;
refresh();if(key)refreshCatalogue().catch(e=>{$('pairing').hidden=false;notice(e.message,true)});
setInterval(refresh,700);
}
