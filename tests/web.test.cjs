const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.join(__dirname,'..');
const source=fs.readFileSync(path.join(root,'main/web/app.js'),'utf8');
const html=fs.readFileSync(path.join(root,'main/web/index.html'),'utf8');
const css=fs.readFileSync(path.join(root,'main/web/style.css'),'utf8');
const model=require('../main/web/app.js');
test('entire browser asset parses independently from C',()=>assert.doesNotThrow(()=>new vm.Script(source)));
test('calibration drafts survive repeated status updates',()=>{
 const saved={min:0,rest:512,max:1023,inverted:false},draft={minimum:100,rest:500,maximum:900,reverse:true};
 for(let i=0;i<100;i++)assert.equal(model.nextDraft(saved,draft),draft);
 assert.deepEqual(model.nextDraft(saved),{minimum:0,rest:512,maximum:1023,reverse:false});
});
test('calibration accepts only ordered finite integer limits',()=>{
 assert.ok(model.validLimits(0,512,1023));
 for(const limits of [[0,0,1023],[512,100,900],[-1,10,100],[0,512,1024],[0,NaN,1023],[0,1.5,10]])assert.equal(model.validLimits(...limits),false);
});
test('Stadia standard chords use L3/R3 and trigger mapping',()=>{
 assert.equal(model.chordMask([10,11]),0xC00);
 assert.equal(model.chordMask([]),0);
 assert.equal(model.chordMask([0,15]),32769);
});
test('media file browser accepts only controlled SD roots',()=>{
 assert.ok(model.validMediaPath('sounds/walle_hello.wav'));
 assert.ok(model.validMediaPath('firmware/motion/walle_reborn_2.1.0.bin'));
 assert.equal(model.validMediaPath('../secret'),false);
 assert.equal(model.validMediaPath('/sounds/walle.wav'),false);
 assert.equal(model.validMediaPath('sounds/bad name.wav'),false);
});
test('STOP and OTA take precedence over armed mode text',()=>{
 assert.equal(model.safeMode({ota:true,stopped:true,service:true}),'Updating');
 assert.equal(model.safeMode({stopped:true,service:false}),'Stopped');
 assert.equal(model.safeMode({stopped:false,service:true}),'Service armed');
});
test('all five tabs have a matching labelled panel',()=>{
 const controls=[...html.matchAll(/role="tab" id="([^"]+)" aria-controls="([^"]+)"/g)];
 assert.equal(controls.length,5);
 for(const [,id,panel]of controls)assert.ok(html.includes('id="'+panel+'" aria-labelledby="'+id+'"'));
});
test('HTML is UTF-8, script is external, no inline handlers or duplicate IDs',()=>{
 assert.match(html,/charset="utf-8"/);
 assert.equal(/\son\w+=/.test(html),false);
 const ids=[...html.matchAll(/\sid="([^"]+)"/g)].map(m=>m[1]);
 assert.equal(new Set(ids).size,ids.length);
 for(const [,id]of source.matchAll(/\$\('([^']+)'\)/g))assert.ok(ids.includes(id),'Missing UI element '+id);
});
test('STOP is sticky and layout reflows on phones',()=>{
 assert.match(css,/header\{position:sticky;top:0/);
 assert.match(css,/@media\(max-width:720px\)/);
 assert.match(html,/id="stop" class="danger">STOP ALL/);
});
test('sweep completion depends on feedback and supports cancellation',()=>{
 assert.match(source,/await testPosition\(point,abort.signal,joint\)/);
 assert.match(source,/Math.abs\(x.position-position\)<=3/);
 assert.match(source,/if\(sweepAbort\)sweepAbort.abort\(\)/);
 assert.doesNotMatch(source,/setTimeout\([^;]*testPoint/);
});
test('capture reads telemetry rather than the requested target',()=>{
 assert.match(source,/await feedback\(\);\$\(b.dataset.capture\).value=x.position/);
});

class Element {
 constructor(id=''){this.id=id;this.value='';this.checked=false;this.disabled=false;this.hidden=false;this.textContent='';this.children=[];this.listeners={};this.dataset={};this.attributes={};this.classList={toggle(){}};this.files=[]}
 addEventListener(name,fn){(this.listeners[name]??=[]).push(fn)}
 async emit(name){for(const fn of this.listeners[name]||[])await fn({target:this,preventDefault(){},key:'Enter'})}
 append(...v){this.children.push(...v)}
 appendChild(v){this.children.push(v)}
 replaceChildren(...v){this.children=v}
 add(v){this.children.push(v)}
 setAttribute(k,v){this.attributes[k]=v}
 getAttribute(k){return this.attributes[k]}
 focus(){}
 click(){return this.emit('click')}
}
function browserFixture(fault) {
 const elements={};for(const [,id]of html.matchAll(/\sid="([^"]+)"/g))elements[id]=new Element(id);
 for(const id of ['caljoint','assignjoint','throttle','steering'])elements[id].value='0';
 elements.targetid.value='2';
 const requests=[],intervals=[];
 const status={stopped:true,service:true,ota:false,ready:false,assignedMask:0,calibratedMask:0,batteryOk:false,
 mediaOnline:false,wifiMode:'station',ssid:'Home',ip:'192.168.4.6',version:'2.0.0',uptimeMs:1000,heap:200000,httpStackFree:6000,
 joints:Array.from({length:9},()=>({id:0,min:0,rest:512,max:1023,inverted:false,present:false}))};
 const document={getElementById:id=>elements[id],querySelectorAll:()=>[],createElement:()=>new Element(),createTextNode:t=>t,addEventListener(){},hidden:false};
 const fetch=async(url,options)=>{
   requests.push({url,options});
   if(fault){const response=fault(url,options);if(response)return response}
   const result=url==='/api/status'?status:url==='/api/catalog'?{profiles:[],automations:[]}:url==='/api/networks'?{busy:false,result:0,networks:[{ssid:'Home <&>',rssi:-42}]}:'Saved';
   return {ok:true,status:200,text:async()=>typeof result==='string'?result:JSON.stringify(result),headers:{get:()=>typeof result==='string'?'text/plain':'application/json'}};
 };
 const context=vm.createContext({document,window:{addEventListener(){}},navigator:{},sessionStorage:{getItem:()=>'',setItem(){}},
 crypto:{getRandomValues:a=>a.fill(1)},Uint8Array,fetch,AbortController,setTimeout,clearTimeout,
 setInterval:(fn,ms)=>intervals.push({fn,ms}),confirm:()=>true,Option:function(label,value){this.label=label;this.value=value},
 console,URL,Blob,XMLHttpRequest:function(){}});
 vm.runInContext(source,context);
 return {elements,requests,intervals,status};
}
const flush=()=>new Promise(r=>setImmediate(r));
test('whole frontend boots with missing hardware and keeps commissioning usable',async()=>{
 const b=browserFixture();await flush();
 assert.equal(b.elements.connection.textContent,'Online');
 assert.equal(b.elements.axes.children.length,9);
 assert.match(b.elements.battery.textContent,/absent/);
 assert.equal(b.elements.minimum.value,0);
});
test('Wi-Fi save posts exact credentials and reports result',async()=>{
 const b=browserFixture();await flush();
 b.elements.ssid.value='Home <&>';b.elements.password.value='passw0rd';
 await b.elements.savewifi.click();
 const request=b.requests.find(r=>r.url==='/api/wifi');
 assert.deepEqual(JSON.parse(request.options.body),{ssid:'Home <&>',password:'passw0rd'});
 assert.equal(b.elements.password.value,'');
 assert.equal(b.elements.wifiresult.textContent,'Saved');
});
test('HTTP error does not clear a Wi-Fi password or claim success',async()=>{
 const b=browserFixture(url=>url==='/api/wifi'?{ok:false,status:500,text:async()=>'Persistence failed'}:null);await flush();
 b.elements.ssid.value='Home';b.elements.password.value='passw0rd';
 await b.elements.savewifi.click();
 assert.equal(b.elements.password.value,'passw0rd');
 assert.match(b.elements.notice.textContent,/Persistence failed/);
});
test('network failure during control leads to explicit offline state',async()=>{
 const b=browserFixture(url=>{if(url==='/api/status')throw Error('offline')});await flush();
 assert.equal(b.elements.connection.textContent,'Offline');
 assert.match(b.elements.notice.textContent,/Connection lost/);
});
test('STOP bypasses normal command ordering and clears manual controls',async()=>{
 const b=browserFixture();await flush();b.elements.throttle.value=100;
 await b.elements.stop.click();await flush();
 assert.equal(b.elements.throttle.value,0);
 assert.ok(b.requests.some(r=>r.url==='/api/command'&&JSON.parse(r.options.body).command==='stop'));
});
