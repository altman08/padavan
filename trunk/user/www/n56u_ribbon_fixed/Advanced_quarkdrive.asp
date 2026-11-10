<!DOCTYPE html>
<html>
<head>
<title><#Web_Title#> - 夸克网盘 WebDAV</title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="Pragma" content="no-cache">
<meta http-equiv="Expires" content="-1">
<link rel="shortcut icon" href="images/favicon.ico">
<link rel="stylesheet" href="/bootstrap/css/bootstrap.min.css">
<link rel="stylesheet" href="/bootstrap/css/main.css">
<link rel="stylesheet" href="/bootstrap/css/engage.itoggle.css">
<script src="/jquery.js"></script>
<script src="/bootstrap/js/bootstrap.min.js"></script>
<script src="/bootstrap/js/engage.itoggle.min.js"></script>
<script src="/state.js"></script>
<script src="/general.js"></script>
<script src="/itoggle.js"></script>
<script src="/popup.js"></script>
<script src="/help.js"></script>
<script>
var $j = jQuery.noConflict();
var serviceState = <% quarkdrive_status(); %>;
var busy = false, statusKnown = true, pollTimer, logTimer, statusGeneration = 0, logGeneration = 0;
function initial() {
    show_banner(2); show_menu(5,21,0); show_footer();
    init_itoggle('quarkdrive_enable');
    $j('#quark_tabs a').click(function(event) {
        event.preventDefault();
        $j('#quark_tabs li').removeClass('active');
        $j(this).parent().addClass('active');
        $j('.quark_panel').hide();
        $j($j(this).attr('href')).show();
        if ($j(this).attr('href') === '#quark_log') refreshLog();
    });
    renderStatus();
    refreshStatus();
}
function renderStatus() {
    var running = serviceState.pid > 0;
    var text = running ? '运行中' + (serviceState.enabled ? '' : '（未启用开机启动）') + ' · PID ' + serviceState.pid :
        (serviceState.enabled ? '已停止' : '未启用，服务当前未运行');
    $j('#quark_status').text(statusKnown ? text : '状态读取失败').attr('class', 'label label-' + (statusKnown && running ? 'success' : 'warning'));
    var acceleration = {off:'未启用或设备不支持', pending:'等待符合条件的 TLS 数据', active:'本次运行已使用 EIP93', software:'已回退到软件'};
    $j('#quark_acceleration').text(statusKnown && running ? acceleration[serviceState.eip93] || '未知' : '—');
    var disabled = busy || !statusKnown || !serviceState.can_control;
    $j('#quark_start').prop('disabled', disabled || running);
    $j('#quark_stop, #quark_restart').prop('disabled', disabled || !running);
    $j('#quark_apply').prop('disabled', busy || !serviceState.can_control);
}
function refreshStatus() {
    clearTimeout(pollTimer);
    if (busy) return;
    var generation = ++statusGeneration;
    $j.ajax({url:'/quarkdrive_status.asp', dataType:'json', cache:false, timeout:8000,
        success:function(data) {
            if (busy || generation !== statusGeneration) return;
            if (!data || typeof data.pid !== 'number' || typeof data.enabled !== 'number' || typeof data.can_control !== 'number') {
                statusKnown = false;
            } else { serviceState = data; statusKnown = true; }
            renderStatus();
        },
        error:function() { if (!busy && generation === statusGeneration) { statusKnown = false; renderStatus(); } },
        complete:function() { if (!busy && generation === statusGeneration) pollTimer = setTimeout(refreshStatus,5000); }
    });
}
function serviceAction(action) {
    if (busy || !statusKnown || !serviceState.can_control || ['start','stop','restart'].indexOf(action) < 0) return;
    busy = true; ++statusGeneration; clearTimeout(pollTimer); renderStatus();
    $j('#quark_feedback').text('正在执行，请稍候…');
    $j.ajax({url:'/apply.cgi', type:'POST', dataType:'json', timeout:30000,
        data:{action_mode:' QuarkDrive ', quark_action:action},
        success:function(data) {
            $j('#quark_feedback').text(data && data.sys_result === 0 ? '操作完成' : '操作失败，请查看运行日志');
        },
        error:function() { $j('#quark_feedback').text('请求失败，请刷新状态并查看日志'); },
        complete:function() { busy = false; statusKnown = false; renderStatus(); refreshStatus(); refreshLog(); }
    });
}
function refreshLog() {
    clearTimeout(logTimer);
    var generation = ++logGeneration;
    $j.ajax({url:'/quarkdrive_log.asp', dataType:'html', cache:false, timeout:8000,
        success:function(data) {
            if (generation !== logGeneration) return;
            // The server escapes every log character before emitting HTML.
            document.getElementById('quark_log_text').innerHTML = data || '暂无服务日志';
        },
        error:function() { if (generation === logGeneration) $j('#quark_log_text').text('日志读取失败'); },
        complete:function() {
            if (generation === logGeneration && $j('#quark_log').is(':visible')) logTimer = setTimeout(refreshLog,5000);
        }
    });
}
function reveal(id) {
    var input = document.getElementById(id);
    input.type = input.type === 'password' ? 'text' : 'password';
}
function validationError(message, field) {
    alert(message);
    if (field) field.focus();
    return false;
}
function validForm() {
    var form = document.form;
    var textLimits = {qd_binary:255, qd_cookie:4095, qd_auth_user:255, qd_auth_password:255, qd_root:1023, qd_strip_prefix:1023};
    for (var key in textLimits) {
        var value = form.elements[key].value;
        if (/[\r\n]/.test(value) || unescape(encodeURIComponent(value)).length > textLimits[key])
            return validationError('内容过长或包含换行；Cookie 最多支持 4095 字节。', form.elements[key]);
    }
    var enabled = document.getElementById('quarkdrive_enable_1').checked;
    if (enabled && !form.qd_cookie.value.trim()) return validationError('请填写完整的夸克网盘 Cookie。',form.qd_cookie);
    if (!!form.qd_auth_user.value !== !!form.qd_auth_password.value)
        return validationError('WebDAV 用户名和密码必须同时填写或同时留空。',form.qd_auth_user);
    if (form.qd_binary.value.charAt(0) !== '/' || form.qd_root.value.charAt(0) !== '/')
        return validationError('程序路径和网盘根目录必须以 / 开头。');
    if (form.qd_strip_prefix.value && form.qd_strip_prefix.value.charAt(0) !== '/')
        return validationError('URL 前缀必须以 / 开头。',form.qd_strip_prefix);
    var bounds = {qd_port:[1,65535], qd_read_buffer_size:[1,16777216], qd_upload_buffer_size:[1,16777216],
        qd_cache_size:[0,100000], qd_cache_ttl:[0,2147483647], qd_refresh_cache_secs_interval:[1,2147483647], qd_upload_wait_timeout:[0,2147483647]};
    for (key in bounds) {
        value = form.elements[key].value;
        if (!/^\d{1,10}$/.test(value) || Number(value) < bounds[key][0] || Number(value) > bounds[key][1])
            return validationError('数值超出允许范围。',form.elements[key]);
    }
    return true;
}
function applyRule() {
    if (busy || !serviceState.can_control || !validForm()) return;
    busy = true; ++statusGeneration; clearTimeout(pollTimer); renderStatus(); showLoading();
    document.form.action_mode.value = ' Restart ';
    document.form.submit();
}
function quarkdrive_apply_error(message) {
    busy = false; hideLoading(); renderStatus(); refreshStatus();
    validationError('配置未保存：' + message);
}
function done_validating() { refreshpage(); }
</script>
<style>
.quark_panel table th { width:32%; vertical-align:middle; }
.quark_panel .help-block { margin-bottom:0; }
#quark_log_text { height:340px; overflow:auto; white-space:pre-wrap; word-break:break-all; }
</style>
</head>
<body onload="initial();" onunload="clearTimeout(pollTimer); clearTimeout(logTimer); return unload_body();">
<div class="wrapper">
<div class="container-fluid" style="padding-right:0"><div class="row-fluid">
<div class="span3"><center><div id="logo"></div></center></div><div class="span9"><div id="TopBanner"></div></div>
</div></div>
<div id="Loading" class="popup_bg"></div>
<iframe name="hidden_frame" id="hidden_frame" src="" width="0" height="0" frameborder="0"></iframe>
<form method="post" name="form" action="/start_apply.htm" target="hidden_frame" autocomplete="off">
<input type="hidden" name="current_page" value="/Advanced_quarkdrive.asp">
<input type="hidden" name="next_page" value=""><input type="hidden" name="next_host" value="">
<input type="hidden" name="sid_list" value="QUARKDRIVE;"><input type="hidden" name="group_id" value="">
<input type="hidden" name="action_mode" value=""><input type="hidden" name="action_script" value="">
<div class="container-fluid"><div class="row-fluid">
<div class="span3"><div class="well sidebar-nav side_nav" style="padding:0">
<ul id="mainMenu" class="clearfix"></ul><ul class="clearfix"><li><div id="subMenu" class="accordion"></div></li></ul>
</div></div>
<div class="span9"><div class="box well grad_colour_dark_blue">
<h2 class="box_head round_top">夸克网盘 WebDAV</h2><div class="round_bottom">
<div id="tabMenu" class="submenuBlock"></div>
<div class="alert alert-info" style="margin:10px">
将夸克网盘作为 WebDAV 访问，根目录指网盘中的目录。填写浏览器请求头中的完整 Cookie，失效后重新获取。<br>
WebDAV 地址为 http://路由器地址:端口/；用户名和密码同时留空时无需认证。
</div>
<table class="table"><tr><th style="width:32%">运行状态</th><td><span id="quark_status" class="label">读取中</span></td></tr>
<tr><th>EIP93 TLS 加速</th><td id="quark_acceleration">—</td></tr>
<tr><th>临时服务操作</th><td>
<button id="quark_start" type="button" class="btn btn-success" onclick="serviceAction('start')">启动</button>
<button id="quark_stop" type="button" class="btn" onclick="serviceAction('stop')">停止</button>
<button id="quark_restart" type="button" class="btn btn-info" onclick="serviceAction('restart')">重启</button>
<span id="quark_feedback" role="status"></span>
<span class="help-block">使用已保存的配置，不改变启用开关。运行中仅表示进程存在。</span></td></tr></table>
<ul class="nav nav-tabs" id="quark_tabs">
<li class="active"><a href="#quark_basic">基本设置</a></li><li><a href="#quark_advanced">高级设置</a></li><li><a href="#quark_log">运行日志</a></li>
</ul>
<div id="quark_basic" class="quark_panel"><table class="table">
<tr><th>启用并开机启动</th><td>
<div class="main_itoggle"><div id="quarkdrive_enable_on_of"><input type="checkbox" id="quarkdrive_enable_fake" <% nvram_match_x("", "quarkdrive_enable", "1", "value=1 checked"); %><% nvram_match_x("", "quarkdrive_enable", "0", "value=0"); %>></div></div>
<div style="position:absolute; margin-left:-10000px">
<input type="radio" name="quarkdrive_enable" id="quarkdrive_enable_1" value="1" <% nvram_match_x("", "quarkdrive_enable", "1", "checked"); %>>
<input type="radio" name="quarkdrive_enable" id="quarkdrive_enable_0" value="0" <% nvram_match_x("", "quarkdrive_enable", "0", "checked"); %>>
</div></td></tr>
<tr><th><label for="qd_cookie">夸克网盘 Cookie</label></th><td><input type="password" name="qd_cookie" id="qd_cookie" maxlength="4095" class="input-xlarge" autocomplete="new-password" value="<% nvram_get_x("", "qd_cookie"); %>"> <button type="button" class="btn btn-mini" onclick="reveal('qd_cookie')">显示 / 隐藏</button><span class="help-block">粘贴完整的 name=value; other=value，不包含 Cookie: 前缀。</span></td></tr>
<tr><th><label for="qd_auth_user">WebDAV 用户名</label></th><td><input type="text" name="qd_auth_user" id="qd_auth_user" maxlength="255" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_auth_user"); %>"></td></tr>
<tr><th><label for="qd_auth_password">WebDAV 密码</label></th><td><input type="password" name="qd_auth_password" id="qd_auth_password" maxlength="255" class="input-xlarge" autocomplete="new-password" value="<% nvram_get_x("", "qd_auth_password"); %>"> <button type="button" class="btn btn-mini" onclick="reveal('qd_auth_password')">显示 / 隐藏</button></td></tr>
<tr><th><label for="qd_host">监听地址</label></th><td><input type="text" name="qd_host" id="qd_host" maxlength="45" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_host"); %>"><span class="help-block">0.0.0.0 表示所有 IPv4 地址，:: 表示 IPv6。</span></td></tr>
<tr><th><label for="qd_port">监听端口</label></th><td><input type="text" name="qd_port" id="qd_port" maxlength="5" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_port"); %>"></td></tr>
<tr><th><label for="qd_root">网盘根目录</label></th><td><input type="text" name="qd_root" id="qd_root" maxlength="1023" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_root"); %>"></td></tr>
<tr><th><label for="qd_binary">程序路径</label></th><td><input type="text" name="qd_binary" id="qd_binary" maxlength="255" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_binary"); %>"><span class="help-block">默认 /usr/bin/quarkdrive-webdav；可指定外部存储中的兼容程序。</span></td></tr>
</table></div>
<div id="quark_advanced" class="quark_panel" style="display:none"><table class="table">
<tr><th><label for="qd_read_only">只读模式</label></th><td><select name="qd_read_only" id="qd_read_only"><option value="0" <% nvram_match_x("", "qd_read_only", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_read_only", "1", "selected"); %>>开启</option></select><span class="help-block">禁止上传、修改和删除。</span></td></tr>
<tr><th><label for="qd_no_trash">永久删除</label></th><td><select name="qd_no_trash" id="qd_no_trash"><option value="0" <% nvram_match_x("", "qd_no_trash", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_no_trash", "1", "selected"); %>>开启</option></select><span class="help-block">开启后删除文件不进入回收站。</span></td></tr>
<tr><th><label for="qd_auto_index">自动生成目录索引</label></th><td><select name="qd_auto_index" id="qd_auto_index"><option value="0" <% nvram_match_x("", "qd_auto_index", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_auto_index", "1", "selected"); %>>开启</option></select></td></tr>
<tr><th><label for="qd_redirect">302 重定向</label></th><td><select name="qd_redirect" id="qd_redirect"><option value="0" <% nvram_match_x("", "qd_redirect", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_redirect", "1", "selected"); %>>开启</option></select><span class="help-block">直连下载由客户端完成，这部分流量不经过路由器后端及其 EIP93 加速。</span></td></tr>
<tr><th><label for="qd_prefer_http_download">优先使用 HTTP 下载</label></th><td><select name="qd_prefer_http_download" id="qd_prefer_http_download"><option value="0" <% nvram_match_x("", "qd_prefer_http_download", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_prefer_http_download", "1", "selected"); %>>开启</option></select><span class="help-block">HTTP 文件流量不使用 TLS 加速。</span></td></tr>
<tr><th><label for="qd_skip_upload_same_size">跳过同名同大小文件上传</label></th><td><select name="qd_skip_upload_same_size" id="qd_skip_upload_same_size"><option value="0" <% nvram_match_x("", "qd_skip_upload_same_size", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_skip_upload_same_size", "1", "selected"); %>>开启</option></select></td></tr>
<tr><th><label for="qd_read_buffer_size">下载缓冲区（字节）</label></th><td><input type="text" name="qd_read_buffer_size" id="qd_read_buffer_size" maxlength="8" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_read_buffer_size"); %>"><span class="help-block">默认 1048576（1 MiB），最大 16777216。并发请求会增加内存使用。</span></td></tr>
<tr><th><label for="qd_upload_buffer_size">上传缓冲区（字节）</label></th><td><input type="text" name="qd_upload_buffer_size" id="qd_upload_buffer_size" maxlength="8" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_upload_buffer_size"); %>"><span class="help-block">默认 1048576（1 MiB），最大 16777216。</span></td></tr>
<tr><th><label for="qd_cache_size">目录缓存条目数</label></th><td><input type="text" name="qd_cache_size" id="qd_cache_size" maxlength="6" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_cache_size"); %>"></td></tr>
<tr><th><label for="qd_cache_ttl">目录缓存有效期（秒）</label></th><td><input type="text" name="qd_cache_ttl" id="qd_cache_ttl" maxlength="10" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_cache_ttl"); %>"></td></tr>
<tr><th><label for="qd_refresh_cache_secs_interval">缓存刷新间隔（秒）</label></th><td><input type="text" name="qd_refresh_cache_secs_interval" id="qd_refresh_cache_secs_interval" maxlength="10" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_refresh_cache_secs_interval"); %>"><span class="help-block">必须大于 0。</span></td></tr>
<tr><th><label for="qd_upload_wait_timeout">上传等待时间（秒）</label></th><td><input type="text" name="qd_upload_wait_timeout" id="qd_upload_wait_timeout" maxlength="10" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_upload_wait_timeout"); %>"><span class="help-block">默认 280；设为 0 表示一直等待完成。</span></td></tr>
<tr><th><label for="qd_strip_prefix">URL 前缀</label></th><td><input type="text" name="qd_strip_prefix" id="qd_strip_prefix" maxlength="1023" class="input-xlarge" autocomplete="off" value="<% nvram_get_x("", "qd_strip_prefix"); %>"><span class="help-block">反向代理使用的路径前缀，留空表示不设置。</span></td></tr>
<tr><th><label for="qd_debug">调试日志</label></th><td><select name="qd_debug" id="qd_debug"><option value="0" <% nvram_match_x("", "qd_debug", "0", "selected"); %>>关闭</option><option value="1" <% nvram_match_x("", "qd_debug", "1", "selected"); %>>开启</option></select><span class="help-block">排查完成后关闭，日志可能包含网盘请求信息。</span></td></tr>
</table></div>
<div id="quark_log" class="quark_panel" style="display:none">
<p style="margin:10px">最近的服务系统日志，每 5 秒刷新。</p><pre id="quark_log_text">读取中</pre>
<button class="btn" type="button" onclick="refreshLog()">刷新日志</button>
</div>
<div style="text-align:center; padding:20px">
<button type="button" class="btn btn-primary" id="quark_apply" style="width:219px" onclick="applyRule()">保存并应用</button>
<p class="help-block">启用时启动或重启服务，关闭时停止服务。</p>
</div>
</div></div></div></div></div>
</form><div id="footer"></div></div>
</body></html>
