"""Test whether board observations also admit a small camera-LiDAR baseline."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from scipy.optimize import least_squares
from local_calibrate import detector, detect_tags, tag_object_corners

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--dataset',type=Path,required=True)
    p.add_argument('--output',type=Path)
    p.add_argument('--skip',nargs='*',default=[])
    args=p.parse_args()
    c=json.loads(args.candidate.read_text(encoding='utf-8'))
    cam=c['camera']; ext=c['lidar_to_camera']; ar=detector()
    K=np.array([[cam['fx'],0,cam['cx']],[0,cam['fy'],cam['cy']],[0,0,1]],np.float64)
    D=np.array([cam['distortion'][k] for k in ('k1','k2','p1','p2','k3')])
    R0=np.array(ext['R']); t0=np.array(ext['t'])
    pairs=[]
    for pose in sorted((args.dataset/'poses').glob('pose_*')):
        if pose.name in args.skip: continue
        image=cv2.imread(str(pose/'image.jpg'))
        tags,_=detect_tags(image,ar)
        if len(tags)<3: continue
        obj=np.array([v for tid in sorted(tags) for v in tag_object_corners(tid)],np.float32)
        pix=np.array([v for tid in sorted(tags) for v in tags[tid]],np.float32)
        ok,rv,tv=cv2.solvePnP(obj,pix,K,D)
        if not ok: continue
        B,_=cv2.Rodrigues(rv)
        with np.load(pose/'points.npz') as data: points=data['points'].astype(np.float64)
        board=(points@R0.T+t0-tv.reshape(3))@B
        selected=((board[:,0]>0.05)&(board[:,0]<1.35)&(board[:,1]>0.05)&
                  (board[:,1]<0.95)&(np.abs(board[:,2])<0.08))
        selected_points=points[selected]
        if len(selected_points)>500:
            selected_points=selected_points[np.random.default_rng(42).choice(len(selected_points),500,False)]
        pairs.append((pose.name,selected_points,tv.reshape(3),B[:,2]))
    print('pairs',[(name,len(pts)) for name,pts,_,_ in pairs])
    def residual(params):
        dR,_=cv2.Rodrigues(params[:3])
        R=dR@R0; t=params[3:]
        return np.concatenate([((pts@R.T+t-center)@normal) for _,pts,center,normal in pairs])
    init=np.r_[np.zeros(3),np.clip(t0,-0.2,0.2)]
    fit=least_squares(residual,init,bounds=(np.r_[[-0.45]*3,[-0.2]*3],
                                            np.r_[[0.45]*3,[0.2]*3]),max_nfev=100)
    print('candidate_t',t0,'candidate_plane_rms_m',float(np.sqrt(np.mean(np.concatenate([
        ((pts@R0.T+t0-center)@normal) for _,pts,center,normal in pairs])**2))))
    print('bounded_t',fit.x[3:],'rotation_delta_deg',np.degrees(np.linalg.norm(fit.x[:3])),
          'bounded_plane_rms_m',float(np.sqrt(np.mean(fit.fun**2))))
    for fixed_t in (np.array([0.0,-0.075,-0.019]),
                    np.array([0.0,0.075,-0.019])):
        fit_rot=least_squares(lambda v: residual(np.r_[v,fixed_t]),
                              np.zeros(3),bounds=([-0.45]*3,[0.45]*3),max_nfev=100)
        print('fixed_t',fixed_t,'rotation_delta_deg',np.degrees(np.linalg.norm(fit_rot.x)),
              'plane_rms_m',float(np.sqrt(np.mean(fit_rot.fun**2))))
        if args.output is not None and fixed_t[1] < 0:
            delta,_=cv2.Rodrigues(fit_rot.x)
            result=dict(c)
            result['lidar_to_camera']=dict(ext)
            result['lidar_to_camera']['R']=(delta@R0).tolist()
            result['lidar_to_camera']['t']=fixed_t.tolist()
            result['valid']=False
            result['calibration_status']='physical_baseline_constrained_candidate'
            args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__': main()
