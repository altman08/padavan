# Padavan 夸克网盘 WebDAV

基于 `chenqimiao/quarkdrive-webdav` 1.3.9，固定提交
`7a88d7f1fa299a6ef75b99d7e5dbc07a717f579e`。后端及 EIP93 TLS 补丁参考
PonWrt 的 `luci-app-quarkdrive-webdav/net/quarkdrive-webdav`，本目录独立构建，
不依赖另一份工作区路径。现有阿里云盘功能保持独立。

## 编译

在所用的 `trunk/configs/templates/<产品>.config` 中加入：

```text
CONFIG_FIRMWARE_INCLUDE_QUARKDRIVE=y
```

没有设置时默认不编入。使用本项目的 **MIPS 小端 musl** C 工具链，主机先安装：

```sh
rustup toolchain install 1.94.0 --profile minimal --component rust-src
make <产品>
```

MIPS 是 Rust tier-3 目标，没有发行版提供的预编译标准库，`build.sh` 使用固定工具链
和 `rust-src` 构建 std；`RUSTC_BOOTSTRAP=1` 仅作用于此子进程的 `build-std`。
不会修改共享 Rust 工具链源码。Cargo 使用上游 `Cargo.lock`，源压缩包有 SHA256 校验。
首次构建需要下载 Cargo 依赖及标准库依赖。

程序动态依赖 musl 和 `libgcc_s.so.1`，由现有 musl 固件安装规则提供；不支持直接使用
uClibc 工具链构建本包。验证产物约 8.9 MiB，单独 xz 压缩约 2.1 MiB，
实际固件增量取决于文件系统压缩和其他包。请按产品 Flash 容量选择编译功能。

程序和脚本安装为 `/usr/bin/quarkdrive-webdav`、`/usr/bin/quarkdrive-webdav.sh`。
`CONFIG_FIRMWARE_INCLUDE_EIP93=n` 会同时禁止本次构建的后端启用 EIP93；
其他 SoC 也禁用本后端的硬件路径。构建模式变化会使程序重新编译。

## 页面与服务

在管理页侧栏打开“夸克网盘 WebDAV”（`Advanced_quarkdrive.asp`）。界面使用
Shadowsocks 同类的 Padavan Bootstrap 布局，包含基本设置、高级设置和运行日志。

- 默认关闭，监听 `0.0.0.0:8088`，网盘根目录 `/`。
- Cookie 从浏览器发往夸克网盘的请求头复制完整值，不包含 `Cookie:` 前缀。
  单项 NVRAM 最多 4095 字节；超长或包含换行时，前端与后端均拒绝保存。
- WebDAV 用户名与密码同时填写或同时留空。留空即关闭 WebDAV 身份验证。
- 下载、上传缓冲区各默认 1 MiB，可配置为 1–16777216 字节；并发会额外占用内存。
- 程序路径可指定外部存储中的兼容原生 ELF，必须适配 MIPS/musl 并可执行。
  自定义程序需要支持页面使用的 CLI；没有本补丁时不会上报 EIP93 使用状态。
- 保存并应用遵循启用开关：开启时启动/重启，关闭时停止。开机也遵循此开关。
- 启动、停止、重启按钮是临时操作，使用已保存配置，不修改开机启动开关。
  修改表单后应先保存并应用。
- 自动刷新不会覆盖未保存内容；服务操作期间禁止重复请求，并忽略旧状态响应。
- 运行状态核对 PID 对应的实际可执行程序，仅表示进程存在，不代表 Cookie 有效。

启动脚本通过环境变量 `QUARK_COOKIE`、`WEBDAV_AUTH_USER`、`WEBDAV_AUTH_PASSWORD`
传递凭据，不拼接 shell 命令，不把凭据放进命令行参数。配置仍按 Padavan 机制
保存在 NVRAM 中，环境变量也可被 root 读取。

配置使用 `quarkdrive_enable` 与 `qd_*` 命名，独立注册 `QUARKDRIVE` 配置组和重启事件。
页面应用先完整校验再写入 NVRAM；脚本在重启前检查配置，明显无效的设置保留旧进程。
服务控制沿用 `get_login_safe()` 的可信登录限制。服务按 PID 与可执行路径停止，
不会按宽泛进程名杀死其他程序。

```sh
/usr/bin/quarkdrive-webdav.sh start         # 遵循启用开关
/usr/bin/quarkdrive-webdav.sh restart       # 遵循启用开关，先校验
/usr/bin/quarkdrive-webdav.sh start_now     # 临时启动
/usr/bin/quarkdrive-webdav.sh restart_now   # 临时重启
/usr/bin/quarkdrive-webdav.sh stop          # 停止并等待退出
/usr/bin/quarkdrive-webdav.sh status        # 运行时退出码 0
/usr/bin/quarkdrive-webdav.sh check         # 检查已保存配置
```

后端输出进入系统日志，标签为 `quarkdrive-webdav`，复用系统日志轮转，不写无限增长的
独立日志文件。日志页显示系统日志末尾 32 KiB 中属于本服务的记录，并进行 HTML 转义。
运行文件位于权限为 0700 的 `/tmp/quarkdrive-webdav/`，不作为持久配置。

## MT7621 EIP93

`100-eip93-rustls.patch` 保留 PonWrt 的 TLS 1.2/1.3 AES-GCM CryptoProvider、
会话自检、TLS 记录处理和软件回退机制。
`110-mt7621-afalg.patch` 将设备检测改为 MT7621，并适配 MIPS o32 和 Linux 4.4：

- 只对小端 Linux MIPS、完整匹配 `mediatek,mt7621-soc` 的设备启用。
- 通过 AF_ALG 精确绑定 `gcm_base(ctr(aes-eip93),ghash-generic)`，不需要 cryptodev。
- 控制消息使用 libc 的原生对齐与长度，处理 32 位 `cmsghdr` / `msghdr`。
- 本树 Linux 4.4 的加密输入包含 tag 空间，解密输出也保留该空间；现代内核使用紧凑布局。
- 8–32 KiB 记录才进入加速路径，保持原密码套件顺序，小记录和 ChaCha20 使用软件。
  8 KiB 下限沿用参考实现，尚未按 MT7621 的吞吐/CPU 结果重新调优。
- 最多 32 个内核会话，复用输入输出缓冲区；每个新会话先对照 ring 验证加密、解密和坏标签。
- 认证成功后才复制明文，认证错误直接作为 TLS 失败，普通接口错误回退软件。
- 硬件执行 AES-CTR，GHASH 在内核 CPU 上执行，属于混合 AES-GCM 加速。

沿用本仓库已有的内核 EIP93 驱动和 AF_ALG/GCM 配置，不改变 Shadowsocks/Xray 的加速实现。
EIP93 只加速本进程访问夸克 API、HTTPS 下载与上传的流量。WebDAV 入站仍是 HTTP；
302 直连及 HTTP 下载不经过本后端的 TLS 加速路径。

后端自动判断设备，页面不提供强制硬件开关。直接运行时可以设置
`QUARK_EIP93=auto|afalg|off` 诊断；不能绕过设备检查或编译时禁用。

页面状态来自后端在私有运行目录的报告：

| 状态 | 含义 |
| --- | --- |
| 未启用或设备不支持 | 编译关闭、设备不匹配，或自定义后端没有报告 |
| 等待符合条件的 TLS 数据 | 已注册加速提供者，尚无成功的加速记录 |
| 本次运行已使用 EIP93 | 至少一条真实 TLS 记录通过硬件接口成功处理 |
| 已回退到软件 | 后端不可用，且本次运行尚无成功的硬件记录 |

“本次运行已使用”是历史记录，不保证后续每条连接都在使用硬件。后续软件回退不会抹去
已发生的硬件使用记录。激活状态和日志不能替代吞吐或 CPU 实测。

## 验证

2026-11-10 已完成：

- 固定源码压缩包 SHA256 核对，干净解包、依次应用两份补丁、MIPS32r2 小端
  musl release 编译和 ROMFS 安装。
- QEMU MIPS 小端模拟运行实际安装的程序，验证版本、帮助、服务启动、未认证 HTTP
  请求返回 401，以及非目标设备的 EIP93 自动关闭；未使用真实网盘凭据。
- 11 项 EIP93 测试，包含真实 Linux AF_ALG 软件驱动的 TLS 1.2/1.3 互通、并发、
  坏 tag/AAD/nonce、运行错误回退、缓冲区复用，以及 Linux 4.4 布局与原生控制消息布局。
- 9 项真实原生测试进程的启动脚本测试，覆盖凭据与参数、重复启停、延迟退出、
  错误配置保留旧进程、失败启动清理、外部 PID 保护和残留锁恢复。
- ASP JavaScript 的四种启用/运行组合、权限、重复操作、过期响应、未保存内容和表单校验。
- C 管理接口的服务端校验、凭据字节限制、操作白名单、可信登录限制和事件位互斥检查。
- Chromium 中加载真实 Padavan CSS/JS，以模拟路由器状态验证菜单、页签、凭据显示、
  状态刷新、未保存输入和日志转义；没有使用真实网盘凭据。
- 新 C 接口及相关 httpd/rc/defaults/variables 文件的 MIPS 编译语法检查，
  页面在功能开启/关闭时的实际 ROMFS 安装与裁剪。

事件检查同时修复了原有 `EVM_RESTART_REBOOT` 和 `EVM_BLOCK_UNSAFE` 的 64/65 位
越界移位，改用空闲的 55/56 位；夸克独占 54 位，不复用阿里云盘事件。

本目录测试在项目根运行：

```sh
python3 trunk/user/quarkdrive-webdav/tests/service_test.py
node trunk/user/quarkdrive-webdav/tests/ui_test.js
```

C 测试使用实际 httpd 头文件。准备临时 `ralink_board.h` 指向目标产品的 `board.h` 后：

```sh
cc -std=gnu99 -DAPP_QUARKDRIVE -I<临时头文件目录> \
  -Itrunk/user/shared/src -Itrunk/user/shared/src/include -Itrunk/user/httpd/src \
  trunk/user/quarkdrive-webdav/tests/httpd_test.c -o /tmp/quarkdrive-httpd-test
/tmp/quarkdrive-httpd-test
```

在已应用补丁的后端目录运行：

```sh
cargo test --locked eip93
# 主机必须提供通用 AF_ALG GCM 算法；该测试不允许静默软件回退：
cargo test --locked kernel_afalg_tls_interoperability -- --ignored --nocapture
```

尚未完成整机固件镜像构建、刷机和 MT7621 真机验证。设备上需要核对内核自检、
`/proc/crypto` 驱动、EIP93 中断增量，验证真实 Cookie 的 WebDAV 目录、下载、上传及哈希，
并对比 `auto` / `off` 的吞吐和 CPU。测试用证书和私钥仅用于本地 TLS 互通测试。
