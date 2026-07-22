#pragma once
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "objfmt.hpp"
#include "types.hpp"

enum class LinkMode
{
    Absolute,
    Relocatable
};

struct LinkConfig
{
    u16 loadAddr;
    bool loadAddrGiven; // true quando -org foi informado explicitamente
    LinkMode mode;
    std::vector< std::string > inputFiles;
    std::string outputFile;
    std::string mapFile;
    LinkConfig ()
        : loadAddr (0x0000), loadAddrGiven (false), mode (LinkMode::Absolute)
    {
    }
};

// Entrada da tabela de simbolos do mapa de ligacao.
struct LinkerSymbol
{
    std::string module;
    std::string name;
    u16 address;
    bool global;
};

struct ExeFile
{
    u16 origin;
    std::vector< u8 > data;
    std::vector< RelocEntry > relocs;
    std::vector< SymbolEntry > symbols;
};

class Linker
{
  public:
    ExeFile link (const std::vector< ObjectFile > &objects, const LinkConfig &cfg)
    {
        errors_.clear ();
        map_.clear ();
        symbols_.clear ();

        pass1 (objects, cfg);
        return pass2 (objects, cfg);
    }

    const std::vector< std::string > &errors () const
    {
        return errors_;
    }
    const std::vector< LinkerMapEntry > &map () const
    {
        return map_;
    }
    const std::vector< LinkerSymbol > &symbols () const
    {
        return symbols_;
    }

  private:
    std::unordered_map< std::string, u16 > globalSyms_;
    std::vector< std::string > errors_;
    std::vector< LinkerMapEntry > map_;
    std::vector< LinkerSymbol > symbols_;

    struct SegPlacement
    {
        std::string module;
        std::string segment;
        u16 base;
        u16 origin; // origem assumida pelo montador (ORG), 0 se nao houver
        u32 size;
    };

    std::vector< SegPlacement > placements_;

    // Calcula onde cada segmento de cada modulo sera carregado.
    void place (const std::vector< ObjectFile > &objs, const LinkConfig &cfg)
    {
        placements_.clear ();
        u16 cursor = cfg.loadAddr;

        for (auto &obj : objs)
        {
            for (auto &seg : obj.segments)
            {
                SegPlacement sp;
                sp.module = obj.filename;
                sp.segment = seg.name;
                sp.size = (u32)seg.data.size ();
                sp.origin = seg.hasOrigin ? (u16)seg.origin : 0;

                // O ORG do fonte e o endereco que o montador assumiu. Se o
                // endereco de carga foi informado na linha de comando, ele tem
                // precedencia e o modulo e relocado para la; caso contrario o
                // ORG do modulo e respeitado.
                if (seg.hasOrigin && !cfg.loadAddrGiven)
                    cursor = (u16)seg.origin;

                sp.base = cursor;
                cursor = (u16)(cursor + sp.size);
                placements_.push_back (sp);
            }
        }
    }

    // Endereco final de um simbolo: base do segmento + deslocamento dentro
    // dele. O montador grava o valor do rotulo ja com o ORG somado, entao o
    // ORG precisa ser descontado aqui para nao ser contado duas vezes.
    u16 symAddress (const std::string &module, const SymbolEntry &sym) const
    {
        for (auto &p : placements_)
            if (p.module == module && p.segment == sym.segment)
                return (u16)(p.base + (u16)(sym.value - p.origin));
        return sym.value;
    }

    void pass1 (const std::vector< ObjectFile > &objs, const LinkConfig &cfg)
    {
        globalSyms_.clear ();

        place (objs, cfg);

        for (auto &p : placements_)
        {
            LinkerMapEntry me;
            me.module = p.module;
            me.segment = p.segment;
            me.base = p.base;
            me.size = p.size;
            map_.push_back (me);
        }

        // tabela global: apenas simbolos exportados (GLOBAL/PUBLIC). Rotulos
        // locais ficam de fora, para que nomes iguais em modulos diferentes
        // nao colidam.
        for (auto &obj : objs)
        {
            for (auto &sym : obj.symbols)
            {
                if (!sym.defined)
                    continue;

                u16 resolved = symAddress (obj.filename, sym);

                LinkerSymbol ls;
                ls.module = obj.filename;
                ls.name = sym.name;
                ls.address = resolved;
                ls.global = sym.global;
                symbols_.push_back (ls);

                if (!sym.global)
                    continue;

                if (globalSyms_.count (sym.name))
                    errors_.push_back ("Simbolo global duplicado: " + sym.name);
                else
                    globalSyms_[sym.name] = resolved;
            }
        }

        for (auto &obj : objs)
        {
            for (auto &sym : obj.symbols)
            {
                if (!sym.defined && !globalSyms_.count (sym.name))
                    errors_.push_back ("Simbolo externo nao resolvido: " + sym.name +
                                       " (referenciado por " + obj.filename + ")");
            }
        }
    }

    ExeFile pass2 (const std::vector< ObjectFile > &objs, const LinkConfig &cfg)
    {
        ExeFile exe;
        exe.origin = cfg.loadAddr;

        // usa a alocacao ja calculada no passo 1
        u32 endAddr = cfg.loadAddr;
        for (auto &p : placements_)
            endAddr = std::max (endAddr, (u32)p.base + p.size);

        exe.data.resize (endAddr - cfg.loadAddr, 0);

        for (auto &obj : objs)
        {
            for (auto &seg : obj.segments)
            {
                u16 base = segBase (obj.filename, seg.name);
                if (base < cfg.loadAddr)
                {
                    errors_.push_back ("Segmento " + seg.name + " de " + obj.filename +
                                       " comeca antes do endereco de carga");
                    continue;
                }
                u32 off = base - cfg.loadAddr;
                if (off + seg.data.size () > exe.data.size ())
                {
                    errors_.push_back ("Segmento fora da imagem: " + seg.name);
                    continue;
                }
                std::copy (seg.data.begin (), seg.data.end (), exe.data.begin () + off);
            }

            for (auto &reloc : obj.relocs)
            {
                u16 segBase_ = segBase (obj.filename, reloc.segment);
                u32 absOff = (segBase_ - cfg.loadAddr) + reloc.offset;

                u16 symVal = 0;
                bool found = false;

                // um rotulo do proprio modulo tem precedencia sobre a tabela
                // global (nomes locais iguais em modulos distintos sao
                // independentes)
                for (auto &sym : obj.symbols)
                {
                    if (sym.name == reloc.symbol && sym.defined)
                    {
                        symVal = symAddress (obj.filename, sym);
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    auto it = globalSyms_.find (reloc.symbol);
                    if (it != globalSyms_.end ())
                    {
                        symVal = it->second;
                        found = true;
                    }
                }

                if (!found)
                {
                    errors_.push_back ("Relocacao sem simbolo: " + reloc.symbol);
                    continue;
                }

                symVal = (u16)(symVal + reloc.addend);

                if (cfg.mode == LinkMode::Absolute)
                {
                    // Ligador-Relocador: aplica a relocacao completa agora
                    if (reloc.type == RelocType::ABS16)
                    {
                        if (absOff + 1 < exe.data.size ())
                        {
                            exe.data[absOff] = (u8)(symVal & 0xFF);
                            exe.data[absOff + 1] = (u8)(symVal >> 8);
                        }
                    }
                    else if (reloc.type == RelocType::ABS8)
                    {
                        if (absOff < exe.data.size ())
                            exe.data[absOff] = (u8)(symVal & 0xFF);
                    }
                    else if (reloc.type == RelocType::REL8)
                    {
                        // o offset aponta para o byte de deslocamento, entao o
                        // PC apos a instrucao e offset + 1
                        u16 pc = (u16)(segBase_ + reloc.offset + 1);
                        i8 rel = (i8)((i16)symVal - (i16)pc);
                        if (absOff < exe.data.size ())
                            exe.data[absOff] = (u8)rel;
                    }
                }
                else
                {
                    // Apenas Ligador: guarda a relocacao resolvida para o
                    // Carregador Relocador finalizar no momento da carga
                    RelocEntry re = reloc;
                    re.offset = absOff;
                    re.addend = (i16)symVal;
                    exe.relocs.push_back (re);
                }
            }
        }

        for (auto &kv : globalSyms_)
        {
            SymbolEntry se;
            se.name = kv.first;
            se.value = kv.second;
            se.defined = true;
            se.global = true;
            exe.symbols.push_back (se);
        }

        return exe;
    }

    u16 segBase (const std::string &module, const std::string &seg) const
    {
        for (auto &p : placements_)
            if (p.module == module && p.segment == seg)
                return p.base;
        return 0;
    }
};

namespace ExeFmt
{

static constexpr u32 MAGIC = 0x5A383045;

inline void save (const ExeFile &exe, const std::string &path)
{
    std::ofstream f (path, std::ios::binary);
    if (!f)
        throw Z80Error ("Cannot write exe: " + path);
    ObjFmt::writeU32 (f, MAGIC);
    ObjFmt::writeU16 (f, exe.origin);
    ObjFmt::writeU32 (f, (u32)exe.data.size ());
    f.write ((char *)exe.data.data (), exe.data.size ());
    ObjFmt::writeU16 (f, (u16)exe.relocs.size ());
    for (auto &r : exe.relocs)
    {
        ObjFmt::writeU32 (f, r.offset);
        ObjFmt::writeU8 (f, (u8)r.type);
        ObjFmt::writeStr (f, r.symbol);
        ObjFmt::writeU16 (f, (u16)(i16)r.addend);
    }
    ObjFmt::writeU16 (f, (u16)exe.symbols.size ());
    for (auto &s : exe.symbols)
    {
        ObjFmt::writeStr (f, s.name);
        ObjFmt::writeU16 (f, s.value);
    }
}

inline ExeFile load (const std::string &path)
{
    std::ifstream f (path, std::ios::binary);
    if (!f)
        throw Z80Error ("Cannot read exe: " + path);
    u32 magic = ObjFmt::readU32 (f);
    if (magic != MAGIC)
        throw Z80Error ("Bad exe magic: " + path);
    ExeFile exe;
    exe.origin = ObjFmt::readU16 (f);
    u32 sz = ObjFmt::readU32 (f);
    exe.data.resize (sz);
    f.read ((char *)exe.data.data (), sz);
    u16 nrel = ObjFmt::readU16 (f);
    exe.relocs.resize (nrel);
    for (auto &r : exe.relocs)
    {
        r.offset = ObjFmt::readU32 (f);
        r.type = (RelocType)ObjFmt::readU8 (f);
        r.symbol = ObjFmt::readStr (f);
        r.addend = (i16)ObjFmt::readU16 (f);
    }
    u16 nsym = ObjFmt::readU16 (f);
    exe.symbols.resize (nsym);
    for (auto &s : exe.symbols)
    {
        s.name = ObjFmt::readStr (f);
        s.value = ObjFmt::readU16 (f);
        s.defined = true;
        s.global = true;
    }
    return exe;
}

} // namespace ExeFmt