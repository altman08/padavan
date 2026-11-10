# Xray 精简构建

默认编译 VLESS 客户端精简版。沿用 Xray 26.11.9 和现有 MT7621 EIP93
overlay，不修改前端或 `trunk/user/shadowsocks`。

## 构建选项

在产品配置 `trunk/configs/templates/<型号>.config` 中设置：

```text
CONFIG_FIRMWARE_INCLUDE_SHADOWSOCKS=y
CONFIG_FIRMWARE_INCLUDE_XRAY=y
CONFIG_FIRMWARE_INCLUDE_XRAY_LITE=y
```

`CONFIG_FIRMWARE_INCLUDE_XRAY_LITE` 未设置时默认为 `y`；设为 `n` 恢复完整
Xray。也可以通过 make 命令行覆盖，例如：

```sh
make RM2100 CONFIG_FIRMWARE_INCLUDE_XRAY_LITE=n
```

现有自定义构建工作流的 Plugin JSON 支持 `"xray_lite":"y"` 或
`"xray_lite":"n"`。切换选项会自动重新编译，安装路径仍为 `/usr/bin/xray`。
`SHADOWSOCKS` 开关仍用于现有代理管理功能；本次只精简 Xray，不删除独立
SS/SSR、V2Ray、Trojan 程序。

## 保留与裁剪

- 保留 VLESS 出站、VLESS 反向代理、RAW/TCP、TLS、REALITY、Vision、UDP/XUDP。
- 保留 SOCKS TCP/UDP 入站及 HTTP 回退、dokodemo-door/tunnel 入站、
  REDIRECT/TPROXY、sockopt、freedom/blackhole、路由、日志、policy。
- 保留 JSON 配置、`run`、`run -test`、`run -dump`、`version` 和系统 DNS 解析。
- 排除 VMess、Trojan、Shadowsocks、WireGuard、Hysteria、MASQUE 等其他代理协议；
  排除 WS、gRPC、mKCP、HTTPUpgrade、XHTTP、XDrive 等其他传输。
- 排除 VLESS 服务端、API、metrics、自定义内置 DNS、FakeDNS、观测、Lua 路由
  脚本、geodata 自动更新、ECH、YAML/TOML 和额外 CLI 子命令。

前端仍显示原有协议与传输选项，订阅也可能包含被裁掉的节点。只有保留范围内的
配置可使用精简 Xray；被裁功能会报错，需要时切回完整版。原有 v2ray/xray
回退逻辑没有修改，精简 Xray 无法承担 VMess 回退。

## 来源与维护

`patches/193-optional-passwall-lite.patch` 原样复制自参考工程：

```text
/docker/root/projects/ponwrt/feed/immortalwrt-packages/net/xray-core/patches/193-optional-passwall-lite.patch
```

该补丁通过 `xray_lite` Go 构建标签选择精简实现；不加标签时保留完整实现。
另附 `194-preserve-xdrive-validation-order.patch`，恢复 XDrive 校验先于通用
加密检查的顺序，修复原参考补丁导致的上游完整版配置测试失败。
不复制参考工程的 ARM64 EIP93 补丁，继续使用本项目的 MIPSLE overlay。
升级 Xray 时需检查补丁应用、精简/完整构建和 EIP93 overlay 兼容性。

补丁变更会自动重新解包应用；构建输出仅放在包的构建目录。
补丁自带配置测试和 `tools/lite/smoke.py` 本地 REALITY/Vision 互通检查，
可复用这些测试，不需要改写转发或加密实现。

## 本次验证

2026-11-10，Go 1.27.2、Bootlin MIPS musl GCC 9.3.0，使用本包 Makefile
和现有 MT7621 EIP93 overlay，保持 `CGO_ENABLED=1` 及原编译参数：

| MIPSLE ELF（未压缩） | 字节 | MiB |
| --- | ---: | ---: |
| 完整版 | 34,730,680 | 33.12 |
| 精简版 | 18,016,408 | 17.18 |
| 减少 | 16,714,272 | 15.94（48.13%） |

- 原始 26.11.9 源码包经本包 Makefile 解包、依次应用两个补丁并构建成功。
- 验证默认精简、`n` 完整、`y` 精简的往返切换，以及同一选项不重复编译。
- 完整/精简 `infra/conf`、`infra/conf/serial` 短测试通过；精简 SOCKS 和
  指定路由用例通过；现有 EIP93 overlay 的 5 项测试通过。
- 当前 Lua 生成配置的完整版 9 项测试、精简版适用的 5 项测试和 UI 检查通过。
- 主机精简客户端与完整服务端的 REALITY/Vision TCP、TLS、UDP/XUDP、HTTP
  回退、VLESS 反向代理互通及 REDIRECT/TPROXY 监听检查通过。
- 精简依赖列表确认排除了 Shadowsocks、VMess、Trojan、WireGuard、gRPC、
  XHTTP、WebSocket、Go Lua 和自定义 DNS 实现；ROMFS 安装文件与构建产物一致。

未编译整个固件、未在 MT7621 真机运行；主机互通测试不能代替硬件加速、吞吐、
内存及真实防火墙转发验证。固件压缩后的体积变化需以实际镜像为准。
