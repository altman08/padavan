local cjson = require "cjson"
local server_section = arg[1]
local proto = arg[2]
local local_port = tonumber(arg[3]) or 0
local socks_port = tonumber(arg[4]) or 0
local ssrindext = assert(io.popen("dbus get ssconf_basic_json_" .. server_section))
local servertmp = ssrindext:read("*all")
ssrindext:close()
local server = cjson.decode(servertmp)

local function nonempty(value, default)
	if value == nil or value == "" then return default end
	return value
end

local function string_list(value, default)
	if type(value) == "table" then return value end
	return {nonempty(value, default)}
end

local transport = nonempty(server.transport, "tcp")
if transport == "tcp" then transport = "raw" end
if transport == "splithttp" then transport = "xhttp" end
if transport == "h2" or transport == "http" or transport == "h3" or transport == "quic" then
	error("Xray 已移除旧 H2/QUIC 传输，请更换节点或在服务端和客户端配置 XHTTP")
end

local tls = tostring(server.tls or "0")
if tls == "1" and tostring(server.insecure or "0") ~= "0" then
	error("Xray 已移除 allowInsecure，请验证服务端证书或配置 pinnedPeerCertSha256")
end
local flow = tostring(server.flow or "0")
flow = (flow == "1") and "xtls-rprx-vision" or
	((flow == "2") and "xtls-rprx-vision-udp443" or "")

local inbounds = {}
if local_port ~= 0 then
	table.insert(inbounds, {
		port = local_port,
		protocol = "dokodemo-door",
		settings = {network = proto, followRedirect = true},
		sniffing = {enabled = true, destOverride = {"http", "tls"}}
	})
end
if proto == "tcp" and socks_port ~= 0 then
	table.insert(inbounds, {
		protocol = "socks",
		port = socks_port,
		settings = {auth = "noauth", udp = true}
	})
end
assert(#inbounds > 0, "Xray 至少需要一个入站端口")

local stream = {
	network = transport,
	security = (tls == "1") and "tls" or ((tls == "2") and "reality" or "none"),
	tlsSettings = (tls == "1") and {
		serverName = nonempty(server.tls_host),
		fingerprint = nonempty(server.fingerprint),
		pinnedPeerCertSha256 = nonempty(server.pinned_peer_cert_sha256)
	} or nil,
	realitySettings = (tls == "2") and {
		serverName = nonempty(server.tls_host, server.server),
		fingerprint = nonempty(server.fingerprint, "chrome"),
		password = nonempty(server.reality_public_key),
		shortId = server.reality_short_id or "",
		spiderX = nonempty(server.reality_spider_x, "/")
	} or nil,
	rawSettings = (transport == "raw" and tls ~= "2") and {
		header = {
			type = nonempty(server.tcp_guise, "none"),
			request = (server.tcp_guise == "http") and {
				path = string_list(server.http_path, "/"),
				headers = nonempty(server.http_host) and {Host = string_list(server.http_host)} or nil
			} or nil
		}
	} or nil,
	kcpSettings = (transport == "kcp" or transport == "mkcp") and {
		mtu = tonumber(server.mtu),
		tti = tonumber(server.tti),
		uplinkCapacity = tonumber(server.uplink_capacity),
		downlinkCapacity = tonumber(server.downlink_capacity),
		congestion = tostring(server.congestion) == "1",
		readBufferSize = tonumber(server.read_buffer_size),
		writeBufferSize = tonumber(server.write_buffer_size),
		header = {type = nonempty(server.kcp_guise, "none")}
	} or nil,
	wsSettings = (transport == "ws" or transport == "websocket") and {
		path = nonempty(server.ws_path, "/"),
		host = nonempty(server.ws_host)
	} or nil,
	grpcSettings = (transport == "grpc") and {
		serviceName = server.grpc_path or ""
	} or nil,
	httpupgradeSettings = (transport == "httpupgrade") and {
		path = nonempty(server.httpupgrade_path, "/"),
		host = nonempty(server.httpupgrade_host)
	} or nil,
	xhttpSettings = (transport == "xhttp") and {
		path = nonempty(server.splithttp_path, "/"),
		host = nonempty(server.splithttp_host),
		mode = nonempty(server.splithttp_mode, "auto")
	} or nil
}
if tls == "2" then
	assert(stream.realitySettings.password, "REALITY 缺少公钥 (pbk)")
end

local xray = {
	log = {loglevel = "warning"},
	inbounds = inbounds,
	outbounds = {
		{
			protocol = "vless",
			settings = {
				address = server.server,
				port = tonumber(server.server_port),
				id = server.vmess_id,
				flow = flow,
				level = tonumber(server.alter_id) or 0,
				encryption = nonempty(server.security, "none")
			},
			streamSettings = stream,
			-- Vision 不支持 TCP Mux；使用节点指定的原生连接。
			mux = {enabled = tostring(server.mux) == "1" and flow == "", concurrency = tonumber(server.concurrency) or 8}
		},
		{protocol = "freedom", tag = "direct"}
	}
}

print(cjson.encode(xray))
