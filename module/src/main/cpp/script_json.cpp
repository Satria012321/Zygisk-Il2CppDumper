#include "script_json.h"
#include "il2cpp-class.h"
#include "log.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <sstream>
#include <fstream>
#include <unistd.h>

#define DO_API(r, n, p) extern r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

extern uint64_t il2cpp_base;

// ── JSON escape ──────────────────────────────────────────────────────────
static std::string jesc(const char *s) {
    if (!s) return "";
    std::string o;
    for (const char *p = s; *p; ++p) {
        if      (*p == '"')  o += "\\\"";
        else if (*p == '\\') o += "\\\\";
        else if (*p == '\n') o += "\\n";
        else if (*p == '\r') o += "\\r";
        else if (*p == '\t') o += "\\t";
        else                 o += *p;
    }
    return o;
}

// ── RVA dari method pointer ───────────────────────────────────────────────
static uint64_t method_rva(const void *ptr) {
    if (!ptr) return 0;
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (il2cpp_base && addr >= il2cpp_base)
        return addr - il2cpp_base;
    return 0;
}

// ── TypeInfo = RVA dari Il2CppClass* ─────────────────────────────────────
// Il2CppClass* adalah struct di dalam .so (bukan heap).
// dladdr resolves base .so-nya, lalu hitung offset = typeinfo address.
static uint64_t typeinfo_rva(Il2CppClass *klass) {
    if (!klass) return 0;
    Dl_info di;
    if (dladdr(reinterpret_cast<const void*>(klass), &di) && di.dli_fbase) {
        uint64_t base = reinterpret_cast<uint64_t>(di.dli_fbase);
        uint64_t addr = reinterpret_cast<uint64_t>(klass);
        if (addr >= base) return addr - base;
    }
    // fallback
    uint64_t addr = reinterpret_cast<uint64_t>(klass);
    if (il2cpp_base && addr >= il2cpp_base) return addr - il2cpp_base;
    return 0;
}

void script_json_dump(const char *outDir) {
    LOGI("[script.json] start");

    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies ||
        !il2cpp_image_get_class || !il2cpp_image_get_class_count) {
        LOGE("[script.json] api not ready");
        return;
    }

    auto *domain     = il2cpp_domain_get();
    size_t asm_count = 0;
    auto **assemblies = il2cpp_domain_get_assemblies(domain, &asm_count);
    if (!assemblies || asm_count == 0) {
        LOGE("[script.json] no assemblies");
        return;
    }

    std::ostringstream js;
    js << "{\n  \"ScriptMethod\": [\n";
    bool first_m = true;

    for (size_t ai = 0; ai < asm_count; ai++) {
        auto *image = il2cpp_assembly_get_image(assemblies[ai]);
        if (!image) continue;
        size_t cc = il2cpp_image_get_class_count(image);

        for (size_t ci = 0; ci < cc; ci++) {
            auto *klass = const_cast<Il2CppClass*>(il2cpp_image_get_class(image, ci));
            if (!klass) continue;

            const char *cname = il2cpp_class_get_name      ? il2cpp_class_get_name(klass)      : "?";
            const char *ns    = il2cpp_class_get_namespace  ? il2cpp_class_get_namespace(klass)  : "";
            uint64_t    ti    = typeinfo_rva(klass);

            if (!il2cpp_class_get_methods) continue;
            void *iter = nullptr;
            while (auto *m = il2cpp_class_get_methods(klass, &iter)) {
                if (!m->methodPointer) continue;
                uint64_t ma = method_rva(reinterpret_cast<const void*>(m->methodPointer));
                if (!ma) continue;

                const char *mn = il2cpp_method_get_name ? il2cpp_method_get_name(m) : "?";

                // "Namespace.ClassName$$MethodName" — format Il2CppDumper
                std::string sig;
                if (ns && ns[0]) { sig += ns; sig += '.'; }
                sig += cname;
                sig += "$$";
                sig += mn ? mn : "?";

                if (!first_m) js << ",\n";
                first_m = false;
                js << "    {\"Address\":" << ma
                   << ",\"Name\":\"" << jesc(sig.c_str())
                   << "\",\"TypeInfo\":" << ti << "}";
            }
        }
    }

    js << "\n  ],\n  \"ScriptString\": [],\n  \"ScriptMetadata\": [\n";
    bool first_c = true;

    for (size_t ai = 0; ai < asm_count; ai++) {
        auto *image = il2cpp_assembly_get_image(assemblies[ai]);
        if (!image) continue;
        size_t cc = il2cpp_image_get_class_count(image);

        for (size_t ci = 0; ci < cc; ci++) {
            auto *klass = const_cast<Il2CppClass*>(il2cpp_image_get_class(image, ci));
            if (!klass) continue;

            const char *cname = il2cpp_class_get_name     ? il2cpp_class_get_name(klass)     : "?";
            const char *ns    = il2cpp_class_get_namespace ? il2cpp_class_get_namespace(klass) : "";
            uint64_t    ti    = typeinfo_rva(klass);
            if (!ti) continue;

            std::string fullname;
            if (ns && ns[0]) { fullname += ns; fullname += '.'; }
            fullname += cname;

            if (!first_c) js << ",\n";
            first_c = false;
            js << "    {\"Address\":" << ti
               << ",\"Name\":\"" << jesc(fullname.c_str())
               << "\",\"Signature\":null}";
        }
    }

    js << "\n  ],\n  \"ScriptMetadataMethod\": [\n";
    bool first_mm = true;

    for (size_t ai = 0; ai < asm_count; ai++) {
        auto *image = il2cpp_assembly_get_image(assemblies[ai]);
        if (!image) continue;
        size_t cc = il2cpp_image_get_class_count(image);

        for (size_t ci = 0; ci < cc; ci++) {
            auto *klass = const_cast<Il2CppClass*>(il2cpp_image_get_class(image, ci));
            if (!klass) continue;
            uint64_t ti = typeinfo_rva(klass);

            if (!il2cpp_class_get_methods) continue;
            void *iter = nullptr;
            while (auto *m = il2cpp_class_get_methods(klass, &iter)) {
                if (!m->methodPointer) continue;
                uint64_t ma = method_rva(reinterpret_cast<const void*>(m->methodPointer));
                if (!ma) continue;
                const char *mn = il2cpp_method_get_name ? il2cpp_method_get_name(m) : "?";

                if (!first_mm) js << ",\n";
                first_mm = false;
                js << "    {\"Address\":" << ma
                   << ",\"Name\":\"" << jesc(mn)
                   << "\",\"MethodAddress\":" << ma
                   << ",\"TypeInfo\":" << ti << "}";
            }
        }
    }

    js << "\n  ]\n}\n";

    std::string outPath = std::string(outDir) + "/files/script.json";
    std::ofstream out(outPath);
    if (!out.is_open()) {
        LOGE("[script.json] cannot open %s", outPath.c_str());
        return;
    }
    out << js.str();
    out.close();
    LOGI("[script.json] done -> %s", outPath.c_str());
}
