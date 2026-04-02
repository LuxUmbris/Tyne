// ── Standard headers (must be at file scope, never inside a class) ────────────
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

#include "lexer.hpp"
#include "parser.hpp"
#include "son_ir.hpp"
#include "ir_builder.hpp"

// ── TOML config ───────────────────────────────────────────────────────────────
// Minimal hand-rolled TOML reader for the subset used by Tyne build configs.
// Replace with toml++ (https://github.com/marzer/tomlplusplus) for full TOML:
//   drop single-header toml.hpp next to this file and restore the original
//   #include <toml++/toml.hpp> and toml::parse_file() calls.

class TomlDoc {
    std::unordered_map<std::string, std::string> flat; // "section.key" -> raw value
    std::string current_section;

    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }
    static std::string stripQuotes(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

public:
    bool load(const std::string& filename) {
        std::ifstream f(filename);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos)
                    current_section = trim(line.substr(1, end - 1));
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key   = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            if (!value.empty() && value[0] != '"') {
                size_t hpos = value.find('#');
                if (hpos != std::string::npos) value = trim(value.substr(0, hpos));
            }
            std::string fqkey = current_section.empty() ? key : current_section + "." + key;
            flat[fqkey] = stripQuotes(value);
        }
        return true;
    }

    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = flat.find(key); return it == flat.end() ? def : it->second; }
    bool getBool(const std::string& key, bool def = false) const {
        auto it = flat.find(key);
        if (it == flat.end()) return def;
        return it->second == "true"; }
    int getInt(const std::string& key, int def = 0) const {
        auto it = flat.find(key);
        if (it == flat.end()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; } }
};

// ── Config struct ─────────────────────────────────────────────────────────────

struct TyneConfig {
    std::string source_file;       // optional: overrides stem-derived path
    std::string output_file;
    std::string target_arch        = "x86_64";
    bool        optimize           = true;
    int         optimization_level = 2;
    std::vector<std::string>                     include_paths;
    std::unordered_map<std::string, std::string> defines;
    bool        debug_symbols = false;
    std::string entry_point   = "main";
};

// ── Binary format ─────────────────────────────────────────────────────────────

struct TyneBinaryHeader {
    uint32_t magic             = 0x54594E45u; // "TYNE"
    uint16_t version_major     = 1;
    uint16_t version_minor     = 0;
    uint16_t target_arch;   // 0=x86_64  1=arm32  2=arm64  3=riscv64
    uint16_t flags             = 0;
    uint64_t code_size;
    uint64_t data_size;
    uint64_t symbol_table_offset;
    uint64_t string_table_offset;
    uint64_t relocation_table_offset;
};

struct SymbolEntry {
    uint32_t name_offset;
    uint32_t value;
    uint8_t  type;    // 0=function  1=variable  2=constant
    uint8_t  binding; // 0=local     1=global
};

struct RelocationEntry {
    uint32_t offset;
    uint32_t symbol_index;
    uint8_t  type;
};

// ── Code generator ────────────────────────────────────────────────────────────

class CodeGenerator {
public:
    enum class Architecture { X86_64, ARM64, ARM32, RISCV64 };
    explicit CodeGenerator(Architecture arch) : target_arch(arch) {}

    std::vector<uint8_t> generateCode(const IRGraph& graph) {
        code_buffer.clear();
        switch (target_arch) {
            case Architecture::X86_64:  return generateX86_64(graph);
            case Architecture::ARM64:   return generateARM64(graph);
            case Architecture::ARM32:   return generateARM32(graph);
            case Architecture::RISCV64: return generateRISCV64(graph);
            default: throw std::runtime_error("Unsupported architecture");
        }
    }

private:
    Architecture         target_arch;
    std::vector<uint8_t> code_buffer;

    // x86-64
    std::vector<uint8_t> generateX86_64(const IRGraph& graph) {
        emitX86(0x55); emitX86(0x48, 0x89, 0xE5);
        for (const auto& n : graph.nodes) genX86Node(n.get());
        emitX86(0xB8, 0x00, 0x00, 0x00, 0x00); emitX86(0x5D); emitX86(0xC3);
        return code_buffer;
    }
    void genX86Node(const IRNode* n) {
        switch (n->kind) {
            case IRNodeKind::Constant: {
                int64_t v = 0;
                try { v = std::stoll(n->value); } catch (...) {}
                emitX86(0x48, 0xB8);
                for (int i = 0; i < 8; ++i) emitX86(static_cast<uint8_t>((v >> (i*8)) & 0xFF));
                break; }
            case IRNodeKind::Add:     emitX86(0x48, 0x01, 0xD0); break;
            case IRNodeKind::Sub:     emitX86(0x48, 0x29, 0xD0); break;
            case IRNodeKind::Mul:     emitX86(0x48, 0x0F, 0xAF, 0xC2, 0x00); break;
            case IRNodeKind::Compare: emitX86(0x48, 0x39, 0xD0); break;
            case IRNodeKind::Return:
                // mov rax, <retval already in rax>; pop rbp; ret
                emitX86(0x5D); emitX86(0xC3); break;
            case IRNodeKind::Call:
                emitX86(0xE8, 0x00, 0x00, 0x00, 0x00); break;
            default: break;
        }
    }

    // ARM64
    std::vector<uint8_t> generateARM64(const IRGraph& graph) {
        for (const auto& n : graph.nodes) genARM64Node(n.get());
        emitA64(0x00,0x00,0x80,0xD2); emitA64(0xC0,0x03,0x5F,0xD6);
        return code_buffer;
    }
    void genARM64Node(const IRNode* n) {
        switch (n->kind) {
            case IRNodeKind::Constant: {
                int64_t v = std::stoll(n->value);
                uint32_t ins = 0xD2800000u | (static_cast<uint32_t>(v & 0xFFFF));
                emitA64((ins>>24)&0xFF,(ins>>16)&0xFF,(ins>>8)&0xFF,ins&0xFF); break; }
            case IRNodeKind::Add:  emitA64(0x00,0x00,0x01,0x8B); break;
            case IRNodeKind::Call: emitA64(0x00,0x00,0x00,0x94); break;
            default: break;
        }
    }

    // ARM32
    std::vector<uint8_t> generateARM32(const IRGraph& graph) {
        for (const auto& n : graph.nodes) genARM32Node(n.get());
        emitA32(0x00,0x00,0xA0,0xE3); emitA32(0x1E,0xFF,0x2F,0xE1);
        return code_buffer;
    }
    void genARM32Node(const IRNode* n) {
        switch (n->kind) {
            case IRNodeKind::Constant: {
                int32_t v = std::stoi(n->value);
                uint32_t ins = 0xE3A00000u | (static_cast<uint32_t>(v & 0xFF));
                emitA32((ins>>24)&0xFF,(ins>>16)&0xFF,(ins>>8)&0xFF,ins&0xFF); break; }
            case IRNodeKind::Add:  emitA32(0x01,0x00,0x80,0xE0); break;
            case IRNodeKind::Call: emitA32(0x00,0x00,0x00,0xEB); break;
            default: break;
        }
    }

    // RISC-V 64
    std::vector<uint8_t> generateRISCV64(const IRGraph& graph) {
        for (const auto& n : graph.nodes) genRV64Node(n.get());
        emitRV(0x02,0x05); emitRV(0x82,0x80);
        return code_buffer;
    }
    void genRV64Node(const IRNode* n) {
        switch (n->kind) {
            case IRNodeKind::Constant: {
                int64_t v = std::stoll(n->value);
                if (v >= -2048 && v <= 2047) {
                    auto ins = static_cast<uint16_t>(0x4000u|((v&0x1F)<<7)|((v&0x3E0)>>5));
                    emitRV(ins&0xFF,(ins>>8)&0xFF);
                }
                break; }
            case IRNodeKind::Add:  emitRV(0xB3,0x05); break;
            case IRNodeKind::Call: emitRV(0x02,0x00); break;
            default: break;
        }
    }

    // emit helpers
    void emitX86(uint8_t b) { code_buffer.push_back(b); }
    void emitX86(uint8_t a,uint8_t b) { code_buffer.push_back(a);code_buffer.push_back(b); }
    void emitX86(uint8_t a,uint8_t b,uint8_t c) { emitX86(a,b); code_buffer.push_back(c); }
    void emitX86(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e)
        { emitX86(a,b,c); code_buffer.push_back(d); code_buffer.push_back(e); }
    void emitA64(uint8_t a,uint8_t b,uint8_t c,uint8_t d)
        { code_buffer.push_back(a);code_buffer.push_back(b);
          code_buffer.push_back(c);code_buffer.push_back(d); }
    void emitA32(uint8_t a,uint8_t b,uint8_t c,uint8_t d) { emitA64(a,b,c,d); }
    void emitRV(uint8_t a,uint8_t b) { code_buffer.push_back(a);code_buffer.push_back(b); }
};

// ── Memory-mapped loader ──────────────────────────────────────────────────────

class TyneLoader {
    void*  mapped_memory = nullptr;
    size_t mapped_size   = 0;
#ifdef _WIN32
    HANDLE file_handle    = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = INVALID_HANDLE_VALUE;
    void* do_mmap(const std::string& fn) {
        file_handle = CreateFileA(fn.c_str(),GENERIC_READ,FILE_SHARE_READ,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file_handle==INVALID_HANDLE_VALUE) return nullptr;
        mapping_handle = CreateFileMappingA(file_handle,nullptr,PAGE_READONLY,0,0,nullptr);
        if (!mapping_handle){CloseHandle(file_handle);return nullptr;}
        mapped_memory = MapViewOfFile(mapping_handle,FILE_MAP_READ,0,0,0);
        if (!mapped_memory){CloseHandle(mapping_handle);CloseHandle(file_handle);return nullptr;}
        LARGE_INTEGER sz; GetFileSizeEx(file_handle,&sz);
        mapped_size = static_cast<size_t>(sz.QuadPart);
        return mapped_memory;
    }
    void do_munmap() {
        if (mapped_memory){UnmapViewOfFile(mapped_memory);mapped_memory=nullptr;}
        if (mapping_handle!=INVALID_HANDLE_VALUE){CloseHandle(mapping_handle);mapping_handle=INVALID_HANDLE_VALUE;}
        if (file_handle!=INVALID_HANDLE_VALUE){CloseHandle(file_handle);file_handle=INVALID_HANDLE_VALUE;}
        mapped_size=0;
    }
#else
    int fd = -1;
    void* do_mmap(const std::string& fn) {
        fd = open(fn.c_str(), O_RDONLY);
        if (fd==-1) return nullptr;
        struct stat sb;
        if (fstat(fd,&sb)==-1){close(fd);fd=-1;return nullptr;}
        mapped_size = static_cast<size_t>(sb.st_size);
        void* m = ::mmap(nullptr,mapped_size,PROT_READ,MAP_PRIVATE,fd,0);
        if (m==MAP_FAILED){close(fd);fd=-1;return nullptr;}
        return (mapped_memory=m);
    }
    void do_munmap() {
        if (mapped_memory&&mapped_memory!=MAP_FAILED){::munmap(mapped_memory,mapped_size);mapped_memory=nullptr;}
        if (fd!=-1){close(fd);fd=-1;}
        mapped_size=0;
    }
#endif
public:
    ~TyneLoader(){ do_munmap(); }
    bool load(const std::string& fn){ do_munmap(); return do_mmap(fn)!=nullptr; }

    const TyneBinaryHeader* getHeader() const {
        if (!mapped_memory||mapped_size<sizeof(TyneBinaryHeader)) return nullptr;
        return static_cast<const TyneBinaryHeader*>(mapped_memory);
    }
    const void* getCodeSection() const {
        return mapped_memory
            ? static_cast<const char*>(mapped_memory)+sizeof(TyneBinaryHeader)
            : nullptr;
    }
    size_t getCodeSize() const {
        auto h=getHeader(); return h ? static_cast<size_t>(h->code_size) : 0; }
    const char* getStringTable() const {
        auto h=getHeader();
        return (h&&h->string_table_offset)
            ? static_cast<const char*>(mapped_memory)+h->string_table_offset : nullptr;
    }
    const SymbolEntry* getSymbolTable(size_t& count) const {
        auto h=getHeader();
        if (!h||!h->symbol_table_offset){count=0;return nullptr;}
        count=0; // TODO: proper count
        return reinterpret_cast<const SymbolEntry*>(
            static_cast<const char*>(mapped_memory)+h->symbol_table_offset);
    }
};

// ── Binary writer ─────────────────────────────────────────────────────────────

class TyneBinaryWriter {
    std::vector<uint8_t>                      buf;
    std::unordered_map<std::string,uint32_t>  str_offsets;
    std::vector<SymbolEntry>                  symbols;
    std::vector<RelocationEntry>              relocs;

    uint32_t getStrOff(const std::string& s) {
        auto it=str_offsets.find(s);
        if (it!=str_offsets.end()) return it->second;
        auto off=static_cast<uint32_t>(buf.size());
        str_offsets[s]=off; return off;
    }
public:
    void writeHeader(uint16_t arch, uint64_t cs, uint64_t ds) {
        TyneBinaryHeader h;
        h.target_arch             = arch;
        h.code_size               = cs;
        h.data_size               = ds;
        h.symbol_table_offset     = sizeof(TyneBinaryHeader)+cs+ds;
        h.string_table_offset     = h.symbol_table_offset+symbols.size()*sizeof(SymbolEntry);
        h.relocation_table_offset = h.string_table_offset+buf.size();
        buf.insert(buf.begin(),
                   reinterpret_cast<uint8_t*>(&h),
                   reinterpret_cast<uint8_t*>(&h)+sizeof(h));
    }
    void writeCode(const std::vector<uint8_t>& c){ buf.insert(buf.end(),c.begin(),c.end()); }
    void writeData(const std::vector<uint8_t>& d){ buf.insert(buf.end(),d.begin(),d.end()); }
    void addSymbol(const std::string& name,uint32_t val,uint8_t type,uint8_t bind){
        SymbolEntry s; s.name_offset=getStrOff(name); s.value=val; s.type=type; s.binding=bind;
        symbols.push_back(s);
    }
    void addRelocation(uint32_t off,uint32_t sym,uint8_t type){
        RelocationEntry r; r.offset=off; r.symbol_index=sym; r.type=type; relocs.push_back(r);
    }
    void writeSymbolTable(){
        for(auto& s:symbols) buf.insert(buf.end(),
            reinterpret_cast<const uint8_t*>(&s),
            reinterpret_cast<const uint8_t*>(&s)+sizeof(s));
    }
    void writeStringTable(){
        for(auto& [s,_]:str_offsets){ buf.insert(buf.end(),s.begin(),s.end()); buf.push_back(0); }
    }
    void writeRelocationTable(){
        for(auto& r:relocs) buf.insert(buf.end(),
            reinterpret_cast<const uint8_t*>(&r),
            reinterpret_cast<const uint8_t*>(&r)+sizeof(r));
    }
    bool saveToFile(const std::string& fn){
        std::ofstream f(fn,std::ios::binary);
        if(!f) return false;
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        return f.good();
    }
};

// ── Config loader ─────────────────────────────────────────────────────────────

TyneConfig loadConfig(const std::string& config_file) {
    TyneConfig cfg;
    TomlDoc    doc;
    if (!doc.load(config_file))
        std::cerr << "Warning: could not open config: " << config_file << "\n";
    cfg.source_file        = doc.getString("source.file");
    cfg.output_file        = doc.getString("output.file");
    cfg.target_arch        = doc.getString("target.arch",           "x86_64");
    cfg.optimize           = doc.getBool  ("optimization.enabled",  true);
    cfg.optimization_level = doc.getInt   ("optimization.level",    2);
    cfg.debug_symbols      = doc.getBool  ("debug.symbols",         false);
    cfg.entry_point        = doc.getString("entry.point",           "main");
    return cfg;
}

// ── Arch helpers ──────────────────────────────────────────────────────────────

static uint16_t getArchCode(const std::string& a) {
    if (a=="arm32")   return 1;
    if (a=="arm64")   return 2;
    if (a=="riscv64") return 3;
    return 0;
}
static CodeGenerator::Architecture getArchEnum(const std::string& a) {
    if (a=="arm64")   return CodeGenerator::Architecture::ARM64;
    if (a=="arm32")   return CodeGenerator::Architecture::ARM32;
    if (a=="riscv64") return CodeGenerator::Architecture::RISCV64;
    return CodeGenerator::Architecture::X86_64;
}
static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size()>=suffix.size() &&
           s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;
}

// ── Stdlib helper used by IRBuilder ──────────────────────────────────────────
// Forward-declared in ir_builder.hpp; defined here after all classes exist.
static bool compileAndBuildIR(const std::string& src_path, IRGraph& graph) {
    try {
        std::ifstream f(src_path);
        if (!f) return false;
        std::string src(std::istreambuf_iterator<char>(f), {});

        Lexer lexer(src);
        std::vector<Lexer::Token> tokens;
        Lexer::Token tok;
        do { tok = lexer.nextToken(); tokens.push_back(tok); }
        while (tok.type != Lexer::TokenType::EOF_TOKEN);

        Parser parser(tokens);
        auto* ast = parser.parseProgram();

        // Re-use same graph; lib search path derived from the file's directory
        std::filesystem::path p(src_path);
        IRBuilder builder(graph, { p.parent_path() });
        builder.buildProgram(ast);
        delete ast;
        return true;
    } catch (...) {
        return false; // best-effort: don't abort main compile for a bad stdlib file
    }
}

// ── Compiler pipeline ─────────────────────────────────────────────────────────

bool compileToBinary(const std::string& src_file, const TyneConfig& cfg,
                     const std::string& config_file = "") {
    try {
        std::ifstream f(src_file);
        if (!f){ std::cerr<<"Cannot open: "<<src_file<<"\n"; return false; }
        std::string src(std::istreambuf_iterator<char>(f),{});

        Lexer lexer(src);
        std::vector<Lexer::Token> tokens;
        Lexer::Token tok;
        do { tok=lexer.nextToken(); tokens.push_back(tok); }
        while (tok.type != Lexer::TokenType::EOF_TOKEN);

        Parser parser(tokens);
        auto*  ast = parser.parseProgram();

        // Build lib search paths: directory of source file + <source_dir>/lib
        std::filesystem::path srcPath(src_file);
        std::vector<std::filesystem::path> libPaths = {
            srcPath.parent_path(),
            srcPath.parent_path() / "lib",
            std::filesystem::path(config_file).parent_path() / "lib",
        };

        IRGraph   graph;
        IRBuilder builder(graph, libPaths);
        builder.buildProgram(ast);
        delete ast;

        CodeGenerator        codegen(getArchEnum(cfg.target_arch));
        std::vector<uint8_t> code = codegen.generateCode(graph);
        std::vector<uint8_t> data;

        TyneBinaryWriter w;
        w.writeHeader(getArchCode(cfg.target_arch), code.size(), data.size());
        w.writeCode(code);
        w.writeData(data);
        w.addSymbol(cfg.entry_point, 0, 0, 1);
        w.writeSymbolTable();
        w.writeStringTable();
        w.writeRelocationTable();

        std::filesystem::path out(cfg.output_file);
        if (out.has_parent_path())
            std::filesystem::create_directories(out.parent_path());

        return w.saveToFile(cfg.output_file);
    } catch (const std::exception& e) {
        std::cerr<<"Compilation error: "<<e.what()<<"\n"; return false;
    }
}

// ── Loader ────────────────────────────────────────────────────────────────────

bool loadAndExecute(const std::string& bin) {
    TyneLoader loader;
    if (!loader.load(bin)){ std::cerr<<"Failed to load: "<<bin<<"\n"; return false; }
    auto* h = loader.getHeader();
    if (!h){ std::cerr<<"Invalid binary format\n"; return false; }
    if (h->magic != 0x54594E45u){ std::cerr<<"Invalid magic number\n"; return false; }
    std::cout<<"Loaded Tyne binary:\n"
             <<"  Version      : "<<h->version_major<<"."<<h->version_minor<<"\n"
             <<"  Architecture : "<<h->target_arch<<"\n"
             <<"  Code size    : "<<h->code_size<<" bytes\n"
             <<"  Data size    : "<<h->data_size<<" bytes\n";
    return true;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr<<"Usage: tyne <config.toml>   # compile a .tyne source\n"
                 <<"       tyne <binary>        # load a .tyne.bin binary\n";
        return 1;
    }
    std::string input = argv[1];

    if (endsWith(input, ".toml")) {
        TyneConfig cfg = loadConfig(input);
        if (cfg.output_file.empty()){
            std::cerr<<"No output.file in config\n"; return 1; }
        std::filesystem::path p(input);
        std::string src;
        if (!cfg.source_file.empty()) {
            // Resolve relative to config file's directory
            src = (p.parent_path() / cfg.source_file).string();
        } else {
            src = (p.parent_path() / p.stem()).string() + ".tyne";
        }
        std::cout<<"Compiling "<<src<<"  ->  "<<cfg.output_file<<"\n";
        if (compileToBinary(src, cfg, input)){ std::cout<<"Compilation successful.\n"; return 0; }
        std::cerr<<"Compilation failed.\n"; return 1;
    } else {
        std::cout<<"Loading "<<input<<" ...\n";
        if (loadAndExecute(input)){ std::cout<<"Done.\n"; return 0; }
        std::cerr<<"Failed.\n"; return 1;
    }
}
