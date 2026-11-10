#!/usr/bin/env python3
"""Exercise the actual service script with isolated NVRAM and a native process."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PACKAGE = Path(__file__).resolve().parents[1]
DEFAULTS = {
    'quarkdrive_enable': '1', 'qd_cookie': 'a=one; b=two words',
    'qd_auth_user': 'user name', 'qd_auth_password': 'secret with spaces',
    'qd_host': '0.0.0.0', 'qd_port': '8088', 'qd_root': '/my drive',
    'qd_strip_prefix': '/dav', 'qd_read_buffer_size': '1048576',
    'qd_upload_buffer_size': '1048576', 'qd_cache_size': '1000',
    'qd_cache_ttl': '600', 'qd_refresh_cache_secs_interval': '300',
    'qd_upload_wait_timeout': '280', 'qd_auto_index': '1', 'qd_read_only': '1',
    'qd_no_trash': '0', 'qd_skip_upload_same_size': '0',
    'qd_prefer_http_download': '0', 'qd_redirect': '0', 'qd_debug': '0'
}


class ServiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix='padavan-quark-service-')
        cls.base = Path(cls.workspace.name)
        cls.native = cls.base / 'native.c'
        cls.binary = cls.base / 'quarkdrive-webdav'
        cls.native.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int sig) { stopped = 1; }
int main(int argc, char **argv) {
    if (getenv("TEST_FAIL")) return 7;
    FILE *fp = fopen(getenv("TEST_CAPTURE"), "w");
    fprintf(fp, "cookie=%s\nuser=%s\npassword=%s\n", getenv("QUARK_COOKIE"),
        getenv("WEBDAV_AUTH_USER") ?: "", getenv("WEBDAV_AUTH_PASSWORD") ?: "");
    for (int i=1; i<argc; i++) fprintf(fp, "arg=%s\n", argv[i]);
    fclose(fp);
    signal(SIGTERM, stop);
    while (!stopped) pause();
    if (getenv("TEST_DELAY")) sleep(2);
    return 0;
}
''')
        subprocess.run(['cc', '-std=gnu99', '-o', str(cls.binary), str(cls.native)], check=True)
        cls.mockbin = cls.base / 'bin'
        cls.mockbin.mkdir()
        (cls.mockbin / 'nvram').write_text('#!/usr/bin/env python3\nimport json,os,sys\nprint(json.load(open(os.environ["TEST_NVRAM"])).get(sys.argv[2],""))\n')
        (cls.mockbin / 'logger').write_text('#!/bin/sh\nif [ "$#" -eq 2 ]; then cat >/dev/null; fi\n')
        for mock in cls.mockbin.iterdir():
            mock.chmod(0o755)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=str(self.base))
        self.path = Path(self.temp.name)
        self.runtime = self.path / 'runtime'
        self.script = self.path / 'control.sh'
        self.script.write_text(PACKAGE.joinpath('quarkdrive-webdav.sh').read_text().replace('/tmp/quarkdrive-webdav', str(self.runtime)))
        self.config = dict(DEFAULTS, qd_binary=str(self.binary))
        self.nvram = self.path / 'nvram.json'
        self.capture = self.path / 'capture'
        self.env = dict(os.environ, TEST_NVRAM=str(self.nvram), TEST_CAPTURE=str(self.capture),
                        PATH=str(self.mockbin) + ':' + os.environ['PATH'], LC_ALL='C')
        for key in ['TEST_DELAY', 'TEST_FAIL']:
            self.env.pop(key, None)

    def run_service(self, action, success=True):
        self.nvram.write_text(json.dumps(self.config))
        result = subprocess.run(['sh', str(self.script), action], env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=18)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode())
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def tearDown(self):
        self.run_service('stop')
        self.temp.cleanup()

    def pid(self):
        return (self.runtime / 'pid').read_text().strip()

    def test_credentials_and_arguments_are_data(self):
        marker = self.path / 'must-not-exist'
        self.config['qd_cookie'] = 'name="quoted"; semi=two words; value=$(touch ' + str(marker) + '); tick=`id`'
        self.run_service('start')
        content = self.capture.read_text()
        self.assertIn('cookie=' + self.config['qd_cookie'] + '\n', content)
        self.assertIn('password=secret with spaces\n', content)
        self.assertIn('arg=/my drive\n', content)
        self.assertIn('arg=--strip-prefix\narg=/dav\n', content)
        self.assertIn('arg=--read-only\n', content)
        self.assertNotIn('--quark-cookie', content)
        cmdline = Path('/proc', self.pid(), 'cmdline').read_bytes()
        self.assertNotIn(self.config['qd_cookie'].encode(), cmdline)
        self.assertFalse(marker.exists())
        self.run_service('status')

    def test_disabled_boot_and_temporary_start(self):
        self.config['quarkdrive_enable'] = '0'
        self.run_service('start')
        self.assertFalse(self.capture.exists())
        self.run_service('start_now')
        self.run_service('status')
        self.assertEqual(json.loads(self.nvram.read_text())['quarkdrive_enable'], '0')
        self.run_service('restart')
        self.run_service('status', False)

    def test_repeated_start_and_stop(self):
        self.run_service('start')
        first = self.pid()
        self.run_service('start')
        self.assertEqual(first, self.pid())
        self.run_service('stop')
        self.run_service('stop')
        self.run_service('status', False)

    def test_invalid_restart_keeps_running_process(self):
        self.run_service('start')
        first = self.pid()
        for changes in [{'qd_port': '65536'}, {'qd_cookie': ''},
                        {'qd_auth_password': ''}, {'qd_binary': '/missing'},
                        {'qd_read_buffer_size': '16777217'}, {'qd_refresh_cache_secs_interval': '0'}]:
            saved = self.config.copy()
            self.config.update(changes)
            self.run_service('restart', False)
            self.assertEqual(self.pid(), first)
            self.run_service('status')
            self.config = saved

    def test_restart_waits_for_old_process(self):
        self.env['TEST_DELAY'] = '1'
        self.run_service('start')
        first = self.pid()
        self.run_service('restart')
        self.assertNotEqual(first, self.pid())
        self.assertFalse(Path('/proc', first, 'exe').exists())

    def test_failed_launch_removes_runtime_state(self):
        self.env['TEST_FAIL'] = '1'
        self.run_service('start', False)
        self.assertFalse((self.runtime / 'pid').exists())
        self.run_service('status', False)

    def test_numeric_boolean_and_path_validation(self):
        for key, value in [('qd_port', '1;id'), ('qd_upload_wait_timeout', '-1'),
                           ('qd_cache_size', '999999999999999999999'), ('qd_read_only', 'yes'),
                           ('qd_binary', 'relative/path'), ('qd_root', 'relative'),
                           ('qd_strip_prefix', 'relative'), ('qd_cookie', 'x' * 4096)]:
            saved = self.config.copy()
            self.config[key] = value
            self.run_service('check', False)
            self.config = saved
        self.config['qd_cookie'] = 'x' * 4095
        self.config['qd_auth_user'] = self.config['qd_auth_password'] = ''
        self.config['qd_upload_wait_timeout'] = self.config['qd_cache_size'] = '0'
        self.run_service('check')

    def test_foreign_pid_is_never_signalled(self):
        foreign = subprocess.Popen(['sleep', '30'])
        try:
            self.runtime.mkdir()
            (self.runtime / 'pid').write_text(str(foreign.pid))
            (self.runtime / 'exe').write_text(str(self.binary))
            self.run_service('stop')
            self.assertIsNone(foreign.poll())
        finally:
            foreign.terminate()
            foreign.wait()

    def test_stale_control_lock_recovers(self):
        lock = self.runtime / 'control.lock'
        lock.mkdir(parents=True)
        (lock / 'owner').write_text('2147483647')
        self.run_service('start')
        self.run_service('status')


if __name__ == '__main__':
    unittest.main()
