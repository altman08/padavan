# MT7621 Xray EIP93 加速

参考 `/docker/root/projects/ponwrt/feed/immortalwrt-packages/net/xray-core` 的
`190-an7581-eip93-aes-gcm.patch` 和 `191-an7581-eip93-aes-cbc.patch`，
适配本项目的 MIPS little-endian / Linux 4.4。
复用上游方案的 Xray 私有 Go overlay、每个密钥首次使用时自检和软件回退机制；
模板保留原作者与 BSD-3-Clause 声明。

## 启用

MT7621 固件启用 `CONFIG_FIRMWARE_INCLUDE_XRAY=y` 时默认编入加速后端。
需 Go 1.27.x、Python 3 和目标 C 交叉编译器；现有 CI 使用 Go 1.27.x。
Xray 保留原来的 `CGO_ENABLED=1` 和交叉编译器设置；overlay 不会改写共享的 GOROOT。

之前移植的内核驱动由 `trunk/configure` 默认启用，并同时启用
`CRYPTO_GCM`、`CRYPTO_CTR`、`CRYPTO_GHASH` 和 AF_ALG AEAD/skcipher 接口；
EIP93 驱动的 Kconfig 同时选中 `CRYPTO_CBC`。
须使用包含该驱动的新内核；仅替换 Xray 无法给旧内核增加 EIP93 支持。

加速只在运行时确认以下条件后尝试：

- Linux/mipsle，根设备树 compatible 包含完整的 `mediatek,mt7621-soc` 字符串。
- 非 FIPS、非 purego 构建，未设置 `XRAY_EIP93=off`。

AES-GCM 还需满足：

- AES-GCM 使用 12 字节 nonce、16 字节标签，载荷 4096–32768 字节，AAD 不超过 256 字节。
- 成功绑定 `gcm_base(ctr(aes-eip93),ghash-generic)`，并通过当前密钥的加密、解密及坏标签自检。

EIP93 执行 AES-CTR，GHASH 在 CPU 上执行。这是混合 AES-GCM 加速，适用于使用
Go 标准库 AES-GCM 的 Xray 协议和 TLS 流量；不加速 ChaCha20-Poly1305 等其他算法。
Linux 4.4 使用旧 AEAD 缓冲区布局，后端为加密输入补足标签空间，并裁掉解密输出
保留的标签空间；不会把此布局直接套用于新版内核。

首次成功启用会向 Xray 标准错误输出写入：

```text
Xray EIP93: MT7621 AES-GCM hybrid backend enabled (AF_ALG CTR + CPU GHASH)
```

GCM 同一密钥的操作串行执行，最多保留 32 个操作 socket，并设置收发超时。
不符合条件的请求继续使用 Go 实现；连接失败或操作错误会关闭并停用该密钥的后端，
当前请求回退到 Go。认证失败仍返回认证错误，不会返回未认证的明文。
socket 超时不能保证中止正在执行的内核加密；本树 EIP93 驱动另外提供硬件请求超时处理。

AES-CBC 支持 128/192/256 位密钥，单次 `CryptBlocks` 为 4096–32768 字节，
通过 AF_ALG skcipher 显式绑定 `cbc(aes-eip93)`。较小或较大的请求使用 Go 实现，
软件与硬件路径之间保留同一条 CBC IV 链，支持原地加解密及 `SetIV`。
仅对实际选用 AES-CBC 的代码路径生效，不会改变 Xray/TLS 算法协商。

每个 CBC 加密器/解密器在首次符合长度的请求时创建独立操作 socket，
用当前密钥及对应方向对照 Go 自检，通过后才处理业务数据。
CBC 另设 32 个 socket 的上限及收发超时，不与 GCM 共用配额。
操作失败时保持调用方输入和 IV，关闭并停用该实例的硬件后端，由 Go 完成当前请求；
临时输出在操作后清零。单个 CBC 实例与 Go 原实现一样，不能并发使用。

首次成功启用 CBC 时输出：

```text
Xray EIP93: MT7621 AES-CBC backend enabled (AF_ALG)
```

## 关闭

启动 Xray 进程前设置环境变量 `XRAY_EIP93=off`，重启进程后生效。
构建时在产品配置加入 `CONFIG_FIRMWARE_INCLUDE_EIP93=n`，可同时关闭内核驱动
和 Xray overlay。Makefile 追踪产品/内核配置及 overlay 文件的变化以重新编译。

## 验证

已使用 Go 1.27.2 完成：

- 通过本包 Makefile 以 `CGO_ENABLED=1` 交叉构建 MIPS little-endian Xray 26.10.21。
- AES（含 CBC）/GCM/Cipher 真机测试程序交叉编译通过。
- 主机通过明确指定的 CPU 算法对照 AES-128/192/256 的 AF_ALG 加解密，检查原地操作、
  坏标签拒绝、长度边界、并发及连接失败回退；竞态检查通过。
- Go Cipher 短回归、Xray `common/crypto` 短测试通过；overlay 的隔离和拒绝不兼容
  Go 版本/源码的检查通过。
- CBC 主机 AF_ALG 对照及竞态检查通过，覆盖 AES-128/192/256、连续 IV 链、
  长度边界、原地与独立缓冲区、`SetIV`、错误密钥自检拒绝、socket 失败与配额回退、
  临时输出清理及失败连接的配额回收。

上述主机测试不等同于 MT7621 硬件验证。尚未在 MT7621 真机运行本 Xray 构建，
也未测量代理互通、长期稳定性或吞吐；不能据此声称已经提速。

在构建主机生成测试程序（Go 1.27.x）：

```sh
python3 trunk/user/xray/eip93/overlay.py --go go --out /tmp/xray-eip93-overlay
GOOS=linux GOARCH=mipsle GOMIPS=softfloat CGO_ENABLED=0 go test \
  -overlay=/tmp/xray-eip93-overlay/overlay.json -c -o gcm-mipsle.test \
  crypto/internal/fips140/aes/gcm
GOOS=linux GOARCH=mipsle GOMIPS=softfloat CGO_ENABLED=0 go test \
  -overlay=/tmp/xray-eip93-overlay/overlay.json -c -o aes-mipsle.test \
  crypto/internal/fips140/aes
GOOS=linux GOARCH=mipsle GOMIPS=softfloat CGO_ENABLED=0 go test \
  -overlay=/tmp/xray-eip93-overlay/overlay.json -c -o cipher-mipsle.test crypto/cipher
```

在包含新内核的 MT7621 路由器运行 `./gcm-mipsle.test -test.run TestEIP93Hardware -test.v`，
以及 `./aes-mipsle.test -test.run TestEIP93CBC -test.v`，
确认硬件用例实际通过（未跳过）且 `/proc/interrupts` 中 EIP93 计数增加。再运行
`./cipher-mipsle.test -test.short`，以及设置 `XRAY_EIP93=off` 后的同一回归。
Cipher 测试含同时持有约 96 MiB 数据的用例，128 MiB 路由器需预留足够内存，
测试程序宜放在外部存储以减少 tmpfs 占用。

最后使用真实 Xray AES-GCM/AES-CBC 流量，比较启用/关闭时的结果哈希、CPU 使用率和吞吐，
同时核对 EIP93 中断计数。仅出现启用日志不足以证明性能改善。

主机开发检查命令：

```sh
GO=go python3 trunk/user/xray/eip93/test_overlay.py
go test -overlay=/tmp/xray-eip93-overlay/overlay.json -tags=purego,eip93test \
  -race -run TestEIP93 -v crypto/internal/fips140/aes crypto/internal/fips140/aes/gcm
```

`eip93test` 仅用于主机测试，生产固件不使用该标签。
