// Temp tool: resolve module offsets to symbol names via DbgHelp (crash triage).
// Usage: sym_resolve.exe <module> <hex-offset> [<more offsets>...]
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "dbghelp.lib")

namespace {

struct NearSym {
    ULONG64 addr;
    char name[256];
};

struct Ctx {
    ULONG64 addr = 0;    // absolute target address
    ULONG64 base = 0;    // module base
    NearSym below[3] = {};
    NearSym above[3] = {};
    int nb = 0;
    int na = 0;
};

BOOL CALLBACK EnumCallback(PSYMBOL_INFO s, ULONG64 /*size*/, PVOID p) {
    Ctx* c = static_cast<Ctx*>(p);
    if (s->Address <= c->addr) {
        if (c->nb == 3) {
            c->below[0] = c->below[1];
            c->below[1] = c->below[2];
            c->nb = 2;
        }
        ::strncpy_s(c->below[c->nb].name, s->Name, 255);
        c->below[c->nb].addr = s->Address;
        c->nb++;
    } else if (c->na < 3) {
        ::strncpy_s(c->above[c->na].name, s->Name, 255);
        c->above[c->na].addr = s->Address;
        c->na++;
    }
    return c->na < 3;   // stop after collecting 3 above the target
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: sym_resolve <module> <offset-hex>...\n");
        return 1;
    }
    wchar_t module[MAX_PATH];
    ::MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, module, MAX_PATH);

    ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (!::SymInitializeW(::GetCurrentProcess(), nullptr, FALSE)) {
        printf("SymInitialize failed %lu\n", ::GetLastError());
        return 1;
    }
    const ULONG64 base = ::SymLoadModuleExW(::GetCurrentProcess(), nullptr, module,
                                            nullptr, 0, 0, nullptr, 0);
    if (!base) {
        printf("SymLoadModule failed %lu\n", ::GetLastError());
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        const ULONG64 off = ::_strtoui64(argv[i], nullptr, 16);
        const ULONG64 addr = base + off;
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(new char[sizeof(SYMBOL_INFO) + 512]);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (::SymFromAddr(::GetCurrentProcess(), addr, &disp, sym)) {
            printf("0x%08llx = %s + %llu\n", static_cast<unsigned long long>(off),
                   sym->Name, static_cast<unsigned long long>(disp));
        } else {
            printf("0x%08llx = ? (err %lu)\n", static_cast<unsigned long long>(off),
                   ::GetLastError());
        }
        delete[] reinterpret_cast<char*>(sym);

        Ctx ctx;
        ctx.addr = addr;
        ctx.base = base;
        ::SymEnumSymbols(::GetCurrentProcess(), base, nullptr,
                         reinterpret_cast<PSYM_ENUMERATESYMBOLS_CALLBACK>(&EnumCallback),
                         &ctx);
        for (int k = 0; k < ctx.nb; k++) {
            printf("  < 0x%08llx %s (%llu bytes before)\n",
                   static_cast<unsigned long long>(ctx.below[k].addr - base),
                   ctx.below[k].name,
                   static_cast<unsigned long long>(off - (ctx.below[k].addr - base)));
        }
        for (int k = 0; k < ctx.na; k++) {
            printf("  > 0x%08llx %s\n",
                   static_cast<unsigned long long>(ctx.above[k].addr - base),
                   ctx.above[k].name);
        }
    }
    ::SymCleanup(::GetCurrentProcess());
    return 0;
}
