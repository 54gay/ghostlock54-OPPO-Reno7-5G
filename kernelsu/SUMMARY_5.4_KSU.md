# OPPO Reno7 5G（PFJM10）KernelSU 3.3.1 移植记录

本目录是 KernelSU v3.3.1 针对本机内核 `5.4.254-qgki-gaa96eae94d32` 的移植，
以运行时内核模块（late-load）方式加载，不改动 boot.img / 任何分区。

## 交付物

| 文件 | 说明 |
|---|---|
| `kernelsu.ko` | 预编译模块（KSU_VERSION 32680，uapi 5） |
| `load_ksu_54.sh` | 启动/加载脚本（系统 sh；结尾自动重启 Manager） |
| `SUMMARY_5.4_KSU.md` | 本文件，移植全过程与踩坑记录 |
| `src/` | 模块源码（派生自 KernelSU，GPL-2.0） |
| `build.sh` | 交叉编译脚本（需要配套内核源码树） |

## 每次开机后的加载流程

```sh
# 1) 获得 root shell（ghostlock，或用 su 拿 ksu 域 shell）
# 2) 加载模块
sh /storage/emulated/0/Download/load_ksu_54.sh
```

脚本行为：
1. 检查 `/data/local/tmp/kernelsu.ko`，不存在则提示放置位置并退出（exit 1）
2. 模块已加载 → 直接退出（不卸载、不杀进程，exit 0）
3. 否则在 `kptr_restrict=1`（普通用户读不到 /proc/kallsyms、root 可读）下解析
   `kallsyms_lookup_name` 地址，传给模块的 `kln=` 参数后 `insmod`
4. 最后强制停止并重新启动 KernelSU Manager（`am force-stop` + `am start`），
   让 Manager 拿到新模块的驱动 fd

模块是运行时加载，**重启后需重新加载**。

## 已验证功能

- 模块加载后：`ksu: applied kernelsu rules on 5.4`（sepolicy 注入成功）
- Manager 识别：`Crowning manager: uid=10264`（官方 3.3.1 APK 签名校验通过）
- Manager 获取 root：ksud 拿 fd → ioctl `GRANT_ROOT`(0x4b01) → escape → 执行 sh（uid 0）
- 已授权 app 终端里 `su` → `uid=0(root) context=u:r:ksu:s0`
- su 后端使用 **Manager APK 目录下的 `lib/arm64/libksud.so`**（不再依赖 /data/adb/ksud）
- 默认 `setenforce 1`（强制模式）；可用 `permissive=1` 参数保持当前模式
- 直钩 `sys_call_table`：execve(221)/execveat(281)/newfstatat(79)/faccessat(48)/setresuid(147)

## 5.4 兼容实现要点

1. **符号解析**：vendor 内核未导出 `kallsyms_lookup_name` 等符号。加载时通过
   `kln=` 模块参数传入该函数地址（root 读 /proc/kallsyms 可得），模块内用
   内联 asm `blr` 蹦床调用（同时绕过 KernelCFI 的类型检查）。
2. **栈保护**：`-mstack-protector-guard=sysreg -reg=sp_el0 -offset=1624`
   （offset 来自 kheaders 的 asm-offsets.h），否则 TLS 读取会 panic。
3. **直钩 syscall 表**：5.4 上 tracepoint + ni-slot 分发方案会导致 usap64 崩溃，
   改为直接改写 sys_call_table 条目并保留原表快照（`ksu54_orig_table`），
   内部"调用原始 handler"时走快照，避免递归。
4. **39-bit VA**：页表遍历不处理内核镜像的 PGD 块映射，内核镜像地址改用
   `__pa_symbol()` 取物理地址后走 fixmap 写。
5. **rmmod 崩溃**：`free_module()` 在 sysfs kobject 已删除时崩溃，5.4 跳过
   `kobject_del(&THIS_MODULE->mkobj.kobj)`。
6. **ksud 路径**：加载时由 throne tracker 拿到 Manager base.apk 路径，
   推导 `<apk目录>/lib/arm64/libksud.so` 作为 su 后端；找不到则回退 /data/adb/ksud。
7. **kptr_restrict**：加载脚本临时置 1 解析地址（普通用户不可读、root 可读），
   若读到空值才临时降 0 再读，读完恢复 1。

## sepolicy 注入（生成 u:r:ksu:s0 域）

vendor 策略里没有 `ksu` 域。移植实现了 5.4 原生策略加载
（`selinux/rules54.c` + `selinux/ksu54_secops.c`）：

1. `security_read_policy` 读取当前策略
2. `policydb_read` 反序列化为 policydb
3. 应用 KSU 规则（`ksu_type`/`ksu_allow`/`ksu_typeattribute` 等），
   包括 `ksu`/`ksu_file` 类型、allow-all、以及 servicemanager/logd/system_server 等规则
4. `policydb_write` 序列化
5. `security_load_policy` 加载（内核自行完成 sidtab 转换、策略交换、AVC 重置与通知）

其中约 9 个策略内部函数（`hashtab_insert/search`、`avtab_*`、`ebitmap_*`、
`policydb_read/write`、`security_load_policy` 等）未导出，全部通过 `kln` + `blr`
解析调用；`sepolicy.c` 针对 5.4 的 filename_trans hashtab 布局做了适配。

## ColorOS 隐藏内核反提权（oplus_root_check）绕过

ColorOS 在 kernel 里内置了提权检测，检测到 app uid → 0 会直接杀进程
（这就是此前 Manager root "静默失败"的真正原因）：

- `oplus_root_check_succ`：格式化 payload、printk、kevent 上报（仅上报）
- `oplus_invoke_syscall`（替换 invoke_syscall 的 vendor 函数）：调用上报后
  `mov w0,#9; bl do_group_exit` 杀掉整个线程组

修复（`ksu54_patch_vendor_root_check()`，模块 init 时执行）：
1. 把 `oplus_root_check_succ` 入口补丁为 `ret`
2. 在 `oplus_invoke_syscall` 中定位 `mov w0,#9 + bl` 指令对，替换为两条 NOP

两处均通过 `ksu_patch_text`（fixmap + stop_machine）写入，并有回读校验。

## sucompat 修复：终端里 `su` 显示 not found

现象：程序直接 exec `/system/bin/su` 能被重定向，但在终端（mksh/bash）里
输入 `su` 提示 "inaccessible or not found"。

原因：shell 先用 `newfstatat("/system/bin/su")` 检查命令是否存在，sucompat
的 stat/faccessat 钩子需要读路径，而 5.4 兼容层 `strncpy_from_user_nofault`
调 `strncpy_from_unsafe_user` 时**参数顺序传反了**（内核原型是
`(char *dst, const void __user *, long)`），读路径失败导致重定向从未发生。

修复：参数改为 `(dst, uaddr, count)`。修复后 stat → faccessat → execve
全链路都会重定向到 ksud，终端里 `su` 正常。

## SELinux 模式

- 模块默认在 init 末尾执行 `setenforce(1)`（强制模式）
- 若需要保持当前模式（例如 exploit 破坏过 SELinux 状态），加载时加：
  `insmod kernelsu.ko kln=0x... level=3 permissive=1`
- 注意：SELinux 强制模式下，**ghostlock 直接给出的 u:r:kernel:s0 shell
  不能执行 /system/bin 下的命令**（会被拒绝并终止）。解决办法：
  1. 用 KernelSU 的 `su` 拿 `u:r:ksu:s0` shell 再操作
  2. 或在 exploit 里 `setcon("u:r:shell:s0")`（本项目已加回）
  3. 或临时 `runcon u:r:shell:s0 sh 脚本`（临时 shell 域）
  4. 或在加载时用 `permissive=1`

## 危险操作记录（勿踩）

- **不要用 rmmod 热换模块**：直钩的 syscall 表项在卸载窗口内被并发进程命中会
  触发内核恐慌（曾实测 panic：`in_group_p` 访问已释放的模块内存）。换版本请
  重启后在干净状态加载。
- 恢复系统正常状态（enforcing、清理内核残留）只能重启。

## 已知问题 / 注意

- 模块无法卸载（rmmod 有竞态风险），重启即消失
- 不改任何分区 / boot.img（BL 锁状态下安全）
- ColorOS 弹出"安全警告"时不要点"立即重启"
- 首次使用前需在 Manager 里给需要的 app 授权
