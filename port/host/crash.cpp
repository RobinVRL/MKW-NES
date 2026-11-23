// x86-64 test build only: print a backtrace on a crash (no gdb here).
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ucontext.h>

static void onCrash(int sig, siginfo_t* si, void* uc) {
    void* frames[64];
    const int n = backtrace(frames, 64);
    char head[128];
    const auto* ctx = static_cast<ucontext_t*>(uc);
#if defined(__x86_64__)
    void* pc = reinterpret_cast<void*>(ctx->uc_mcontext.gregs[REG_RIP]);
    void* lr = nullptr;
#else
    void* pc = reinterpret_cast<void*>(ctx->uc_mcontext.arm_pc);
    void* lr = reinterpret_cast<void*>(ctx->uc_mcontext.arm_lr);
#endif
    const int len = snprintf(head, sizeof head, "\n*** signal %d at %p (pc %p lr %p)\n", sig, si->si_addr, pc, lr);
    write(2, head, size_t(len));
    backtrace_symbols_fd(frames, n, 2);
    // load addresses, to symbolize PIE pcs
    if (FILE* m = fopen("/proc/self/maps", "r")) {
        char line[256];
        while (fgets(line, sizeof line, m))
            if (strstr(line, "r-xp")) write(2, line, strlen(line));
        fclose(m);
    }
    _exit(128 + sig);
}

__attribute__((constructor)) static void installCrashHandler() {
    struct sigaction sa {};
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    static char altstack[65536];
    stack_t st{altstack, 0, sizeof altstack};
    sigaltstack(&st, nullptr);
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (int s : sigs) sigaction(s, &sa, nullptr);
}
