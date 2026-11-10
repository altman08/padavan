// Run with Node.js. Exercises the actual ASP JavaScript with a small DOM stub.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../../www/n56u_ribbon_fixed/Shadowsocks.asp'), 'utf8').replace(/<%[\s\S]*?%>/g, '');
const elements = new Map();
for (const match of html.matchAll(/\bid="([^"]+)"/g)) {
    elements.set(match[1], {value: '', checked: false, style: {}});
}
for (const match of html.matchAll(/<select\b[^>]*\bid="([^"]+)"[^>]*>([\s\S]*?)<\/select>/g)) {
    const element = elements.get(match[1]);
    element.options = [...match[2].matchAll(/<option\b[^>]*\bvalue="([^"]*)"/g)].map(option => ({value: option[1]}));
    let value = element.options[0].value;
    Object.defineProperty(element, 'value', {
        get: () => value,
        set: input => { value = element.options.some(option => option.value === String(input)) ? String(input) : ''; }
    });
}
function get(id) {
    assert.ok(elements.has(id), `Missing element: ${id}`);
    return elements.get(id);
}
// The import status is supplied dynamically by the editor.
elements.set('import-status', {innerHTML: ''});
const jquery = () => ({ready() {}, fadeIn() {}});
const alerts = [];
let uri;
const context = {
    window: {},
    document: {
        getElementById: get,
        form: {ssp_type: get('ssp_type'), v2_transport: get('v2_transport')},
        createEvent: () => ({initEvent() {}})
    },
    jQuery: {noConflict: () => jquery},
    showhide_div: (id, shown) => {
        const element = elements.get(id);
        if (element) element.style.display = shown ? '' : 'none';
    },
    prompt: () => uri,
    alert: message => alerts.push(message),
    console: {log() {}}
};
vm.createContext(context);
for (const script of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) {
    vm.runInContext(script[1], context);
}
let saved;
context.push_data = values => { saved = JSON.parse(Object.values(values)[0]); };
const uuid = 'de04add9-5c68-4bab-950c-8cd5320df18a';
const key = 'Dt0kZa7s-qikLpsBv32LJtFXbmCNubhSg7UpD15w4g8';
uri = `vless://${uuid}@example.com:443?type=raw&security=reality&sni=sni.example.com&pbk=${key}&sid=1234&fp=firefox&spx=%2Fx%3Fa%3D1%26b%3D2&flow=xtls-rprx-vision#Test`;
context.import_ssr_url(null, 'import', null);
assert.equal(get('row_v2_reality_public_key').style.display, '');
context.add_ss();
assert.equal(saved.reality_public_key, key);
assert.equal(saved.reality_short_id, '1234');
assert.equal(saved.fingerprint, 'firefox');
assert.equal(saved.reality_spider_x, '/x?a=1&b=2');
assert.equal(saved.tls_host, 'sni.example.com');
assert.equal(saved.flow, '1');
context.initSSParams();
context.showSSEditor(saved);
context.add_ss();
assert.equal(saved.reality_public_key, key);
assert.equal(saved.reality_spider_x, '/x?a=1&b=2');

uri = `vless://${uuid}@[fd00::1]:443?type=xhttp&security=tls&host=cdn.example.com&path=%2Ftest%3Fa%3D1%26b%3D2&mode=stream-up`;
context.import_ssr_url(null, 'import', null);
assert.equal(get('ssp_server').value, 'fd00::1');
context.add_ss();
assert.equal(saved.splithttp_path, '/test?a=1&b=2');
assert.equal(saved.splithttp_host, 'cdn.example.com');
assert.equal(saved.splithttp_mode, 'stream-up');
assert.equal(saved.flow, '0');
get('v2_vless_encryption').value = 'mlkem768x25519plus.native.0rtt.' + key;
context.add_ss();
assert.equal(saved.security, 'mlkem768x25519plus.native.0rtt.' + key);
context.initSSParams();
context.showSSEditor(saved);
assert.equal(get('v2_vless_encryption').value, saved.security);
assert.equal(get('v2_splithttp_path').value, '/test?a=1&b=2');
assert.equal(get('v2_splithttp_mode').value, 'stream-up');

get('v2_transport').value = 'h2';
context.add_ss();
assert.match(alerts.pop(), /H2\/QUIC/);
get('v2_transport').value = 'tcp';
get('v2_tls').value = '2';
context.add_ss();
assert.match(alerts.pop(), /pbk/);
get('ssp_type').value = 'ss';
context.switch_ss_type();
assert.equal(get('row_v2_reality_public_key').style.display, 'none');
assert.equal(get('row_v2_splithttp_mode').style.display, 'none');
get('ssp_type').value = 'v2ray';
context.switch_ss_type();
assert.equal(get('v2_transport').options.find(option => option.value === 'h2').disabled, false);
assert.equal(alerts.length, 0);
console.log('Xray UI import, edit, save, and validation checks passed');
