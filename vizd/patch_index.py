# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\board\web\index.html'
s = open(p, encoding='utf-8').read()

# 1) CSS: 在 </style> 前插入 live 样式
old_css = """    @media(max-width:650px){.shell{padding:16px}"""
new_css = """    .live-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}
    .live-card{background:var(--card);border-radius:var(--radius);padding:16px;box-shadow:var(--shadow);min-width:0}
    .live-card h3{font-size:14px;margin:0 0 12px;font-weight:600}
    #camLive{width:100%;border-radius:10px;background:#000;display:block;aspect-ratio:16/9;object-fit:contain}
    #radarCanvas{width:100%;border-radius:10px;background:#fff;border:1px solid var(--line);display:block;aspect-ratio:2/1}
    @media(max-width:1000px){.live-grid{grid-template-columns:1fr}}
    @media(max-width:650px){.shell{padding:16px}"""
assert s.count(old_css) == 1, "css anchor"
s = s.replace(old_css, new_css)

# 2) nav 按钮
old_nav = """      <button class="nav" data-page="debug">调试信息<small>Diagnostics</small></button>"""
new_nav = """      <button class="nav" data-page="debug">调试信息<small>Diagnostics</small></button>
      <button class="nav" data-page="live">实时画面<small>Live View</small></button>"""
assert s.count(old_nav) == 1, "nav anchor"
s = s.replace(old_nav, new_nav)

# 3) live section: 在 debug section 结束后、</main> 前
old_sec = """      </section>
    </main>"""
new_sec = """      </section>
      <section id="live" class="page">
        <div class="hero"><div><div class="eyebrow">Live view</div><h2>实时画面</h2><p>相机 RTSP 转 MJPEG 实时预览，雷达点云俯视热力图（XT-M60，前向 0–2 m / 横向 ±1 m）。</p></div><div class="signal"><div><b id="camStat">--</b><span>CAMERA</span></div><div><b id="radarStat">--</b><span>LIDAR GRID</span></div></div></div>
        <div class="live-grid">
          <div class="live-card"><div class="panel-head"><h3>相机画面</h3><span class="badge">LIVE</span></div><img id="camLive" alt="camera preview"></div>
          <div class="live-card"><div class="panel-head"><h3>雷达点云热力图</h3><span class="badge" id="radarBadge">--</span></div><canvas id="radarCanvas"></canvas><div class="legend"><span>俯视图 · 前向 x 0–2 m</span><span>横向 y ±1 m</span><span>亮度 ∝ 回波强度</span></div></div>
        </div>
      </section>
    </main>"""
assert s.count(old_sec) == 1, "sec anchor"
s = s.replace(old_sec, new_sec)

# 4) nav onclick: 处理 live 的雷达轮询启停
old_click = """document.querySelectorAll('.nav').forEach(n=>n.onclick=()=>{document.querySelectorAll('.nav,.page').forEach(x=>x.classList.remove('active'));n.classList.add('active');$(n.dataset.page).classList.add('active');if(n.dataset.page==='debug')loadLogs()});"""
new_click = """function liveOn(){const img=$('camLive');if(img&&!img.src)img.src='http://'+location.hostname+':8081/camera';if(!radarTimer){radarTick();radarTimer=setInterval(radarTick,600)}}function liveOff(){if(radarTimer){clearInterval(radarTimer);radarTimer=null}}
document.querySelectorAll('.nav').forEach(n=>n.onclick=()=>{document.querySelectorAll('.nav,.page').forEach(x=>x.classList.remove('active'));n.classList.add('active');$(n.dataset.page).classList.add('active');if(n.dataset.page==='debug'){liveOff();loadLogs()}else if(n.dataset.page==='live'){liveOn()}else{liveOff()}});"""
assert s.count(old_click) == 1, "click anchor"
s = s.replace(old_click, new_click)

# 5) 雷达渲染 JS: 插在文件末尾 </script> 前
old_end = """</script>
</body>"""
new_end = """let radarTimer=null;
function renderRadar(buf){if(buf.length<26)return;const w=buf[4],h=buf[5];if(!w||!h)return;const c=$('radarCanvas');if(!c)return;const dpr=devicePixelRatio||1,cw=c.clientWidth||480,ch=c.clientHeight||240;c.width=cw*dpr;c.height=ch*dpr;const x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.fillStyle='#fff';x.fillRect(0,0,cw,ch);x.strokeStyle='#ececf0';x.lineWidth=1;for(let i=0;i<=4;i++){x.beginPath();x.moveTo(cw*i/4,0);x.lineTo(cw*i/4,ch);x.stroke();x.beginPath();x.moveTo(0,ch*i/4);x.lineTo(cw,ch*i/4);x.stroke()}let mx=1;for(let i=26;i<buf.length;i++)if(buf[i]>mx)mx=buf[i];const cellW=cw/w,cellH=ch/h;for(let gy=0;gy<h;gy++){for(let gx=0;gx<w;gx++){const v=buf[26+gy*w+gx];if(!v)continue;const t=Math.pow(v/mx,0.6);x.fillStyle='rgba(0,113,227,'+(0.10+0.90*t).toFixed(3)+')';x.fillRect(gx*cellW,gy*cellH,cellW+0.5,cellH+0.5)}}const xmin=new Float32Array(buf.buffer,12,1)[0],xmax=new Float32Array(buf.buffer,16,1)[0],ymin=new Float32Array(buf.buffer,20,1)[0],ymax=new Float32Array(buf.buffer,24,1)[0];x.fillStyle='#86868b';x.font='11px -apple-system,Segoe UI,sans-serif';x.fillText(xmin.toFixed(1)+'m',6,14);x.fillText(xmax.toFixed(1)+'m',cw-30,14);x.fillText(ymin.toFixed(1)+'m',6,ch-6);x.fillText(ymax.toFixed(1)+'m',cw-30,ch-6);x.fillText('前向 →',cw-52,ch-8);$('radarBadge').textContent='seq '+buf[6];$('radarStat').textContent=w+'×'+h}
async function radarTick(){try{const r=await fetch('http://'+location.hostname+':8081/radar',{cache:'no-store'});if(!r.ok)throw 0;const b=new Uint8Array(await r.arrayBuffer());renderRadar(b);$('camStat').textContent='--'}catch(e){const c=$('radarCanvas');if(c){const x=c.getContext('2d');x.clearRect(0,0,c.width,c.height)}if($('radarBadge'))$('radarBadge').textContent='离线'}}
(function(){const img=$('camLive');if(img){img.onload=()=>{$('camStat').textContent='在线'};img.onerror=()=>{$('camStat').textContent='离线'}}})();
</script>
</body>"""
assert s.count(old_end) == 1, "end anchor"
s = s.replace(old_end, new_end)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("INDEX_PATCH_OK")
