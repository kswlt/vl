"""Offline checks for plan mode and credential shell quoting; no device access."""
import contextlib,importlib.util,io,subprocess,sys,unittest
from pathlib import Path
from unittest.mock import patch
path=Path(__file__).with_name('deploy_windows.py')
spec=importlib.util.spec_from_file_location('deploy_windows',path)
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class DeployTests(unittest.TestCase):
    def test_default_plan_never_connects_or_asks_password(self):
        with patch.object(sys,'argv',[str(path),'--host','198.51.100.180','--sn','DEVICE00000002']),patch.object(mod,'run') as call,patch.object(mod.getpass,'getpass') as ask,contextlib.redirect_stdout(io.StringIO()):
            mod.main();call.assert_not_called();ask.assert_not_called()
    def test_invalid_sn_rejected_before_any_connection(self):
        with patch.object(sys,'argv',[str(path),'--host','198.51.100.180','--sn','bad;reboot']),patch.object(mod,'run') as call,contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):mod.main()
            call.assert_not_called()
    def test_shell_credentials_keep_metacharacters_literal(self):
        password="single' double\" dollar$(echo WRONG) `echo BAD`; back\\slash"
        encoded=mod.shell_value(password)
        # shlex.split models parsing of the assignment value without executing it.
        self.assertEqual(mod.shlex.split(encoded),[password])
    def test_newline_and_nul_rejected(self):
        for password in ['line\nnext','line\rnext','null\x00']:
            with self.assertRaises(ValueError):mod.shell_value(password)
if __name__=='__main__':unittest.main()
