# MT7621 EIP93 / Linux 4.4 移植

基于 Linux 上游 `drivers/crypto/inside-secure/eip93`，固定来源提交：
`5b0764a53235d3c77e1ed674e836e1b3fe1a4b1f`。
上游文件保留原作者和 GPL-2.0 声明。

来源：<https://github.com/torvalds/linux/tree/5b0764a53235d3c77e1ed674e836e1b3fe1a4b1f/drivers/crypto/inside-secure/eip93>

## 功能范围

- AES-128/192/256：ECB、CBC、CTR、RFC3686 CTR。
- DES、3DES：ECB、CBC。
- MD5、SHA1、SHA224、SHA256，以及各自的 HMAC。
- 16 种 authenc：上述 4 种 HMAC 分别搭配 CBC(AES/DES/3DES) 和 RFC3686 CTR(AES)。
- 根据硬件能力寄存器注册算法；驱动名称包含 `-eip93`，优先级 1500。

EIP93 不提供原生 AES-GCM/GHASH、ChaCha20-Poly1305；本移植没有把这些算法标成原生硬件实现。
ARC4、随机数发生器和 IPsec 协议卸载不在此上游驱动的算法范围内。

AEAD 的硬件路径使用整字标签和 AAD 偏移。非整字标签、非整字或超过
1020 字节的 AAD、空载荷，以及需要超过 64 KiB 连续缓冲区的请求，使用明确指定
`*-generic` 子算法的软件 authenc 回退。硬件解密使用私有输出缓冲区，仅在认证
成功后复制到调用方，认证失败返回 `-EBADMSG`。

## Linux 4.4 适配

- 使用本树的 ablkcipher、ahash、AEAD API，驱动内部兼容宏独立封装。
- MT7621 设备树地址 `0x1e004000`、大小 `0x1000`、GIC 中断 19。
- 32 位 DMA；修正映射方向、失败清理、请求独立 SA、描述符长度和 CTR 计数溢出处理。
- 有序工作队列承担可能睡眠的准备和完成工作，软中断调用方只负责排队。
  这会串行执行硬件请求，吞吐需要真机测量。
- 中断/tasklet 回收描述符，完成回调在 DMA 解除映射后执行。
- 硬件请求和满环等待有超时限制；超时复位并停用引擎，后续请求返回 `-EIO`，
  防止在 DMA 尚未停止时释放内存。模块模式可重载，内建模式需重启。

## 配置

Padavan 的 `trunk/configure` 在 MT7621 上默认内建启用 `CONFIG_CRYPTO_DEV_EIP93=y`，
同时启用 AF_ALG 的 hash/skcipher/AEAD 接口并保留 Crypto API 自检。
产品配置中加入 `CONFIG_FIRMWARE_INCLUDE_EIP93=n` 可关闭。
其他 SoC 不自动启用，Kconfig 也限制为 MT7621 小端平台。

手工编译模块时启用 `CONFIG_CRYPTO_HW=y`、`CONFIG_CRYPTO_DEV_EIP93=m`，模块名为
`crypto-hw-eip93.ko`，设备树匹配会自动加载。

## 验证状态

使用 Bootlin MIPS little-endian GCC 9.3.0、RM2100 配置完成 Linux 4.4.198 的
内建驱动 `vmlinux` 链接及模块编译。构建在独立输出目录完成。
原始板级配置单独构建时另需 `CONFIG_NF_CONNTRACK_EVENTS=y`，以满足本树
FULLCONENAT 的现有依赖；该验证配置没有写回板级模板。

独立 Go AF_ALG 诊断工具位于 `trunk/tools/eip93-check`，用 Go 标准库计算参考值；
诊断工具已通过主机软件算法对照并可交叉编译为静态 MIPS little-endian 程序。
主机软件对照只验证诊断逻辑，不能证明 EIP93 硬件运行正常。

尚未在 MT7621 真机启动、执行内核自检或测量吞吐。刷入包含驱动的完整固件后，
须检查 `dmesg` 中 Crypto API 自检结果、`/proc/crypto` 中的驱动及 `selftest` 状态，
并运行诊断工具，对照 `/proc/interrupts` 中 EIP93 中断计数确实增加。
完整固件打包和刷机不属于已完成的验证。
