#include <cstring>
#include <fstream>
#include <iostream>

#include "linker.hpp"

// So o nome do arquivo, sem o caminho, para o mapa ficar legivel.
static std::string baseName (const std::string &path)
{
    size_t p = path.find_last_of ("/\\");
    return p == std::string::npos ? path : path.substr (p + 1);
}

static std::string hex4 (u16 v)
{
    char b[8];
    snprintf (b, sizeof (b), "%04X", v);
    return b;
}

static void printUsage ()
{
    std::cerr << "Usage: z80link [options] file1.obj [file2.obj ...]\n"
              << "  -o <out.exe>    output file (default: out.exe)\n"
              << "  -m <out.map>    generate map file\n"
              << "  -org <addr>     load address in hex (default: 0000)\n"
              << "  -reloc          produce relocatable output (Relocating Loader)\n"
              << "  -abs            produce absolute output (Absolute Loader) "
                 "[default]\n";
}

int main (int argc, char **argv)
{
    if (argc < 2)
    {
        printUsage ();
        return 1;
    }

    LinkConfig cfg;
    cfg.outputFile = "out.exe";
    std::vector< std::string > inputs;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)
        {
            cfg.outputFile = argv[++i];
        }
        else if (a == "-m" && i + 1 < argc)
        {
            cfg.mapFile = argv[++i];
        }
        else if (a == "-org" && i + 1 < argc)
        {
            cfg.loadAddr = (u16)strtoul (argv[++i], nullptr, 16);
            cfg.loadAddrGiven = true;
        }
        else if (a == "-reloc")
        {
            cfg.mode = LinkMode::Relocatable;
        }
        else if (a == "-abs")
        {
            cfg.mode = LinkMode::Absolute;
        }
        else if (a[0] != '-')
        {
            inputs.push_back (a);
        }
        else
        {
            std::cerr << "Unknown option: " << a << "\n";
            printUsage ();
            return 1;
        }
    }

    if (inputs.empty ())
    {
        std::cerr << "No input files.\n";
        return 1;
    }

    std::vector< ObjectFile > objects;
    for (auto &f : inputs)
    {
        try
        {
            objects.push_back (ObjFmt::load (f));
        }
        catch (Z80Error &e)
        {
            std::cerr << "Load error: " << e.what () << "\n";
            return 1;
        }
    }

    Linker lnk;
    ExeFile exe;
    try
    {
        exe = lnk.link (objects, cfg);
    }
    catch (Z80Error &e)
    {
        std::cerr << "Link error: " << e.what () << "\n";
        return 1;
    }
    if (!lnk.errors ().empty ())
    {
        for (auto &e : lnk.errors ())
            std::cerr << e << "\n";
        return 1;
    }

    ExeFmt::save (exe, cfg.outputFile);
    std::cout << "Linked: " << cfg.outputFile << "  origin=0x" << std::hex << exe.origin
              << "  size=" << std::dec << exe.data.size () << "\n";

    if (!cfg.mapFile.empty ())
    {
        std::ofstream mf (cfg.mapFile);
        mf << "Mapa de ligacao\n";
        mf << "Modo: "
           << (cfg.mode == LinkMode::Absolute
                   ? "Ligador-Relocador (relocacao completa na ligacao)"
                   : "Ligador (relocacao finalizada na carga)")
           << "\n";
        mf << "Endereco de carga: " << hex4 (exe.origin) << "\n\n";

        mf << "Modulo                 Segmento   Base   Tamanho\n";
        mf << std::string (55, '-') << "\n";
        for (auto &me : lnk.map ())
        {
            char line[160];
            snprintf (line, sizeof (line), "%-22s %-10s %04X   %u",
                      baseName (me.module).c_str (), me.segment.c_str (), me.base,
                      me.size);
            mf << line << "\n";
        }

        mf << "\nTabela de simbolos\n";
        mf << "Modulo                 Simbolo              Endereco  Escopo\n";
        mf << std::string (63, '-') << "\n";
        for (auto &sym : lnk.symbols ())
        {
            char line[160];
            snprintf (line, sizeof (line), "%-22s %-20s %04X      %s",
                      baseName (sym.module).c_str (), sym.name.c_str (), sym.address,
                      sym.global ? "GLOBAL" : "local");
            mf << line << "\n";
        }

        if (!exe.relocs.empty ())
        {
            mf << "\nRelocacoes pendentes para o Carregador Relocador: "
               << exe.relocs.size () << "\n";
        }

        std::cout << "Map: " << cfg.mapFile << "\n";
    }
    return 0;
}