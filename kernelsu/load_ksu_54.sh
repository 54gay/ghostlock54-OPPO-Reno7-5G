#!/system/bin/sh
# KernelSU 3.3.1 port for OPPO Reno7 5G (PFJM10), kernel 5.4.254-qgki-gaa96eae94d32
# Run INSIDE a root shell (uid 0), e.g. from a terminal after `su`.
# Runtime module: must be reloaded after every reboot (no partition changes).
#   sh /storage/emulated/0/Download/load_ksu_54.sh
KO=/data/local/tmp/kernelsu.ko

if [ ! -f "$KO" ]; then
  echo "[!] $KO not found"
  echo "    Please place the ko file at $KO"
  exit 1
fi

if grep -q kernelsu /proc/modules; then
  echo "[*] kernelsu module already loaded, nothing to do"
  exit 0
fi

# kptr_restrict=1: normal users cannot read kallsyms, root (CAP_SYSLOG) can.
echo "[*] resolve kallsyms_lookup_name"
echo 1 > /proc/sys/kernel/kptr_restrict
KLN=$(grep -E " kallsyms_lookup_name$" /proc/kallsyms | head -n1 | cut -d" " -f1)
if [ -z "$KLN" ] || [ "$KLN" = "0000000000000000" ]; then
  echo 0 > /proc/sys/kernel/kptr_restrict
  KLN=$(grep -E " kallsyms_lookup_name$" /proc/kallsyms | head -n1 | cut -d" " -f1)
  echo 1 > /proc/sys/kernel/kptr_restrict
fi

echo "[*] load module (kln=0x$KLN)"
/system/bin/insmod "$KO" kln=0x$KLN level=3
echo "insmod_ret=$? kln=$KLN"
echo "[*] modules:"; grep -c kernelsu /proc/modules

# Restart the manager so it picks up the fresh driver fd
echo "[*] restarting KernelSU Manager"
am force-stop me.weishu.kernelsu 2>/dev/null
sleep 1
am start -n me.weishu.kernelsu/.ui.MainActivity >/dev/null 2>&1
echo "[*] done."
