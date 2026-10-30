# KernelSU 3.3.1 port for OPPO Reno7 5G (PFJM10)

面向本机（OPPO Reno7 5G / OP5227，内核 `5.4.254-qgki-gaa96eae94d32`）的 KernelSU
内核模块移植，配合本仓库的 GhostLock54 使用：

1. 先用 `ghostlock54` 拿到 uid=0 的 `#` shell（SELinux 已置 Permissive）
2. 在这个 root shell 里执行 `sh load_ksu_54.sh` 加载 `kernelsu.ko`
3. 打开 KernelSU Manager，给需要的 app 授权；之后任意已授权 app 的终端里
   `su` 都能直接拿到 `u:r:ksu:s0` 的 root shell

模块是**运行时加载（late-load）**的：不动 boot.img、不动任何分区，重启后需要
在领到 root 后重新跑一次 `load_ksu_54.sh`。

## 文件

| 文件 | 说明 |
|---|---|
| `kernelsu.ko` | 预编译模块（KernelSU v3.3.1 端口，KSU_VERSION 32680，uapi 5） |
| `load_ksu_54.sh` | 加载脚本（系统 sh；已加载则直接退出） |
| `SUMMARY_5.4_KSU.md` | 完整技术移植记录：所有踩过的坑与修复 |
| `src/` | 模块源码（KernelSU GPL-2.0 端口） |
| `build.sh` | 交叉编译脚本（需要配套的内核源码树） |

## 用法

设备端（把整个 `kernelsu/` 目录推到 `/data/local/tmp/kernelsu/` 即可）：

```sh
# 在 ghostlock54 弹出的 # shell 里
sh /data/local/tmp/kernelsu/load_ksu_54.sh
# 然后打开 KernelSU Manager（me.weishu.kernelsu）
```

脚本行为：
- 找不到 ko 时提示放置路径并退出
- 模块已加载时直接退出（不卸载、不杀进程）
- 否则在 `kptr_restrict=1`（普通用户读不到 kallsyms、root 可读）下解析
  `kallsyms_lookup_name` 地址，通过 `kln=` 参数传给模块后 `insmod`

SELinux 模式：
- 默认 `enforce`（加载成功后 setenforce 1）
- 如果某种状态下强制模式导致系统异常，可用 `permissive=1` 参数加载：
  `insmod kernelsu.ko kln=0x... level=3 permissive=1`（保持当前 SELinux 模式）
- 注意：ghostlock 若破坏过 SELinux 状态（旧版本 exploit），强制模式可能让系统
  服务大面积被拒，此时请用 `permissive=1` 或重启后重新加载

## 这套移植解决了什么（详见 SUMMARY_5.4_KSU.md）

- 5.4 vendor 内核没有导出 `kallsyms_lookup_name`、`security_load_policy`、
  `policydb_read/write`、`avtab_*`、`hashtab_*` 等，全部用 kallsyms 地址 +
  内联 asm `blr` 蹦床解析调用（同时绕过 KernelCFI）
- 直钩 `sys_call_table`（execve/execveat/newfstatat/faccessat/setresuid），规避
  5.4 上 tracepoint 方案对 usap64 的崩溃
- 修复了 `strncpy_from_user_nofault` 参数顺序 bug（否则终端里 `su` 永远
  “not found”）
- 5.4 原生 sepolicy 注入：`security_read_policy → policydb_read → 加 ksu 规则 →
  policydb_write → security_load_policy`，真正生成 `u:r:ksu:s0` 域
- 绕开 ColorOS 隐藏的内核反提权（`oplus_root_check_succ` 上报 +
  `oplus_invoke_syscall` 里 `do_group_exit` 杀进程），补丁点为 `ret` / NOP
- 跳过 KSU 的 `setenforce(true)`（本机 SELinux 已被 exploit 破坏，强制会断网）

## 许可

模块源码派生自 [KernelSU](https://github.com/tiann/KernelSU)（GPL-2.0），
`src/` 下各文件保留上游 SPDX 头。按 GPL-2.0 分发。
