"""Windows OpenSSH uploader. Plan by default; apply explicitly. No third-party Python modules."""
import argparse,getpass,hashlib,ipaddress,json,re,shlex,subprocess,sys,tarfile,tempfile,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def shell_value(value):
    if any(c in value for c in '\r\n\x00'): raise ValueError('newlines/NUL forbidden in credentials')
    return shlex.quote(value)
def run(argv,**kw):
    return subprocess.run(argv,check=True,**kw)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',required=True);p.add_argument('--sn',required=True)
    p.add_argument('--user',default='root');p.add_argument('--camera-user',default='admin')
    p.add_argument('--configure',action='store_true',help='replace an existing config after backup')
    p.add_argument('--apply',action='store_true');p.add_argument('--check-only',action='store_true')
    p.add_argument('--radar-interface',choices=['eth0','eth1'],default='eth0')
    p.add_argument('--radar-source',default='192.0.2.179')
    a=p.parse_args()
    ipaddress.IPv4Address(a.host);ipaddress.IPv4Address(a.radar_source)
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',a.sn) or a.sn in {'UNCONFIGURED','REPLACE_WITH_UNIQUE_SN'}:p.error('SN must be a unique topic segment')
    if not re.fullmatch(r'[a-z_][a-z0-9_-]*',a.user):p.error('invalid SSH user')
    if not re.fullmatch(r'[A-Za-z0-9_-]+',a.camera_user):p.error('camera user must contain only letters, numbers, underscore or hyphen')
    target=f'{a.user}@{a.host}'
    print(f'Target: {target}; SN: {a.sn}; mode: '+('check-only' if a.check_only else 'apply' if a.apply else 'plan'))
    print('Install fresh xt_gateway/xt_camera/xt_radar; keep network and factory files; never copy source/calibration.')
    if not a.apply and not a.check_only:
        print('No connection or changes made. Add --apply to upload and install, or --check-only for remote preflight.');return
    # Verify delivery tree before creating any per-device secrets.
    run([sys.executable,str(ROOT/'verify_package.py')])
    password=getpass.getpass('Camera password (not SSH password; input hidden): ')
    if not password:raise SystemExit('camera password required')
    shell_value(password)
    digest=hashlib.md5(password.encode('utf-8')).hexdigest().upper()
    cfg=json.loads((ROOT/'source/config/board.template.json').read_text(encoding='utf-8'))
    cfg['legacy_compatibility']['legacy_sn']=a.sn
    cfg['radar_network']={'interface':a.radar_interface,'source_address':a.radar_source}
    runtime=ROOT/'runtime'
    token=uuid.uuid4().hex
    remote=f'/userdata/xtdeploy-{token}'
    with tempfile.TemporaryDirectory(prefix='rkdeploy-') as tmp:
        tmp=Path(tmp);profile=tmp/'profile';profile.mkdir()
        (profile/'config.json').write_text(json.dumps(cfg,indent=2)+'\n',encoding='utf-8')
        (profile/'device_identity.yaml').write_text(f'device_sn: "{a.sn}"\n',encoding='utf-8')
        (profile/'runtime.env').write_text('XT_DEVICE_SN_FILE=/userdata/xtapp/device_identity.yaml\n',encoding='utf-8')
        env=f'XT_CAMERA_USER={shell_value(a.camera_user)}\nXT_CAMERA_PASSWORD={shell_value(password)}\nXT_CAMERA_PASSWORD_MD5={shell_value(digest)}\nXT_DEVICE_SN_FILE=/userdata/xtapp/device_identity.yaml\n'
        (profile/'camera.env').write_text(env,encoding='utf-8')
        if a.configure:(profile/'ALLOW_CONFIG_REPLACE').write_text('explicit replacement requested\n',encoding='utf-8')
        archive=tmp/'deployment.tar.gz';entries={}
        for sub in ['app','init']:
            for f in (runtime/sub).iterdir():
                if f.is_file() and f.name!='vizd':entries[f'{sub}/{f.name}']=f
        entries['install.sh']=ROOT/'deploy/install.sh'
        for f in profile.iterdir():entries[f'profile/{f.name}']=f
        sums=tmp/'SHA256SUMS';sums.write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+name+'\n' for name,f in sorted(entries.items())),encoding='utf-8')
        entries['SHA256SUMS']=sums
        with tarfile.open(archive,'w:gz') as tar:
            for name,f in sorted(entries.items()):
                info=tar.gettarinfo(str(f),name);info.mode=0o600 if name.startswith('profile/') else 0o755;info.uid=info.gid=0
                with f.open('rb') as stream:tar.addfile(info,stream)
        # OpenSSH keeps its normal known_hosts verification; no host-key bypass.
        run(['ssh',target,f'umask 077; mkdir {remote}'])
        try:
            run(['scp','-O',str(archive),f'{target}:{remote}/deployment.tar.gz'])
            action='check' if a.check_only else 'apply'
            command=f'cd {remote} && tar -xzf deployment.tar.gz && sh ./install.sh {action}'
            run(['ssh',target,command])
        finally:
            # Only delete files in the fixed, freshly-created UUID stage.
            run(['ssh',target,f'rm -f {remote}/profile/camera.env {remote}/deployment.tar.gz'])
        print(f'Remote stage retained for diagnostics: {remote} (temporary credential files removed)')
if __name__=='__main__':main()
