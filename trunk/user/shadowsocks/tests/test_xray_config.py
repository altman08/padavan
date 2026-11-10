#!/usr/bin/env python3
"""Run with Lua 5.1 + cjson; set XRAY_BIN (colon-separated) to validate with cores."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE_NODE = {
    'server': '192.168.23.1', 'server_port': '443',
    'vmess_id': 'de04add9-5c68-4bab-950c-8cd5320df18a',
    'security': 'none', 'transport': 'tcp', 'tls': '0', 'insecure': '0',
}
PUBLIC_KEY = 'Dt0kZa7s-qikLpsBv32LJtFXbmCNubhSg7UpD15w4g8'


class XrayConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='padavan-xray-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.env = os.environ.copy()
        self.env['PATH'] = str(self.directory) + os.pathsep + self.env['PATH']
        self.env['XRAY_NODE_JSON'] = str(self.directory / 'node.json')
        dbus = self.directory / 'dbus'
        dbus.write_text('#!/bin/sh\ncat "$XRAY_NODE_JSON"\n', encoding='utf-8')
        dbus.chmod(0o755)

    def generate(self, changes=None, proto='tcp', port=1080, socks=0, error=None):
        node = dict(BASE_NODE)
        node.update(changes or {})
        Path(self.env['XRAY_NODE_JSON']).write_text(json.dumps(node), encoding='utf-8')
        result = subprocess.run(
            ['lua', str(ROOT / 'ss/genxrayconfig.lua'), '1', proto, str(port), str(socks)],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            encoding='utf-8')
        if error:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(error, result.stderr)
            self.assertEqual(result.stdout, '')
            return
        self.assertEqual(result.returncode, 0, result.stderr)
        config = json.loads(result.stdout)
        config_file = self.directory / 'config.json'
        config_file.write_text(result.stdout, encoding='utf-8')
        for binary in filter(None, self.env.get('XRAY_BIN', '').split(os.pathsep)):
            checked = subprocess.run(
                [binary, 'run', '-test', '-config', str(config_file)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
            self.assertEqual(checked.returncode, 0, binary + '\n' + checked.stdout)
        return config

    def import_node(self, uri):
        # Isolate the real subscription parser from downloading/updating router state.
        source = (ROOT / 'ss/dlink.lua').read_text(encoding='utf-8').split('-- wget')[0]
        harness = '''
package.preload.nixio = function()
    nixio = {bin = {b64decode = function(value) return value end}}
    return nixio
end
io.lines = function() return function() return nil end end
local popen = io.popen
io.popen = function(command)
    if command:find("nvram get ss_keyword", 1, true) then
        return {read = function() return "" end}
    end
    return popen(command)
end
'''
        parser = self.directory / 'parse.lua'
        parser.write_text(harness + source + '\nprint(cjson.encode(processData("vless", arg[1])))\n', encoding='utf-8')
        result = subprocess.check_output(['lua', str(parser), uri], env=self.env, encoding='utf-8')
        return json.loads(result.splitlines()[-1])

    def test_inbound_layouts(self):
        for proto, port, socks, protocols in [
            ('tcp', 1080, 0, ['dokodemo-door']),
            ('udp', 1080, 1081, ['dokodemo-door']),
            ('tcp', 0, 1081, ['socks']),
            ('tcp', 1080, 1081, ['dokodemo-door', 'socks']),
        ]:
            with self.subTest(proto=proto, port=port, socks=socks):
                config = self.generate(proto=proto, port=port, socks=socks)
                self.assertEqual([inbound['protocol'] for inbound in config['inbounds']], protocols)

    def test_reality_and_numeric_database_values(self):
        config = self.generate({
            'tls': 2, 'flow': 1, 'mux': 1, 'tls_host': 'example.com',
            'reality_public_key': PUBLIC_KEY, 'reality_short_id': '1234',
        })
        outbound = config['outbounds'][0]
        reality = outbound['streamSettings']['realitySettings']
        self.assertEqual(reality['password'], PUBLIC_KEY)
        self.assertEqual(reality['fingerprint'], 'chrome')
        self.assertEqual(reality['shortId'], '1234')
        self.assertEqual(outbound['settings']['flow'], 'xtls-rprx-vision')
        self.assertFalse(outbound['mux']['enabled'])
        self.generate({'tls': '2'}, error='pbk')

    def test_transport_hosts_and_paths(self):
        for transport, prefix, settings in [
            ('ws', 'ws', 'wsSettings'),
            ('httpupgrade', 'httpupgrade', 'httpupgradeSettings'),
            ('splithttp', 'splithttp', 'xhttpSettings'),
            ('xhttp', 'splithttp', 'xhttpSettings'),
        ]:
            with self.subTest(transport=transport):
                config = self.generate({
                    'transport': transport, 'tls': '1', 'tls_host': 'example.com',
                    prefix + '_host': 'cdn.example.com', prefix + '_path': '/path?key="a"&b=\\value',
                    'splithttp_mode': 'stream-up',
                })
                stream = config['outbounds'][0]['streamSettings']
                self.assertEqual(stream[settings]['host'], 'cdn.example.com')
                self.assertEqual(stream[settings]['path'], '/path?key="a"&b=\\value')
                if settings == 'xhttpSettings':
                    self.assertEqual(stream[settings]['mode'], 'stream-up')

    def test_raw_http_header_lists(self):
        for path, host in [('/test', 'example.com'), (['/test'], ['example.com'])]:
            config = self.generate({'tcp_guise': 'http', 'http_path': path, 'http_host': host})
            request = config['outbounds'][0]['streamSettings']['rawSettings']['header']['request']
            self.assertEqual(request['path'], ['/test'])
            self.assertEqual(request['headers']['Host'], ['example.com'])
        self.generate({'tcp_guise': 'http', 'http_path': '', 'http_host': ''})

    def test_other_supported_transports_and_defaults(self):
        for transport in ['grpc', 'kcp', 'raw', '']:
            with self.subTest(transport=transport):
                self.generate({'transport': transport})
        self.generate({'tls': '1', 'insecure': 0})
        self.generate({'tls': '1', 'pinned_peer_cert_sha256': 'ab' * 32})
        self.generate({'server': 'example.com', 'security': 'mlkem768x25519plus.native.0rtt.' + PUBLIC_KEY})

    def test_shell_preserves_escaping_and_previous_config_on_failure(self):
        script = (ROOT / 'scripts/shadowsocks.sh').read_text(encoding='utf-8')
        helper = script.split('gen_xray_config() {', 1)[1].split('\ngen_config_file()', 1)[0]
        helper = 'gen_xray_config() {' + helper
        helper = helper.replace('/etc_ro/ss/genxrayconfig.lua', str(ROOT / 'ss/genxrayconfig.lua'))
        helper = helper.replace('/tmp/ssrplus.log', str(self.directory / 'error.log'))
        helper += '\nlog() { :; }\ngen_xray_config 1 tcp 0 1081 "$1"\n'
        config_file = self.directory / 'service.json'
        node_file = Path(self.env['XRAY_NODE_JSON'])
        path = '/test?key="a"&b=\\value'
        node = dict(BASE_NODE, transport='ws', ws_path=path)
        node_file.write_text(json.dumps(node), encoding='utf-8')
        service_script = self.directory / 'generate.sh'
        service_script.write_text(helper, encoding='utf-8')
        command = ['sh', str(service_script), str(config_file)]
        subprocess.check_call(command, env=self.env)
        before = config_file.read_text(encoding='utf-8')
        self.assertEqual(json.loads(before)['outbounds'][0]['streamSettings']['wsSettings']['path'], path)
        node.update(tls='2')  # Missing REALITY key must not replace a working config.
        node_file.write_text(json.dumps(node), encoding='utf-8')
        self.assertNotEqual(subprocess.call(command, env=self.env), 0)
        self.assertEqual(config_file.read_text(encoding='utf-8'), before)
        self.assertFalse(Path(str(config_file) + '.tmp').exists())

    def test_removed_options_fail_with_actionable_errors(self):
        for transport in ['h2', 'http', 'h3', 'quic']:
            self.generate({'transport': transport}, error='XHTTP')
        self.generate({'tls': '1', 'insecure': 1}, error='allowInsecure')
        self.generate(port=0, socks=0, error='入站')

    def test_import_reality_round_trip(self):
        uri = BASE_NODE['vmess_id'] + '@example.com:443?type=raw&security=reality&sni=sni.example.com&host=cdn.example.com&pbk=' + PUBLIC_KEY + '&sid=1234&fp=firefox&spx=%2Fspider%3Fa%3D1%26b%3D2&flow=xtls-rprx-vision#Test'
        node = self.import_node(uri)
        self.assertEqual(node['tls_host'], 'sni.example.com')
        self.assertEqual(node['reality_spider_x'], '/spider?a=1&b=2')
        config = self.generate(node, port=0, socks=1081)
        reality = config['outbounds'][0]['streamSettings']['realitySettings']
        self.assertEqual(reality['fingerprint'], 'firefox')
        self.assertEqual(reality['password'], PUBLIC_KEY)
        self.assertEqual(reality['spiderX'], '/spider?a=1&b=2')

    def test_import_transports_without_fragment(self):
        for transport, prefix in [('xhttp', 'splithttp'), ('splithttp', 'splithttp'), ('httpupgrade', 'httpupgrade')]:
            with self.subTest(transport=transport):
                node = self.import_node(BASE_NODE['vmess_id'] + '@[fd00::1]:443?type=' + transport + '&security=tls&host=cdn.example.com&path=%2Ftest%3Fa%3D1%26b%3D2&mode=stream-up')
                self.assertEqual(node['server'], 'fd00::1')
                self.assertEqual(node[prefix + '_host'], 'cdn.example.com')
                self.assertEqual(node[prefix + '_path'], '/test?a=1&b=2')
                self.assertEqual(node['flow'], '0')
                self.generate(node)
        node = self.import_node(BASE_NODE['vmess_id'] + '@192.168.23.1:443')
        self.assertEqual(node['tls'], '0')
        self.assertEqual(node['transport'], 'tcp')
        self.generate(node)


if __name__ == '__main__':
    unittest.main(verbosity=2)
