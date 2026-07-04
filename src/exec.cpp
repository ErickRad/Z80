#include <cstring>
#include <iomanip>
#include <iostream>

#include "cpu.hpp"
#include "linker.hpp"

static void printRegs (const Z80CPU &cpu)
{
    auto &r = cpu.regs;
    std::cout << std::hex << std::setfill ('0') << "AF=" << std::setw (2) << (int)r.A
              << std::setw (2) << (int)r.F << " BC=" << std::setw (2) << (int)r.B
              << std::setw (2) << (int)r.C << " DE=" << std::setw (2) << (int)r.D
              << std::setw (2) << (int)r.E << " HL=" << std::setw (2) << (int)r.H
              << std::setw (2) << (int)r.L << " IX=" << std::setw (4) << r.IX
              << " IY=" << std::setw (4) << r.IY << " SP=" << std::setw (4) << r.SP
              << " PC=" << std::setw (4) << r.PC << std::dec << "\n";
}

static void applyPendingRelocations (ExeFile &exe, u16 loadAddr)
{
    i16 shift = (i16)(loadAddr - exe.origin);

    for (auto &r : exe.relocs)
    {
        u16 symVal = (u16)r.addend + (u16)shift;

        if (r.type == RelocType::ABS16)
        {
            if (r.offset + 1 < exe.data.size ())
            {
                exe.data[r.offset] = (u8)(symVal & 0xFF);
                exe.data[r.offset + 1] = (u8)(symVal >> 8);
            }
        }
        else if (r.type == RelocType::ABS8)
        {
            if (r.offset < exe.data.size ())
                exe.data[r.offset] = (u8)(symVal & 0xFF);
        }
        else if (r.type == RelocType::REL8)
        {
            u16 pc = (u16)(loadAddr + r.offset + 2);
            i8 rel = (i8)((i16)symVal - (i16)pc);
            if (r.offset < exe.data.size ())
                exe.data[r.offset] = (u8)rel;
        }
    }

    exe.origin = loadAddr;
    exe.relocs.clear ();
}

int main (int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: z80exec <program.exe> [--trace] [--max-cycles N] "
                     "[--load-addr HEX]\n"
                  << "  --load-addr HEX  Carregador Relocador: aplica as relocacoes\n"
                  << "                   pendentes no momento da carga, posicionando\n"
                  << "                   o programa no endereco hexadecimal informado.\n"
                  << "                   Sem esta opcao, usa o endereco gravado pelo\n"
                  << "                   ligador (Carregador Absoluto).\n";
        return 1;
    }

    std::string file = argv[1];
    bool trace = false;
    bool hasLoadAddr = false;
    u16 loadAddr = 0;
    u64 maxCycles = 10000000;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--trace")
            trace = true;
        else if (a == "--max-cycles" && i + 1 < argc)
            maxCycles = strtoull (argv[++i], nullptr, 10);
        else if (a == "--load-addr" && i + 1 < argc)
        {
            loadAddr = (u16)strtoul (argv[++i], nullptr, 16);
            hasLoadAddr = true;
        }
    }

    ExeFile exe;
    try
    {
        exe = ExeFmt::load (file);
    }
    catch (Z80Error &e)
    {
        std::cerr << "Load error: " << e.what () << "\n";
        return 1;
    }

    if (!exe.relocs.empty ())
    {
        u16 target = hasLoadAddr ? loadAddr : exe.origin;
        std::cout << "Carregador Relocador: aplicando " << exe.relocs.size ()
                  << " relocacao(oes) pendente(s) em 0x" << std::hex << target << std::dec
                  << "\n";
        applyPendingRelocations (exe, target);
    }
    else if (hasLoadAddr && loadAddr != exe.origin)
    {
        std::cerr << "Aviso: executavel nao possui relocacoes pendentes "
                     "(foi gerado pelo Ligador-Relocador / Carregador Absoluto); "
                     "--load-addr sera ignorado e o endereco original sera usado.\n";
    }

    Z80CPU cpu;
    cpu.loadBinary (exe.data, exe.origin);

    cpu.ioWrite = [] (u16 port, u8 val) {
        if (port == 0x00)
            std::cout << (char)val << std::flush;
    };
    cpu.ioRead = [] (u16) -> u8 {
        return 0xFF;
    };

    while (!cpu.regs.halted && cpu.cycles < maxCycles)
    {
        if (trace)
        {
            std::cout << std::hex << std::setw (4) << std::setfill ('0') << cpu.regs.PC
                      << ": ";
            printRegs (cpu);
        }
        cpu.step ();
    }

    std::cout << "\n--- HALT ---\n";
    printRegs (cpu);
    std::cout << "Cycles: " << std::dec << cpu.cycles << "\n";
    return 0;
}