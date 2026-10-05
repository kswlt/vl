"""Linux-only installer simulation: redirects fixed paths into a temporary fixture.

No real board, system services, factory files, or network are touched. This tests
backup/restore and failure handling, not board ABI or service functionality.
"""
import hashlib,os,re,subprocess,tempfile,unittest
from pathlib import Path
INSTALL=Path(__file__).with_name('install.sh')
class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='rk-install-test-');self.root=Path(self.tmp.name)
        self.app=self.root/'userdata/xtapp';self.init=self.root/'etc/init.d';self.stage=self.root/'stage';self.shim=self.root/'shim'
        for p in [self.app,self.init,self.stage/'app',self.stage/'profile',self.stage/'init',self.shim]:p.mkdir(parents=True,exist_ok=True)
        def executable(f,s):f.write_text(s);f.chmod(0o755)
        executable(self.shim/'id','#!/bin/sh\necho 0\n')
        executable(self.shim/'uname','#!/bin/sh\necho aarch64\n')
        executable(self.shim/'ip','#!/bin/sh\nexit 0\n')
        executable(self.shim/'gst-inspect-1.0','#!/bin/sh\nexit 0\n')
        executable(self.shim/'sleep','#!/bin/sh\nexit 0\n')
        executable(self.shim/'pidof',f'#!/bin/sh\n[ ! -f "{self.root}/fail-process" ]\n')
        gst=self.root/'usr/bin/gst-launch-1.0';gst.parent.mkdir(parents=True);executable(gst,'#!/bin/sh\nexit 0\n')
        executable(self.app/'zenohd','#!/bin/sh\nexit 0\n')
        self.original={}
        for name in ['xt_camera','xt_radar','xt_gateway','config.json','camera.env','runtime.env','device_identity.yaml']:
            (self.app/name).write_text('old-'+name);self.original[name]=(self.app/name).read_bytes()
        for name in ['xt_camera','xt_radar','xt_gateway']:(self.stage/'app'/name).write_text('new-'+name)
        for name in ['config.json','camera.env','runtime.env','device_identity.yaml','ALLOW_CONFIG_REPLACE']:(self.stage/'profile'/name).write_text('new-'+name)
        for name in ['S97zenohd','S98zzgateway','S99xtradar','S99xtcamera']:
            executable(self.init/name,'#!/bin/sh\nexit 0\n');executable(self.stage/'init'/name,'#!/bin/sh\nexit 0\n')
        # Only replace literal system paths; the control flow is the delivered installer.
        script=INSTALL.read_text().replace('/userdata',str(self.root/'userdata')).replace('/etc/init.d',str(self.init)).replace('/usr/bin',str(self.root/'usr/bin'))
        (self.stage/'install.sh').write_text(script)
        files=[f for f in self.stage.rglob('*') if f.is_file()]
        (self.stage/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.relative_to(self.stage).as_posix()+'\n' for f in files))
        self.env=dict(os.environ,PATH=str(self.shim)+':'+os.environ['PATH'])
    def tearDown(self):self.tmp.cleanup()
    def run_installer(self,*args):
        return subprocess.run(['sh',str(self.stage/'install.sh'),*args],env=self.env,capture_output=True,text=True)
    def assert_restored(self):
        for name,original in self.original.items():self.assertEqual((self.app/name).read_bytes(),original,name)
    def test_success_then_explicit_rollback_restores_every_managed_app_file(self):
        result=self.run_installer('apply');self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual((self.app/'xt_camera').read_text(),'new-xt_camera')
        backup=re.search(r'^BACKUP=(.+)$',result.stdout,re.M).group(1)
        result=self.run_installer('rollback',backup);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assert_restored()
    def test_corrupted_input_fails_before_replacing_app(self):
        (self.stage/'app/xt_camera').write_text('corrupted')
        result=self.run_installer('apply');self.assertNotEqual(result.returncode,0)
        self.assert_restored();self.assertFalse(list(self.app.glob('backup-delivery-*')))
    def test_failed_process_check_automatically_restores_backup(self):
        (self.root/'fail-process').touch()
        result=self.run_installer('apply');self.assertNotEqual(result.returncode,0)
        self.assertIn('ROLLED_BACK=',result.stdout);self.assert_restored()
if __name__=='__main__':unittest.main()
