# 面向 OPPO Reno7 5G（PFJM10）的 KernelSU 3.3.1 移植

本目录是 KernelSU v3.3.1 针对本机内核 `5.4.254-qgki-gaa96eae94d32` 的移植，
以运行时内核模块（late-load）加载，不改动 boot.img / 任何分区。

配合 GhostLock54 使用：

1. 运行 `ghostlock54` 拿到 `#` shell（SELinux 已按需切换）
2. 在 root shell 里执行：

   ```sh
   sh /data/local/tmp/kernelsu/load_ksu_54.sh
   # 或（如果在设备本地放了 ko）
   sh /data/local/tmp/load_ksu_54.sh
   ```

3. 脚本会自动重启 KernelSU Manager；给需要的 app 授权后，任意已授权 app 的
   终端里 `su` 即可得到 `u:r:ksu:s0` 的 root shell

模块是**运行时加载**，重启后需要重新跑一次加载脚本。

## 文件

| 文件 | 说明 |
|---|---|
| `kernelsu.ko` | 预编译模块（KSU_VERSION 32680，uapi 5） |
| `load_ksu_54.sh` | 加载脚本（系统 sh；结尾自动重启 Manager） |
| `SUMMARY_5.4_KSU.md` | 完整移植记录（所有坑与修复，中文） |
| `src/` | 模块源码（派生自 KernelSU，GPL-2.0） |
| `build.sh` | 交叉编译脚本（需要配套内核源码树） |

## 加载脚本行为

1. 检查 `/data/local/tmp/kernelsu.ko`，不存在则提示放置位置并退出
2. 模块已加载 → 直接退出（不卸载、不杀进程）
3. 否则在 `kptr_restrict=1`（普通用户读不到 kallsyms、root 可读）下解析
   `kallsyms_lookup_name` 地址，通过 `kln=` 参数传给模块后 `insmod`
4. 强制停止并重新启动 KernelSU Manager（`am force-stop` + `am start`），
   让 Manager 加载新模块的驱动

## SELinux 模式

- 默认 `enforce`（加载成功后 `setenforce 1`）
- 需要保持当前模式时（如 exploit 破坏过 SELinux 状态、或不想强制）：

  ```sh
  /system/bin/insmod /data/local/tmp/kernelsu.ko kln=0x... level=3 permissive=1
  ```

- 强制模式下，ghostlock 直接给出的 `u:r:kernel:s0` shell 不能执行
  `/system/bin` 下的命令（会被拒绝终止）。解决：用 `su` 拿 ksu 域 shell、
  或在 exploit 里 `setcon("u:r:shell:s0")`、或 `runcon u:r:shell:s0 sh 脚本`、
  或 `permissive=1` 加载。

## 关键实现（详见 SUMMARY_5.4_KSU.md）

- vendor 内核未导出 `kallsyms_lookup_name` / `security_load_policy` /
  `policydb_*` / `avtab_*` / `hashtab_*` 等 → 全部用 kln 地址 + 内联 asm
  `blr` 蹦床调用（同时绕过 KernelCFI）
- 直钩 `sys_call_table`（execve/execveat/newfstatat/faccessat/setresuid），
  规避 5.4 上 tracepoint 方案对 usap64 的崩溃
- 修复 `strncpy_from_user_nofault` 参数顺序（否则终端里 `su` 永远 not found）
- 5.4 原生 sepolicy 注入（security_read_policy → policydb_read → 加规则 →
  policydb_write → security_load_policy），真正生成 `u:r:ksu:s0` 域
- 绕开 ColorOS 隐藏内核反提权（`oplus_root_check_succ` + `oplus_invoke_syscall`
  的 `do_group_exit` 杀进程），补丁为 `ret` / NOP
- su 后端使用 Manager APK 目录下的 `lib/arm64/libksud.so`

## 许可

模块源码派生自 [KernelSU](https://github.com/tiann/KernelSU)（GPL-2.0），
`src/` 下各文件保留上游 SPDX 头。按 GPL-2.0 分发。
