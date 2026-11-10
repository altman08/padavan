// Run with Node.js 18+. Execute the real ASP JavaScript with isolated browser APIs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../../www/n56u_ribbon_fixed/Advanced_quarkdrive.asp'), 'utf8');
const elements = new Map();
for (const match of source.matchAll(/\bid="([^"]+)"/g)) {
    elements.set(match[1], {value: '', checked: false, disabled: false, text: '', innerHTML: '', focus() { this.focused = true; }});
}
const get = id => {
    assert.ok(elements.has(id), `Missing page element ${id}`);
    return elements.get(id);
};
const defaults = {
    qd_cookie: 'a=1; b=two words', qd_binary: '/usr/bin/quarkdrive-webdav',
    qd_auth_user: 'user', qd_auth_password: 'password', qd_root: '/', qd_strip_prefix: '',
    qd_port: '8088', qd_read_buffer_size: '1048576', qd_upload_buffer_size: '1048576',
    qd_cache_size: '1000', qd_cache_ttl: '600', qd_refresh_cache_secs_interval: '300', qd_upload_wait_timeout: '280'
};
const form = {elements: {}, action_mode: {value: ''}, submit() { this.submitted = true; }};
for (const [key, value] of Object.entries(defaults)) {
    form[key] = form.elements[key] = get(key);
    form[key].value = value;
}
get('quarkdrive_enable_1').checked = true;
const requests = [], alerts = [];
let nextTimer = 0;
const jq = selector => {
    const selected = typeof selector === 'string' ? selector.split(',').map(s => elements.get(s.trim().slice(1))).filter(Boolean) : [];
    return {
        text(value) { for (const e of selected) e.text = value; return this; },
        attr(key, value) { for (const e of selected) e[key] = value; return this; },
        prop(key, value) { for (const e of selected) e[key] = value; return this; },
        is() { return false; }
    };
};
jq.ajax = options => requests.push(options);
const context = {
    jQuery: {noConflict: () => jq},
    document: {form, getElementById: get},
    setTimeout: () => ++nextTimer, clearTimeout() {},
    showLoading() {}, hideLoading() {}, refreshpage() {},
    alert: message => alerts.push(message)
};
vm.createContext(context);
const html = source.replace('<% quarkdrive_status(); %>', JSON.stringify({pid: 0, enabled: 0, eip93: 'off', can_control: 1})).replace(/<%[\s\S]*?%>/g, '');
for (const script of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) vm.runInContext(script[1], context);
for (const enabled of [0, 1]) {
    for (const pid of [0, 123]) {
        context.serviceState = {pid, enabled, eip93: 'active', can_control: 1};
        context.statusKnown = true;
        context.renderStatus();
        assert.equal(get('quark_start').disabled, pid > 0);
        assert.equal(get('quark_stop').disabled, !pid);
        assert.equal(get('quark_restart').disabled, !pid);
        assert.match(get('quark_status').text, pid ? /运行中/ : enabled ? /已停止/ : /未启用/);
    }
}
context.serviceState.can_control = 0;
context.renderStatus();
assert.equal(get('quark_apply').disabled, true);
context.serviceAction('start');
assert.equal(requests.length, 0);
context.serviceState.can_control = 1;
context.refreshStatus();
const stale = requests.pop();
form.qd_cookie.value = 'unsaved cookie';
context.serviceAction('restart');
const control = requests.pop();
assert.equal(control.type, 'POST');
assert.equal(control.data.quark_action, 'restart');
assert.equal(control.data.action_mode, ' QuarkDrive ');
context.serviceAction('restart');
assert.equal(requests.length, 0, 'Ignore duplicate actions while busy');
control.success({sys_result: 0});
control.complete();
stale.success({pid: 0, enabled: 0, can_control: 1, eip93: 'off'});
assert.equal(context.serviceState.pid, 123, 'Ignore status from before the control action');
const status = requests.find(r => r.url === '/quarkdrive_status.asp');
status.success({pid: 456, enabled: 1, can_control: 1, eip93: 'pending'});
assert.equal(context.serviceState.pid, 456);
assert.equal(form.qd_cookie.value, 'unsaved cookie', 'Polling must preserve unsaved settings');
status.error();
assert.equal(get('quark_stop').disabled, true);
context.statusKnown = true;
assert.equal(context.validForm(), true);
form.qd_cookie.value = 'x'.repeat(4095);
assert.equal(context.validForm(), true);
form.qd_cookie.value = 'x'.repeat(4096);
assert.equal(context.validForm(), false);
form.qd_cookie.value = '云'.repeat(1366);
assert.equal(context.validForm(), false, 'Count UTF-8 bytes, not JavaScript characters');
form.qd_cookie.value = 'a=1\nb=2';
assert.equal(context.validForm(), false);
form.qd_cookie.value = defaults.qd_cookie;
form.qd_auth_password.value = '';
assert.equal(context.validForm(), false);
form.qd_auth_user.value = '';
assert.equal(context.validForm(), true);
for (const [field, bad] of [['qd_port','65536'], ['qd_read_buffer_size','0'], ['qd_cache_ttl','1;id'], ['qd_refresh_cache_secs_interval','0'], ['qd_strip_prefix','relative']]) {
    const value = form[field].value;
    form[field].value = bad;
    assert.equal(context.validForm(), false, `${field} rejects ${bad}`);
    form[field].value = value;
}
context.serviceState.can_control = 1;
context.applyRule();
assert.equal(form.submitted, true);
assert.equal(form.action_mode.value, ' Restart ');
context.quarkdrive_apply_error('Cookie is required');
assert.equal(context.busy, false);
console.log('QuarkDrive UI: states, actions, stale responses, unsaved input and validation passed');
