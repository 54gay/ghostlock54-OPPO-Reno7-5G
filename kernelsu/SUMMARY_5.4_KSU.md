# kernelsu.ko for OPPO Reno7 5G (PFJM10), kernel 5.4.254-qgki-gaa96eae94d32

## Deliverable (2026-10-25, working state)
- `/storage/emulated/0/Download/kernelsu.ko` (5.13MB, KernelSU v3.3.1 port, KSU_VERSION 32680, uapi 5)
- `/storage/emulated/0/Download/load_ksu_54.sh` (load script, re-reads /proc/kallsyms each boot)
- `/data/adb/ksud` (3.3.1 userspace, `ksud 3.3.1 (uapi: 5)`)
- source: `work/ksu331port/kernel` (build: `work/buildksu331.sh`, out-of-tree `M=` build against `android_kernel_oppo_sm7325`)
- reference port that predates 3.3.1: `work/ksu-next/kernel`

## Boot/reload procedure (no partition changes; module is runtime-only)
```
# as root (rish):
o=$(cat /proc/sys/kernel/kptr_restrict); echo 0 > /proc/sys/kernel/kptr_restrict
cp /storage/emulated/0/Download/kernelsu.ko /data/local/tmp/ && chmod 644 /data/local/tmp/kernelsu.ko
/system/bin/rmmod kernelsu            # if already loaded; kill manager first if refcnt>0
KLN=$(grep -E " kallsyms_lookup_name$" /proc/kallsyms | head -n1 | cut -d" " -f1)
/system/bin/insmod /data/local/tmp/kernelsu.ko kln=0x$KLN level=3
echo $o > /proc/sys/kernel/kptr_restrict
```
Manager must be (re)opened AFTER the module is loaded so that it re-spawns `libksud.so debug su`.

## Verified working (2026-10-25)
- module loads with exact vermagic, no CFI panic
- `Crowning manager: uid=10264` (track_throne signature check passes on official 3.3.1 APK)
- manager root: `libksud.so debug su` -> ksud gets fd via reboot magic -> ioctl GRANT_ROOT 0x4b01
  (allowed_for_su: is_manager(uid%100000==appid)) -> escape_with_root_profile -> exec sh as uid=0
- probe (mgr_su_probe2, setresuid(10264) then exec libksud.so debug su) prints:
  `uid=0(root) gid=0(root) groups=0(root)` -> manager "获取 Root" succeeds
- `su` in a terminal (mksh `sh -c su`) for any allowlisted uid (MT 10411, Termux 10341)
  -> sucompat access/stat/execve redirect -> root shell; all three hooks verified
- direct sys_call_table hooks for execve(221)/execveat(281)/newfstatat(79)/faccessat(48)/setresuid(147)
- SELinux stays permissive (we skip KSU setenforce(true) on 5.4: enforcing breaks networking on this ROM)
- no usap64 crash storms with direct-hook design

## sucompat on 5.4: strncpy_from_user_nofault arg order bug (FIXED)
Symptom: typing `su` in a terminal (MT Manager / Termux, mksh or bash) gave
"su: inaccessible or not found" even for allowlisted uids, while a program that
execve'd "/system/bin/su" directly got root.

Cause: shells (mksh) resolve commands with `newfstatat("/system/bin/su")` (and
bash/others with `faccessat(X_OK)`) *before* exec'ing.  Those sucompat hooks
redirect the path to /data/adb/ksud, but the 5.4 shim
`ksu_strncpy_from_user_nofault()` called the kernel `strncpy_from_unsafe_user()`
with arguments swapped (kernel prototype is `(char *dst, const void __user *, long)`;
we passed `(uaddr, dst, count)`), so the path was never read correctly and the
redirect never happened.

Fix: pass `(dst, uaddr, count)` (infra/symbol_resolver.c).  After the fix:
`newfstatat("/system/bin/su")` -> stat(/data/adb/ksud) succeeds, mksh execs
"/system/bin/su" -> execve sucompat -> ksud root shell.
Verified for uids 10411 (MT) and 10341 (Termux): `uid=0(root)`.

## CRITICAL: ColorOS hidden kernel anti-root ("oplus_root_check")ColorOS ships a kernel-level escalation detector that SIGKILLs processes whose creds go app_uid -> 0.
It is the reason manager root previously died silently with no userspace error.

Evidence from dmesg (before the fix):
```
oplus_root_check_succ,payload:10264$$old_euid@@10264$$...$$curr_uid@@0$$...$$ppidname@@mgr_su_probe2
[OPLUS_ROOT_CHECK]:Kill the process of escalation...
```

The check has two pieces (found by disassembling the workspace vmlinux.elf, which IS the running
kernel build; symbol offsets match kallsyms):
- `oplus_root_check_succ()` : formats payload, printk, kevent_send_to_user (reporter only, does NOT kill)
- `oplus_invoke_syscall()`  : vendor replacement of invoke_syscall(); calls the reporter, then
  `mov w0,#9; bl do_group_exit` -> kills the whole thread group (SIGKILL observed)

Fix implemented in `ksu54_patch_vendor_root_check()` (ksu54_syms.c, called from core/init.c late-load):
1. resolve `oplus_root_check_succ` via kln, patch entry to `ret` (0xd65f03c0)
2. resolve `oplus_invoke_syscall`, scan first 0x400 bytes for the `mov w0,#9` + BL pair
   (0x52800120 followed by 0x94xxxxxx) and replace both with NOP (0xd503201f)
Both use ksu_patch_text (fixmap + stop_machine), verified by reading back: `now=d65f03c0` / `now=d503201f`.

## Implemented 5.4 compat (all in work/ksu331port/kernel)
1. kln: kallsyms_lookup_name address via module param (not exported; kprobe-free)
2. blr trampoline (inline asm) for calling kernel functions w/o CFI; x0..x9 outputs + clobbers
3. stack canary: -mstack-protector-guard=sysreg -reg=sp_el0 -offset=1624
4. direct table patch instead of tracepoint/x8 dispatcher (tracepoint design crashed usap64)
5. phys_from_virt -> __pa_symbol for kernel-image addrs (39-bit VA PGD block walk bug)
6. ksu54_orig_table snapshot so internal "call original" does not recurse into wrappers
7. skip setenforce(true) on 5.4 (SELinux broken by exploit -> network dies)
8. skip kobject_del at exit (free_module NULL deref on rmmod)
9. 3.3.1 build: selinux/rules.o, sepolicy.o, seccomp_cache.o, feature/selinux_hide.o,
   manager/pkg_observer.o compiled out; `u:r:ksu:s0` does not exist in this policy,
   so "transive domain failed" is expected and non-fatal (escape continues with caps)
10. manager ksud bootstrap: exec of libksud.so -> disable_seccomp only (uid stays 10264 on purpose;
    ksud's own escape rejects euid 0)

## How to start after every reboot (module is runtime-only, no persistence)
1. 用 ghostlock 提权拿 root
2. 在 root shell 里执行: sh /storage/emulated/0/Download/load_ksu_54.sh
   (脚本行为: 若 /data/local/tmp/kernelsu.ko 不存在会提示放置位置;
    若 kernelsu 模块已加载 -> 直接退出(不卸载);
    否则 kptr_restrict=1 下解析 kln -> insmod level=3)
3. 打开 KernelSU Manager（必须在加载之后打开）
4. 已授权 app（MT/Termux 等）的终端里 `su` 即得到 uid=0 的 u:r:ksu:s0 根 shell

Notes:
- kptr_restrict=1 留在系统上：普通用户读不到 /proc/kallsyms，root(CAP_SYSLOG) 可读
  （实测该 vendor 内核 kptr=1 下 root 可正常读地址；若万一读到空/0，脚本会临时降到 0 再读）
- 脚本内的 `am force-stop` 会杀掉 manager，因为 manager 会持有旧模块的驱动 fd
  (refcnt=1) 导致 rmmod 失败；加载完成后再打开 manager 即可
- 模块经 late-load 方式加载，manager 必须在加载后 (重新) 打开

## Policy injection (u:r:ksu:s0 domain) - 2026-10-25
The vendor policy has no `ksu` domain, so escape used to keep the app context.
Implemented a 5.4-native policy loader (selinux/rules54.c + selinux/ksu54_secops.c):
- security_read_policy -> policydb_read -> apply KSU rules (ksu_type/ksu_allow/...)
  -> policydb_write -> security_load_policy (5.4 path: sidtab conversion, swap,
  AVC reset and policy-load notifications are handled by the kernel itself)
- the ~9 policy internals used by sepolicy.c (hashtab_insert/search, avtab_*,
  ebitmap_set_bit/get_bit) are NOT exported; they are resolved with
  kallsyms_lookup_name (kln) and called through the CFI-safe blr trampoline
- sepolicy.c was adapted for 5.4 (filename_trans hashtab layout, guarded
  selinux_policy-based helpers)
Verified after load: "ksu: applied kernelsu rules on 5.4",
`set root profile ... context: u:r:ksu:s0`, and `su` gives
`uid=0(root) ... context=u:r:ksu:s0`.

## Known issues / notes
- sudo rmmod requires killing the manager first: the app holds a `[ksu_driver]` fd (refcnt=1).
- ColorOS 安全警告 popup can appear during privileged testing; if it offers "立即重启" do NOT tap it.
- No reboot persistence: module is runtime-only (late-load). After reboot re-run the load procedure.
- `su` from a non-ksu-domain root shell is denied by design (sucompat requires allowlist uid).
- UI freezes were observed earlier with the old tracepoint design; current direct-hook design OK so far.

## Level parameter
insmod ... kln=0x<addr> level=N
 0=inert, 1=resolver only, 2=direct table hooks (recommended), 3=(late-load never uses)
