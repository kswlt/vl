# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\board\web\index.html'
s = open(p, encoding='utf-8').read()

# 1) 去掉 onclick 段末尾的 liveOn() 调用
old1 = """}else{liveOff()}});liveOn();"""
new1 = """}else{liveOff()}});"""
assert s.count(old1) == 1, "call-site"
s = s.replace(old1, new1)

# 2) 删除中间的 let radarTimer 声明（避免 TDZ）
old2 = """let radarTimer=null;
function renderRadar(buf)"""
new2 = """function renderRadar(buf)"""
assert s.count(old2) == 1, "tdz-let"
s = s.replace(old2, new2)

# 3) 在 script 末尾（所有函数定义后）声明 + 启动画面
old3 = """(function(){const img=$('camLive');if(img){img.onload=()=>{$('camStat').textContent='在线'};img.onerror=()=>{$('camStat').textContent='离线'}}})();
</script>"""
new3 = """(function(){const img=$('camLive');if(img){img.onload=()=>{$('camStat').textContent='在线'};img.onerror=()=>{$('camStat').textContent='离线'}}})();
let radarTimer=null;
liveOn();
</script>"""
assert s.count(old3) == 1, "end-block"
s = s.replace(old3, new3)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("TDZ_FIX_OK")
