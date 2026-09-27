# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\board\web\index.html'
s = open(p, encoding='utf-8').read()

# 1) 删除 nav 里的 live 按钮
old_nav = """
      <button class="nav" data-page="live">实时画面<small>Live View</small></button>"""
assert s.count(old_nav) == 1, "nav-live"
s = s.replace(old_nav, "")

# 2) 删除独立的 live section（整块）
old_live_sec = """
      <section id="live" class="page">
        <div class="hero"><div><div class="eyebrow">Live view</div><h2>实时画面</h2><p>相机 RTSP 转 MJPEG 实时预览，雷达点云俯视热力图（XT-M60，前向 0–2 m / 横向 ±1 m）。</p></div><div class="signal"><div><b id="camStat">--</b><span>CAMERA</span></div><div><b id="radarStat">--</b><span>LIDAR GRID</span></div></div></div>
        <div class="live-grid">
          <div class="live-card"><div class="panel-head"><h3>相机画面</h3><span class="badge">LIVE</span></div><img id="camLive" alt="camera preview"></div>
          <div class="live-card"><div class="panel-head"><h3>雷达点云热力图</h3><span class="badge" id="radarBadge">--</span></div><canvas id="radarCanvas"></canvas><div class="legend"><span>俯视图 · 前向 x 0–2 m</span><span>横向 y ±1 m</span><span>亮度 ∝ 回波强度</span></div></div>
        </div>
      </section>"""
assert s.count(old_live_sec) == 1, "live-sec"
s = s.replace(old_live_sec, "")

# 3) 把 live-grid 嵌入 overview 页（IMU 行结束后）
old_imu = """<span>IMU</span><b>关闭（兼容保留）</b><em></em></div></div></div></div>
      </section>"""
new_imu = """<span>IMU</span><b>关闭（兼容保留）</b><em></em></div></div></div></div>
        <div class="live-grid">
          <div class="live-card"><div class="panel-head"><h3>相机画面</h3><span class="badge">LIVE</span></div><img id="camLive" alt="camera preview"></div>
          <div class="live-card"><div class="panel-head"><h3>雷达点云热力图</h3><span class="badge" id="radarBadge">--</span></div><canvas id="radarCanvas"></canvas><div class="legend"><span>俯视图 · 前向 x 0–2 m</span><span>横向 y ±1 m</span><span>亮度 ∝ 回波强度</span></div></div>
        </div>
      </section>"""
assert s.count(old_imu) == 1, "imu-anchor"
s = s.replace(old_imu, new_imu)

# 4) onclick: overview 页也启动画面; liveOff 同时停相机流
old_click = """function liveOn(){const img=$('camLive');if(img&&!img.src)img.src='http://'+location.hostname+':8081/camera';if(!radarTimer){radarTick();radarTimer=setInterval(radarTick,600)}}function liveOff(){if(radarTimer){clearInterval(radarTimer);radarTimer=null}}
document.querySelectorAll('.nav').forEach(n=>n.onclick=()=>{document.querySelectorAll('.nav,.page').forEach(x=>x.classList.remove('active'));n.classList.add('active');$(n.dataset.page).classList.add('active');if(n.dataset.page==='debug'){liveOff();loadLogs()}else if(n.dataset.page==='live'){liveOn()}else{liveOff()}});"""
new_click = """function liveOn(){const img=$('camLive');if(img&&!img.src)img.src='http://'+location.hostname+':8081/camera';if(!radarTimer){radarTick();radarTimer=setInterval(radarTick,600)}}
function liveOff(){if(radarTimer){clearInterval(radarTimer);radarTimer=null}const img=$('camLive');if(img)img.src=''}
document.querySelectorAll('.nav').forEach(n=>n.onclick=()=>{document.querySelectorAll('.nav,.page').forEach(x=>x.classList.remove('active'));n.classList.add('active');$(n.dataset.page).classList.add('active');if(n.dataset.page==='debug'){liveOff();loadLogs()}else if(n.dataset.page==='overview'){liveOn()}else{liveOff()}});liveOn();"""
assert s.count(old_click) == 1, "click2"
s = s.replace(old_click, new_click)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("INDEX2_OK")
