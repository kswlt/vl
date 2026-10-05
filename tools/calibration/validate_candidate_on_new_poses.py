"""Independent numerical check of an extrinsic on a later capture set."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from local_calibrate import detector, detect_tags, tag_object_corners

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--dataset',type=Path,required=True)
    args=p.parse_args()
    c=json.loads(args.candidate.read_text(encoding='utf-8'))
    camera=c['camera']; ext=c['lidar_to_camera']
    matrix=np.array([[camera['fx'],0,camera['cx']],[0,camera['fy'],camera['cy']],[0,0,1]],np.float64)
    dist=np.array([camera['distortion'][x] for x in ('k1','k2','p1','p2','k3')],np.float64)
    R=np.array(ext['R']); t=np.array(ext['t']); ar=detector()
    print('translation_norm_m',round(float(np.linalg.norm(t)),4))
    for pose in sorted((args.dataset/'poses').glob('pose_*')):
        image=cv2.imread(str(pose/'image.jpg'))
        tags,_=detect_tags(image,ar)
        obj=np.array([corner for tid in sorted(tags) for corner in tag_object_corners(tid)],np.float32)
        pixels=np.array([corner for tid in sorted(tags) for corner in tags[tid]],np.float32)
        if len(tags)<3: continue
        good,rv,tv=cv2.solvePnP(obj,pixels,matrix,dist,flags=cv2.SOLVEPNP_ITERATIVE)
        if not good: continue
        Br,_=cv2.Rodrigues(rv)
        with np.load(pose/'points.npz') as data: points=data['points']
        camera_points=points@R.T+t
        board_points=(camera_points-tv.reshape(3))@Br
        footprint=((board_points[:,0]>=-0.05)&(board_points[:,0]<=1.45)&
                   (board_points[:,1]>=-0.05)&(board_points[:,1]<=1.05))
        within_15cm=footprint&(np.abs(board_points[:,2])<0.15)
        within_4cm=footprint&(np.abs(board_points[:,2])<0.04)
        hole_centers=((0.45,0.3),(0.95,0.3),(0.45,0.7),(0.95,0.7))
        hole=np.zeros(len(points),dtype=bool)
        for hx,hy in hole_centers:
            hole|=((board_points[:,0]-hx)**2+(board_points[:,1]-hy)**2)<0.12**2
        nominal=((board_points[:,0]>=0)&(board_points[:,0]<=1.4)&
                 (board_points[:,1]>=0)&(board_points[:,1]<=1.0))
        plane=nominal&(np.abs(board_points[:,2])<0.06)
        hole_area=4*np.pi*0.12**2
        outside_density=np.count_nonzero(plane&~hole)/(1.4-hole_area)
        hole_density=np.count_nonzero(plane&hole)/hole_area
        print(pose.name,'tags',len(tags),'footprint',int(footprint.sum()),
              'skin15cm',int(within_15cm.sum()),'skin4cm',int(within_4cm.sum()),
              'skin_median_m',round(float(np.median(board_points[within_15cm,2])),4) if within_15cm.any() else None,
              'hole_density_ratio',round(hole_density/outside_density,3) if outside_density else None)

if __name__=='__main__': main()
