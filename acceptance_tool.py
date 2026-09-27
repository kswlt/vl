"""Independent Zenoh/protobuf acceptance receiver for the space camera."""
from __future__ import annotations
import argparse, bisect, csv, io, json, logging, os, queue, shutil, signal, subprocess, threading, time
from pathlib import Path
try:
    import tkinter as tk
    from tkinter import ttk
except ImportError:  # headless Linux still supports evidence generation
    tk = None
    ttk = None
from PIL import Image, ImageTk
import zenoh
from pc import active_msgs_pb2 as pb

ROOT = Path(__file__).resolve().parent

def find_binary(name, explicit=None):
    candidates = [Path(explicit)] if explicit else []
    candidates += [Path(shutil.which(name))] if shutil.which(name) else []
    if os.name == "nt":
        candidates += [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Wireshark" / f"{name}.exe"]
    else:
        candidates += [Path("/mnt/c/Program Files/Wireshark") / f"{name}.exe"]
    return next((str(p) for p in candidates if p.is_file()), None)

def zconfig(endpoint):
    c = zenoh.Config(); c.insert_json5("mode", '"client"')
    c.insert_json5("connect/endpoints", json.dumps([endpoint])); c.insert_json5("connect/timeout_ms", "0")
    c.insert_json5("connect/exit_on_failure", "false"); c.insert_json5("scouting/multicast/enabled", "false")
    return c

def endpoint_port(endpoint):
    try: return int(endpoint.rsplit(":", 1)[1])
    except (ValueError, IndexError): return None

def endpoint_host(endpoint):
    try:
        host = endpoint.split("://", 1)[-1].rsplit(":", 1)[0]
        return host.strip("[]")
    except IndexError:
        return None

def pct(values, p):
    if not values: return 0.0
    a = sorted(values); return a[min(len(a)-1, round((len(a)-1)*p/100))]

class Stats:
    def __init__(self, kind):
        self.kind=kind; self.lock=threading.Lock(); self.frames=0; self.pb_errors=0; self.decode_errors=0
        self.seq_gaps=0; self.seq_duplicates=0; self.seq_rollbacks=0; self.timestamp_rollbacks=0; self.timestamp_unit_errors=0
        self.last_seq=None; self.last_stamp=None; self.last_rx=None; self.intervals=[]; self.last={}; self.point_counts=[]; self.stamp_history=[]
    def observe(self, seq, stamp, rx):
        previous_rx=self.last_rx
        if previous_rx is not None: self.intervals.append((rx-previous_rx)/1e6)
        self.last_rx=rx
        if self.last_seq is not None:
            d=(seq-self.last_seq)&0xffffffff
            if d==0: self.seq_duplicates += 1
            elif d < 0x80000000: self.seq_gaps += d-1
            else: self.seq_rollbacks += 1
        if stamp <= 0: self.timestamp_unit_errors += 1
        if self.last_stamp is not None:
            if stamp < self.last_stamp: self.timestamp_rollbacks += 1
            stamp_delta=stamp-self.last_stamp; rx_delta_us=max(1,(rx-previous_rx)//1000)
            # A millisecond or nanosecond producer is off by roughly 1000x.
            if stamp_delta > 0 and (stamp_delta < max(1000,rx_delta_us//20) or stamp_delta > rx_delta_us*20):
                self.timestamp_unit_errors += 1
        self.last_seq, self.last_stamp=seq, stamp; self.frames += 1
        self.stamp_history.append((stamp, rx))
    def hz(self):
        return (len(self.intervals)*1000/sum(self.intervals)) if self.intervals and sum(self.intervals) else 0.0
    def report(self):
        with self.lock:
            hz=self.hz(); r={"frames":self.frames,"average_hz":hz,"p50_ms":pct(self.intervals,50),"p95_ms":pct(self.intervals,95),"p99_ms":pct(self.intervals,99),"max_ms":max(self.intervals,default=0),"protobuf_errors":self.pb_errors,"seq_gaps":self.seq_gaps,"seq_duplicates":self.seq_duplicates,"seq_rollbacks":self.seq_rollbacks,"timestamp_rollbacks":self.timestamp_rollbacks,"timestamp_unit_errors":self.timestamp_unit_errors}
            if self.kind=="image":
                r.update(format=self.last.get("format",""),width=self.last.get("width",0),height=self.last.get("height",0),decode_errors=self.decode_errors)
                r["pass"]=bool(self.frames and 9<=hz<=11 and not any(r[k] for k in ("protobuf_errors","decode_errors","seq_gaps","seq_duplicates","seq_rollbacks","timestamp_rollbacks","timestamp_unit_errors")))
            else:
                r.update(scaler=self.last.get("scaler",0),points_min=min(self.point_counts,default=0),points_max=max(self.point_counts,default=0),points_avg=sum(self.point_counts)/len(self.point_counts) if self.point_counts else 0)
                r["pass"]=bool(self.frames and 4<=hz<=6 and self.last.get("scaler")==1000 and not any(r[k] for k in ("protobuf_errors","seq_gaps","seq_duplicates","seq_rollbacks","timestamp_rollbacks","timestamp_unit_errors")))
            return r

class Capture:
    def __init__(self,path,ports,interface="1",binary=None): self.path=path; self.ports=[p for p in ports if p]; self.interface=interface; self.binary=binary; self.proc=None; self.status="unavailable"; self.error=None
    def start(self):
        exe=self.binary or find_binary("dumpcap") or find_binary("tshark")
        if not exe: self.error="dumpcap/tshark not found"; return
        cmd=[exe,"-i",self.interface,"-s","0","-w",str(self.path)]
        if self.ports: cmd += ["-f","("+" or ".join("tcp port %s"%p for p in self.ports)+")"]
        try:
            kwargs={"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {}
            self.proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**kwargs)
            time.sleep(1)
            if self.proc.poll() is not None: raise OSError("capture process exited with code %s" % self.proc.returncode)
            self.status="running"
        except OSError as e: self.error=str(e)
    def stop(self):
        if not self.proc: return
        try:
            self.proc.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
            self.proc.wait(12)
        except (OSError,subprocess.TimeoutExpired):
            try: self.proc.kill()
            except OSError: pass
        self.status="stopped" if self.path.exists() else "failed"

class H264:
    def __init__(self,samples): self.raw=samples/"camera.h264"; self.file=None; self.proc=None; self.frames=queue.Queue(2); self.error=None
    def start(self):
        exe=shutil.which("ffmpeg")
        if not exe: self.error="ffmpeg not found"; return
        self.file=self.raw.open("wb")
        try:
            self.proc=subprocess.Popen([exe,"-loglevel","error","-f","h264","-i","pipe:0","-f","mjpeg","-q:v","5","pipe:1"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            threading.Thread(target=self._read,daemon=True).start()
        except OSError as e: self.error=str(e)
    def _read(self):
        data=b""
        while self.proc and self.proc.stdout:
            chunk=self.proc.stdout.read(65536)
            if not chunk: break
            data+=chunk
            while b"\xff\xd8" in data and b"\xff\xd9" in data:
                a=data.find(b"\xff\xd8"); b=data.find(b"\xff\xd9",a)+2
                if b<2: break
                try: self.frames.put_nowait(data[a:b])
                except queue.Full: self.frames.get_nowait(); self.frames.put_nowait(data[a:b])
                data=data[b:]
    def feed(self,data):
        if self.file: self.file.write(data); self.file.flush()
        if self.proc and self.proc.stdin:
            try: self.proc.stdin.write(data); self.proc.stdin.flush()
            except (BrokenPipeError,OSError) as e: self.error=str(e)
    def close(self):
        if self.file: self.file.close()
        if self.proc:
            try: self.proc.stdin.close(); self.proc.wait(5)
            except (OSError,subprocess.TimeoutExpired): self.proc.kill()

class Receiver:
    def __init__(self,app,kind,endpoint):
        self.app=app; self.kind=kind; self.endpoint=endpoint; self.topic=f"active/{app.sn}/{kind}"; self.stats=Stats(kind); self.q=queue.Queue(8); self.stop=threading.Event(); self.thread=threading.Thread(target=self.run,daemon=True)
    def callback(self,sample):
        try: self.q.put_nowait((sample.payload.to_bytes(),time.time_ns()))
        except queue.Full: pass
    def run(self):
        while not self.stop.is_set():
            session=None
            try:
                session=zenoh.open(zconfig(self.endpoint)); sub=session.declare_subscriber(self.topic,self.callback); self.app.log.info("subscribed %s via %s",self.topic,self.endpoint)
                while not self.stop.is_set():
                    try: item=self.q.get(timeout=.2)
                    except queue.Empty: continue
                    self.process(*item)
                sub.undeclare()
            except Exception as e: self.app.log.warning("%s subscription: %s",self.kind,e); time.sleep(.5)
            finally:
                if session: session.close()
    def process(self,payload,rx):
        s=self.stats
        self.app.set_raw(self.kind, payload, rx)
        with s.lock:
            try:
                arr=(pb.ImageMsgArray.FromString(payload) if self.kind=="image" else pb.FileMsgArray.FromString(payload) if self.kind=="config_file" else pb.LidarPointMsgArray.FromString(payload))
                if self.kind=="config_file":
                    files=[{"path":f.path,"bytes":len(f.data),"stamp":f.header.stamp} for f in arr.array if f.path and f.data and f.HasField("header") and f.header.stamp > 0]
                    if not files: raise ValueError("config response has no non-empty files")
                    if "calibration.json" not in {f["path"] for f in files}: raise ValueError("placeholder calibration missing")
                    self.app.log.info("config_file received %d files",len(files)); return
                if len(arr.array)!=1: raise ValueError("array length %d"%len(arr.array))
                msg=arr.array[0]
                if not msg.HasField("header"): raise ValueError("missing header")
                interval=(rx-s.last_rx)/1e6 if s.last_rx is not None else 0
                if self.kind=="image":
                    names={2:"JPEG",3:"H264"}; name=names.get(msg.format,"UNKNOWN(%s)"%msg.format); ok=False
                    if msg.format==pb.ImageFormatJpeg:
                        if not (msg.data.startswith(b"\xff\xd8") and msg.data.endswith(b"\xff\xd9")): raise ValueError("invalid JPEG markers")
                        with Image.open(io.BytesIO(msg.data)) as im:
                            if im.size!=(msg.width,msg.height): raise ValueError("JPEG dimensions mismatch")
                        ok=True; self.app.save_jpeg(msg.data); self.app.set_image(msg.data)
                    elif msg.format==pb.ImageFormatH264:
                        if not (msg.data.startswith(b"\x00\x00\x01") or msg.data.startswith(b"\x00\x00\x00\x01")): raise ValueError("missing Annex-B start code")
                        self.app.decoder.feed(msg.data)
                        if self.app.decoder.proc is None: raise ValueError("ffmpeg unavailable for H264 decode")
                        ok=True
                        try: self.app.set_image(self.app.decoder.frames.get_nowait())
                        except queue.Empty: pass
                    else: raise ValueError("unsupported ImageMsg.format")
                    s.observe(msg.header.seq,msg.header.stamp,rx); s.last={"format":name,"width":msg.width,"height":msg.height}; self.app.image_csv.writerow([rx,msg.header.seq,msg.header.stamp,name,msg.width,msg.height,len(msg.data),ok,interval]); self.app.image_file.flush()
                else:
                    if msg.header.scaler!=1000: raise ValueError("scaler=%s, expected 1000"%msg.header.scaler)
                    points=[(p.x/msg.header.scaler,p.y/msg.header.scaler,p.z/msg.header.scaler) for p in msg.points]; s.observe(msg.header.seq,msg.header.stamp,rx); s.point_counts.append(len(points)); s.last={"scaler":msg.header.scaler}; self.app.set_points(points); self.app.point_csv.writerow([rx,msg.header.seq,msg.header.stamp,msg.header.scaler,len(points),interval,True]); self.app.point_file.flush()
            except Exception as e:
                s.pb_errors+=1
                if self.kind=="image": s.decode_errors+=1
                self.app.log.warning("invalid %s: %s",self.kind,e)
                if self.kind=="pointcloud": self.app.point_csv.writerow([rx,"","","","","",False])

class App:
    def __init__(self,args):
        cfg=json.loads(Path(args.config).read_text(encoding="utf-8")); self.cfg=cfg; self.sn=args.sn or cfg.get("sn") or "unknown"; self.started=time.time(); self.args=args
        self.out=ROOT/"evidence"/("acceptance-"+time.strftime("%Y%m%d-%H%M%S")); self.samples=self.out/"samples"; self.samples.mkdir(parents=True)
        self.log=logging.getLogger("acceptance"); self.log.setLevel(logging.INFO); self.log.addHandler(logging.FileHandler(self.out/"runtime.log",encoding="utf-8")); self.log.addHandler(logging.StreamHandler())
        self.image_file=(self.out/"image.csv").open("w",newline="",encoding="utf-8"); self.point_file=(self.out/"pointcloud.csv").open("w",newline="",encoding="utf-8"); self.image_csv=csv.writer(self.image_file); self.point_csv=csv.writer(self.point_file); self.image_csv.writerow(["receive_time","seq","stamp","format","width","height","data_bytes","decode_ok","interval_ms"]); self.point_csv.writerow(["receive_time","seq","stamp","scaler","points","interval_ms","parse_ok"])
        self.jpeg_count=0; self.latest_image=None; self.latest_points=[]; self.raw_info={}; self.cloud_view={"zoom":1.0,"yaw":0.0,"pitch":0.0,"pan_x":0.0,"pan_y":0.0,"drag":None}; self.decoder=H264(self.samples); self.decoder.start(); self.capture=Capture(self.out/"acceptance.pcapng",[endpoint_port(cfg.get("image_endpoint","")),endpoint_port(cfg.get("pointcloud_endpoint",""))],str(args.interface),args.dumpcap_path); self.capture.start(); self.started=time.time(); self.image=Receiver(self,"image",cfg["image_endpoint"]); self.cloud=Receiver(self,"pointcloud",cfg["pointcloud_endpoint"]); self.receivers=[self.image,self.cloud]
        if args.check_config: self.receivers.append(Receiver(self,"config_file",cfg["pointcloud_endpoint"]))
        for r in self.receivers: r.thread.start()
        self.command_results={"config_file":{"requested":False,"protobuf_ok":False,"files_received":[],"result":"NOT_RUN"},"setting":{"request_sent":False,"response_received":False,"error_code":None,"result":"NOT_RUN"},"reboot":{"status":"NOT_RUN","reason":"destructive/requires explicit confirmation"}}
        if args.check_config or args.test_setting or args.test_reboot:
            threading.Thread(target=self.run_commands,daemon=True).start()
        self.gui=None
        try:
            if tk is None: raise RuntimeError("tkinter unavailable")
            self.gui=tk.Tk(); self.gui.title("空间相机协议验收 | "+self.sn); self._build(); self.gui.after(100,self.refresh)
        except (tk.TclError if tk else RuntimeError, RuntimeError): self.log.warning("Tk GUI unavailable; running headless")
    def _build(self):
        ttk.Label(self.gui,text="Device: "+self.sn).pack(); self.status=ttk.Label(self.gui,text="Starting..."); self.status.pack(); f=ttk.Frame(self.gui); f.pack(fill="both",expand=True); self.image_label=ttk.Label(f); self.image_label.grid(row=0,column=0,sticky="nsew"); self.canvas=tk.Canvas(f,width=640,height=480,bg="black"); self.canvas.grid(row=0,column=1,sticky="nsew"); self.raw= tk.Text(self.gui,height=8,width=120,state="disabled",font=("Consolas",9)); self.raw.pack(fill="x"); f.columnconfigure((0,1),weight=1); f.rowconfigure(0,weight=1); self.canvas.bind("<ButtonPress-1>",self.cloud_press); self.canvas.bind("<B1-Motion>",self.cloud_drag); self.canvas.bind("<ButtonRelease-1>",lambda e:self.cloud_view.update(drag=None)); self.canvas.bind("<MouseWheel>",self.cloud_zoom); self.canvas.bind("<Button-4>",lambda e:self.cloud_zoom(e,1)); self.canvas.bind("<Button-5>",lambda e:self.cloud_zoom(e,-1))
    def save_jpeg(self,data):
        if self.jpeg_count<3: self.jpeg_count+=1; (self.samples/("image-%03d.jpg"%self.jpeg_count)).write_bytes(data)
    def set_image(self,data): self.latest_image=data
    def set_points(self,points): self.latest_points=points
    def set_raw(self,kind,payload,rx): self.raw_info[kind]=(len(payload),payload[:32].hex(" "),rx)
    def run_commands(self):
        endpoint=self.cfg["pointcloud_endpoint"]
        try:
            with zenoh.open(zconfig(endpoint)) as session:
                if self.args.check_config:
                    self.command_results["config_file"]["requested"]=True
                    files=[]
                    for reply in session.get(self.image.topic.rsplit("/",1)[0]+"/config_file",timeout=5):
                        if reply.ok:
                            arr=pb.FileMsgArray.FromString(reply.ok.payload.to_bytes())
                            for f in arr.array:
                                if f.path and f.data: files.append({"path":f.path,"bytes":len(f.data)})
                    self.command_results["config_file"].update(protobuf_ok=bool(files),files_received=files,result="PASS" if files else "FAIL")
                if self.args.test_setting:
                    setting_topic=self.image.topic.rsplit("/",1)[0]+"/cmd/setting"
                    def send_setting(fmt, seq):
                        request=pb.SettingRequest(header=pb.Header(seq=seq))
                        if fmt: request.image_format=fmt
                        replies=[]
                        for reply in session.get(setting_topic,payload=request.SerializeToString(),timeout=5):
                            if reply.ok: replies.append(pb.SettingResponse.FromString(reply.ok.payload.to_bytes()))
                        if not replies: return None
                        msg=replies[0]
                        return {"requested_image_format":fmt,"response_image_format":msg.image_format,"error_code":msg.error.code if msg.HasField("error") else 0,"stamp":msg.header.stamp,"stamp_microseconds":msg.header.stamp > 0}
                    self.command_results["setting"]["request_sent"]=True
                    cases=[]
                    for fmt in (0,1,0,2,0):
                        case=send_setting(fmt,len(cases)+1); cases.append(case)
                        if fmt in (1,2): time.sleep(2)
                    self.command_results["setting"]["cases"]=cases
                    self.command_results["setting"]["response_received"]=all(cases)
                    self.command_results["setting"]["result"]="PASS" if all(c and c["error_code"]==0 and c["stamp_microseconds"] for c in cases) and cases[2]["response_image_format"]==1 and cases[3]["response_image_format"]==2 and cases[4]["response_image_format"]==2 else "FAIL"
                if self.args.test_reboot:
                    self.command_results["reboot"].update(status="RUN",reason="explicit --test-reboot")
                    req=pb.RebootRequest(header=pb.Header(seq=2)).SerializeToString()
                    for reply in session.get(self.image.topic.rsplit("/",1)[0]+"/cmd/reboot",payload=req,timeout=5):
                        if reply.ok:
                            msg=pb.RebootResponse.FromString(reply.ok.payload.to_bytes()); self.command_results["reboot"].update(response_received=True,error_code=msg.error.code if msg.HasField("error") else 0,status="PASS",stamp=msg.header.stamp,stamp_microseconds=msg.header.stamp > 0)
                (self.out/"command-results.json").write_text(json.dumps(self.command_results,indent=2,ensure_ascii=False),encoding="utf-8")
        except Exception as e:
            if self.args.check_config: self.command_results["config_file"]["error"]=str(e)
            if self.args.test_setting: self.command_results["setting"]["error"]=str(e)
            (self.out/"command-results.json").write_text(json.dumps(self.command_results,indent=2,ensure_ascii=False),encoding="utf-8")
    def cloud_press(self,event): self.cloud_view["drag"]=(event.x,event.y,self.cloud_view["yaw"],self.cloud_view["pitch"],self.cloud_view["pan_x"],self.cloud_view["pan_y"])
    def cloud_drag(self,event):
        d=self.cloud_view.get("drag");
        if d: self.cloud_view.update(yaw=d[2]+(event.x-d[0])*.01,pitch=max(-1.4,min(1.4,d[3]+(event.y-d[1])*.01)),pan_x=d[4]+(event.x-d[0]),pan_y=d[5]+(event.y-d[1]))
    def cloud_zoom(self,event,step=None):
        delta=step if step is not None else (1 if event.delta>0 else -1); self.cloud_view["zoom"]=max(.1,min(20,self.cloud_view["zoom"]*(1.15 if delta>0 else 1/1.15)))
    def refresh(self):
        if self.latest_image:
            try: im=Image.open(io.BytesIO(self.latest_image)); im.thumbnail((800,600)); self.image_label.image=ImageTk.PhotoImage(im); self.image_label.configure(image=self.image_label.image)
            except Exception: pass
        c=self.canvas; c.delete("all"); w=max(c.winfo_width(),2); h=max(c.winfo_height(),2)
        import math
        v=self.cloud_view
        for x,y,z in self.latest_points:
            xx=x*math.cos(v["yaw"])-z*math.sin(v["yaw"]); zz=x*math.sin(v["yaw"])+z*math.cos(v["yaw"]); yy=y*math.cos(v["pitch"])-zz*math.sin(v["pitch"]); zz=y*math.sin(v["pitch"])+zz*math.cos(v["pitch"]); d=max(1,zz+12); px=w/2+v["pan_x"]+xx*28*v["zoom"]/d; py=h/2+v["pan_y"]-yy*28*v["zoom"]/d
            if 0<=px<w and 0<=py<h: c.create_rectangle(px,py,px+1,py+1,fill="#67d4ff",outline="")
        raw=" | ".join("%s raw=%dB [%s]"%(k,v[0],v[1]) for k,v in self.raw_info.items()); self.raw.configure(state="normal"); self.raw.delete("1.0", "end"); self.raw.insert("end",raw+"\nDecoded IMAGE: "+str(self.image.stats.last)+"\nDecoded POINTCLOUD: scaler=%s points=%d zoom=%.2fx yaw=%.2f pitch=%.2f"%(self.cloud.stats.last.get("scaler"),len(self.latest_points),v["zoom"],v["yaw"],v["pitch"])); self.raw.configure(state="disabled"); self.status.configure(text="IMAGE %.2f Hz / %d | POINTCLOUD %.2f Hz / %d points | CAPTURE %s"%(self.image.stats.hz(),self.image.stats.frames,self.cloud.stats.hz(),len(self.latest_points),self.capture.status))
        if time.time()-self.started<self.args.seconds: self.gui.after(100,self.refresh)
    def finish(self):
        for r in self.receivers: r.stop.set()
        for r in self.receivers: r.thread.join(3)
        self.capture.stop(); self.decoder.close(); self.image_file.close(); self.point_file.close(); self.save_pointcloud_image(); ir=self.image.stats.report(); pr=self.cloud.stats.report(); network=self.network(); end=time.time()
        required=[ir["pass"],pr["pass"]]
        if self.args.check_config: required.append(self.command_results["config_file"]["result"]=="PASS")
        if self.args.test_setting: required.append(self.command_results["setting"]["result"]=="PASS")
        if self.args.test_reboot: required.append(self.command_results["reboot"]["status"]=="PASS")
        image_stamps=sorted(stamp for stamp,_ in self.image.stats.stamp_history); cloud_stamps=sorted(stamp for stamp,_ in self.cloud.stats.stamp_history)
        nearest=[]
        if cloud_stamps:
            for stamp in image_stamps:
                i=bisect.bisect_left(cloud_stamps,stamp)
                candidates=[]
                if i: candidates.append(abs(stamp-cloud_stamps[i-1]))
                if i<len(cloud_stamps): candidates.append(abs(stamp-cloud_stamps[i]))
                nearest.append(min(candidates))
        cross={"count":len(nearest),"mean_us":sum(nearest)/len(nearest) if nearest else 0,"p50_us":pct(nearest,50),"p95_us":pct(nearest,95),"p99_us":pct(nearest,99),"max_us":max(nearest,default=0)}
        s={"test":{"start":time.strftime("%Y-%m-%dT%H:%M:%S%z",time.localtime(self.started)),"end":time.strftime("%Y-%m-%dT%H:%M:%S%z"),"duration_seconds":end-self.started,"device_sn":self.sn,"image_topic":self.image.topic,"pointcloud_topic":self.cloud.topic},"image":ir,"pointcloud":pr,"nearest_timestamp_difference_us":cross,"config_file":self.command_results["config_file"],"setting":self.command_results["setting"],"reboot":self.command_results["reboot"],"imu":"NOT_APPLICABLE","network":{"pcap":str(self.out/"acceptance.pcapng"),"capture_status":self.capture.status,"capture_error":self.capture.error,**network},"result":"PASS" if all(required) else "FAIL"}; (self.out/"summary.json").write_text(json.dumps(s,indent=2,ensure_ascii=False),encoding="utf-8"); self.html(s); return s
    def save_pointcloud_image(self):
        from PIL import ImageDraw
        im=Image.new("RGB",(1000,700),(8,12,18)); d=ImageDraw.Draw(im); w,h=im.size
        for x,y,z in self.latest_points:
            depth=max(1,z+10); px=w/2+x*25/depth; py=h/2-y*25/depth
            if 0<=px<w and 0<=py<h: d.point((int(px),int(py)),fill=(103,212,255))
        im.save(self.out/"pointcloud.png")
    def network(self):
        path=self.out/"network-summary.txt"; tshark=find_binary("tshark",self.args.tshark_path); pcap=self.out/"acceptance.pcapng"
        if not tshark or not (self.out/"acceptance.pcapng").exists(): path.write_text("network automated analysis unavailable\n",encoding="utf-8"); return {"retransmissions":0,"rst":0,"analysis":"unavailable"}
        p=str(pcap); base=[tshark,"-r",p]; runkw={"capture_output":True,"text":True,"encoding":"utf-8","errors":"replace"}; read=subprocess.run(base+['-q','-z','io,stat,0'],**runkw); conv=subprocess.run(base+['-q','-z','conv,tcp'],**runkw).stdout; counts={"packets":0,"bytes":pcap.stat().st_size}
        image_host=endpoint_host(self.cfg.get("image_endpoint","")); cloud_host=endpoint_host(self.cfg.get("pointcloud_endpoint",""))
        fields={'7447_packets':('ip.addr==%s && tcp.port==7447'%cloud_host) if cloud_host else 'tcp.port==7447','7448_packets':('ip.addr==%s && tcp.port==7448'%image_host) if image_host else 'tcp.port==7448','retransmissions':'tcp.analysis.retransmission','fast_retransmissions':'tcp.analysis.fast_retransmission','lost_segments':'tcp.analysis.lost_segment','rst':'tcp.flags.reset==1','zero_window':'tcp.analysis.zero_window','window_full':'tcp.analysis.window_full','duplicate_ack':'tcp.analysis.duplicate_ack'}
        for key,filt in fields.items(): counts[key]=subprocess.run(base+['-Y',filt,'-T','fields','-e','frame.number'],**runkw).stdout.count('\n')
        counts['packets']=subprocess.run(base+['-T','fields','-e','frame.number'],**runkw).stdout.count('\n'); proto=subprocess.run([tshark,'-G','protocols'],**runkw).stdout; counts['zenoh_dissector']='zenoh' in proto.lower(); counts['pcap_read_ok']=read.returncode==0 and 'cut short' not in (read.stderr or '').lower(); path.write_text(conv+'\n'+read.stdout+'\n'+json.dumps(counts,indent=2),encoding='utf-8'); counts['analysis']='available'; return counts
    def html(self,s):
        i,p,n=s["image"],s["pointcloud"],s["network"]; h="<meta charset='utf-8'><h1>空间相机通信协议验收</h1><p>设备SN: %s<br>时长: %.1fs</p><h2>Image JPEG: %s</h2><p>%s %sx%s %.2fHz，PB %s，decode %s</p><h2>Pointcloud: %s</h2><p>%.2fHz，points %s..%s，scaler %s</p><h2>Config File: %s</h2><p>files %s</p><h2>Setting: %s</h2><p>request %s response %s error %s</p><h2>Reboot: %s</h2><h2>Network Capture: %s</h2><p>interface %s；pcap %s (%s bytes)；packets %s；7447 %s；7448 %s；retransmission %s；RST %s；Zenoh dissector %s</p><h2>Overall: %s</h2>"%(self.sn,s["test"]["duration_seconds"],"PASS" if i["pass"] else "FAIL",i["format"],i["width"],i["height"],i["average_hz"],i["protobuf_errors"],i["decode_errors"],"PASS" if p["pass"] else "FAIL",p["average_hz"],p["points_min"],p["points_max"],p["scaler"],s["config_file"]["result"],s["config_file"].get("files_received"),s["setting"]["result"],s["setting"].get("request_sent"),s["setting"].get("response_received"),s["setting"].get("error_code"),s["reboot"]["status"],"PASS" if n.get("pcap_read_ok") else "FAIL",self.args.interface,n.get("pcap",self.out/"acceptance.pcapng"),Path(n.get("pcap",self.out/"acceptance.pcapng")).stat().st_size if Path(n.get("pcap",self.out/"acceptance.pcapng")).exists() else 0,n.get("packets",0),n.get("7447_packets",0),n.get("7448_packets",0),n.get("retransmissions",0),n.get("rst",0),"available" if n.get("zenoh_dissector") else "unavailable",s["result"]); (self.out/"report.html").write_text(h,encoding="utf-8")
    def run(self):
        try:
            if self.gui: self.gui.after(int(self.args.seconds*1000),self.gui.destroy); self.gui.mainloop()
            else: time.sleep(self.args.seconds)
        finally: print(json.dumps(self.finish(),ensure_ascii=False,indent=2)); print("Evidence:",self.out)

def main():
    a=argparse.ArgumentParser(); a.add_argument("--seconds",type=float,default=60); a.add_argument("--config",default=str(ROOT/"config/pc.json")); a.add_argument("--sn"); a.add_argument("--interface",default="1",help="Wireshark capture interface id"); a.add_argument("--dumpcap-path"); a.add_argument("--tshark-path"); a.add_argument("--check-config",action="store_true"); a.add_argument("--test-setting",action="store_true",help="send an empty setting request; opt-in because this uses the command channel"); a.add_argument("--test-reboot",action="store_true",help="send reboot command; destructive explicit opt-in"); App(a.parse_args()).run()
if __name__=="__main__": main()
