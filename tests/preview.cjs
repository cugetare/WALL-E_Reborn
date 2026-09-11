// Local-only visual fixture; never represents hardware-in-loop validation.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path');
const root=path.join(__dirname,'../main/web');
http.createServer((req,res)=>{
 if(req.url==='/api/status'){
  res.setHeader('Content-Type','application/json');
  return res.end(JSON.stringify({stopped:true,service:true,epoch:1,batteryOk:false,busVerified:false,ready:false,mediaOnline:false,assignedMask:0,calibratedMask:0,scanBusy:false,ip:'192.168.4.6',ssid:'Local preview',wifiMode:'station',version:'2.0.0',uptimeMs:10000,heap:14000000,httpStackFree:8000,joints:Array.from({length:9},()=>({id:0,min:100,rest:512,max:900,inverted:false,present:false}))}));
 }
 if(req.url==='/api/catalog'){res.setHeader('Content-Type','application/json');return res.end('{"profiles":[],"automations":[]}');}
 const file={'/':'index.html','/app.js':'app.js','/style.css':'style.css'}[req.url];
 if(!file){res.writeHead(404);return res.end();}
 res.setHeader('Content-Type',file.endsWith('.html')?'text/html; charset=utf-8':file.endsWith('.js')?'text/javascript':'text/css');
 res.end(fs.readFileSync(path.join(root,file)));
}).listen(8765,'127.0.0.1',()=>console.log('Visual fixture http://127.0.0.1:8765'));
