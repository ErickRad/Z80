#pragma once
#include <array>
#include <functional>

#include "types.hpp"

class Z80CPU
{
  public:
    std::array< u8, MEM_SIZE > mem;
    Z80Regs regs;
    u64 cycles;

    using IOReadFn = std::function< u8 (u16) >;
    using IOWriteFn = std::function< void (u16, u8) >;
    IOReadFn ioRead;
    IOWriteFn ioWrite;

    static constexpr u8 FLAG_S = 0x80;
    static constexpr u8 FLAG_Z = 0x40;
    static constexpr u8 FLAG_H = 0x10;
    static constexpr u8 FLAG_PV = 0x04;
    static constexpr u8 FLAG_N = 0x02;
    static constexpr u8 FLAG_C = 0x01;

    Z80CPU () : cycles (0)
    {
        reset ();
        ioRead = [] (u16) {
            return (u8)0xFF;
        };
        ioWrite = [] (u16, u8) {
        };
    }

    void reset ()
    {
        mem.fill (0);
        regs = {};
        regs.SP = 0xFFFF;
        regs.PC = 0;
        regs.halted = false;
        cycles = 0;
    }

    void loadBinary (const std::vector< u8 > &data, u16 origin)
    {
        for (size_t i = 0; i < data.size () && (u32)(origin + i) < MEM_SIZE; ++i)
            mem[origin + i] = data[i];
        regs.PC = origin;
    }

    bool step ()
    {
        if (regs.halted)
        {
            cycles += 4;
            return false;
        }
        executeOne ();
        return true;
    }

    int stepN (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (regs.halted)
                return i;
            executeOne ();
        }
        return n;
    }

  private:
    u8 rb (u16 a) const
    {
        return mem[a];
    }
    void wb (u16 a, u8 v)
    {
        mem[a] = v;
    }
    u16 rw (u16 a) const
    {
        return (u16)(mem[a] | (mem[(u16)(a + 1)] << 8));
    }
    void ww (u16 a, u16 v)
    {
        mem[a] = (u8)(v & 0xFF);
        mem[(u16)(a + 1)] = (u8)(v >> 8);
    }

    u8 fetch8 ()
    {
        return mem[regs.PC++];
    }
    u16 fetch16 ()
    {
        u16 v = rw (regs.PC);
        regs.PC += 2;
        return v;
    }
    i8 fetchS ()
    {
        return (i8)fetch8 ();
    }

    void push16 (u16 v)
    {
        regs.SP -= 2;
        ww (regs.SP, v);
    }
    u16 pop16 ()
    {
        u16 v = rw (regs.SP);
        regs.SP += 2;
        return v;
    }

    bool sf () const
    {
        return (regs.F & FLAG_S) != 0;
    }
    bool zf () const
    {
        return (regs.F & FLAG_Z) != 0;
    }
    bool hf () const
    {
        return (regs.F & FLAG_H) != 0;
    }
    bool pvf () const
    {
        return (regs.F & FLAG_PV) != 0;
    }
    bool nf () const
    {
        return (regs.F & FLAG_N) != 0;
    }
    bool cf () const
    {
        return (regs.F & FLAG_C) != 0;
    }

    void setF (bool s, bool z, bool h, bool pv, bool n, bool c)
    {
        regs.F = (u8)((s ? FLAG_S : 0) | (z ? FLAG_Z : 0) | (h ? FLAG_H : 0) |
                      (pv ? FLAG_PV : 0) | (n ? FLAG_N : 0) | (c ? FLAG_C : 0));
    }

    static bool parity (u8 v)
    {
        v ^= v >> 4;
        v ^= v >> 2;
        v ^= v >> 1;
        return (v & 1) == 0;
    }

    u8 &regRef (int r)
    {
        switch (r)
        {
        case 0:
            return regs.B;
        case 1:
            return regs.C;
        case 2:
            return regs.D;
        case 3:
            return regs.E;
        case 4:
            return regs.H;
        case 5:
            return regs.L;
        case 7:
            return regs.A;
        }
        static u8 dummy = 0;
        return dummy;
    }

    u8 getR (int r) const
    {
        switch (r)
        {
        case 0:
            return regs.B;
        case 1:
            return regs.C;
        case 2:
            return regs.D;
        case 3:
            return regs.E;
        case 4:
            return regs.H;
        case 5:
            return regs.L;
        case 6:
            return rb ((u16)(regs.H << 8 | regs.L));
        case 7:
            return regs.A;
        }
        return 0;
    }
    void setR (int r, u8 v)
    {
        if (r == 6)
        {
            wb ((u16)(regs.H << 8 | regs.L), v);
            return;
        }
        regRef (r) = v;
    }

    u16 getReg16 (int rp) const
    {
        switch (rp)
        {
        case 0:
            return (u16)(regs.B << 8 | regs.C);
        case 1:
            return (u16)(regs.D << 8 | regs.E);
        case 2:
            return (u16)(regs.H << 8 | regs.L);
        case 3:
            return regs.SP;
        }
        return 0;
    }
    void setReg16 (int rp, u16 v)
    {
        switch (rp)
        {
        case 0:
            regs.B = (u8)(v >> 8);
            regs.C = (u8)(v & 0xFF);
            break;
        case 1:
            regs.D = (u8)(v >> 8);
            regs.E = (u8)(v & 0xFF);
            break;
        case 2:
            regs.H = (u8)(v >> 8);
            regs.L = (u8)(v & 0xFF);
            break;
        case 3:
            regs.SP = v;
            break;
        }
    }
    u16 getReg16AF (int rp) const
    {
        if (rp == 3)
            return (u16)(regs.A << 8 | regs.F);
        return getReg16 (rp);
    }
    void setReg16AF (int rp, u16 v)
    {
        if (rp == 3)
        {
            regs.A = (u8)(v >> 8);
            regs.F = (u8)(v & 0xFF);
            return;
        }
        setReg16 (rp, v);
    }

    void doADD (u8 v, bool carry = false)
    {
        int ci = carry && cf () ? 1 : 0;
        u16 r = (u16)regs.A + v + ci;
        u8 r8 = (u8)r;
        bool h = (((regs.A & 0xF) + (v & 0xF) + ci) & 0x10) != 0;
        bool ov = ((~(regs.A ^ v)) & (regs.A ^ r8) & 0x80) != 0;
        setF (r8 & 0x80, r8 == 0, h, ov, false, r > 0xFF);
        regs.A = r8;
    }
    void doSUB (u8 v, bool carry = false)
    {
        int ci = carry && cf () ? 1 : 0;
        int r = (int)regs.A - (int)v - ci;
        u8 r8 = (u8)r;
        bool h = (((regs.A & 0xF) - (v & 0xF) - ci) & 0x10) != 0;
        bool ov = ((regs.A ^ v) & (regs.A ^ r8) & 0x80) != 0;
        setF (r8 & 0x80, r8 == 0, h, ov, true, r < 0);
        regs.A = r8;
    }
    void doAND (u8 v)
    {
        regs.A &= v;
        setF (regs.A & 0x80, regs.A == 0, true, parity (regs.A), false, false);
    }
    void doOR (u8 v)
    {
        regs.A |= v;
        setF (regs.A & 0x80, regs.A == 0, false, parity (regs.A), false, false);
    }
    void doXOR (u8 v)
    {
        regs.A ^= v;
        setF (regs.A & 0x80, regs.A == 0, false, parity (regs.A), false, false);
    }
    void doCP (u8 v)
    {
        u8 s = regs.A;
        doSUB (v);
        regs.A = s;
    }

    u8 doINC8 (u8 v)
    {
        u8 r = v + 1;
        regs.F =
            (u8)((regs.F & FLAG_C) | (r & 0x80 ? FLAG_S : 0) | (r == 0 ? FLAG_Z : 0) |
                 ((v & 0xF) == 0xF ? FLAG_H : 0) | (v == 0x7F ? FLAG_PV : 0));
        return r;
    }
    u8 doDEC8 (u8 v)
    {
        u8 r = v - 1;
        regs.F =
            (u8)((regs.F & FLAG_C) | (r & 0x80 ? FLAG_S : 0) | (r == 0 ? FLAG_Z : 0) |
                 ((v & 0xF) == 0 ? FLAG_H : 0) | (v == 0x80 ? FLAG_PV : 0) | FLAG_N);
        return r;
    }

    void doADDHL (u16 v)
    {
        u16 hl = (u16)(regs.H << 8 | regs.L);
        u32 r = hl + v;
        bool h = ((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF;
        regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (h ? FLAG_H : 0) |
                      (r > 0xFFFF ? FLAG_C : 0));
        regs.H = (u8)(r >> 8);
        regs.L = (u8)(r & 0xFF);
    }

    u8 doRLC (u8 v)
    {
        u8 c = v >> 7;
        v = (u8)((v << 1) | c);
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doRRC (u8 v)
    {
        u8 c = v & 1;
        v = (u8)((v >> 1) | (c << 7));
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doRL (u8 v)
    {
        u8 c = v >> 7;
        v = (u8)((v << 1) | (cf () ? 1 : 0));
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doRR (u8 v)
    {
        u8 c = v & 1;
        v = (u8)((v >> 1) | (cf () ? 0x80 : 0));
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doSLA (u8 v)
    {
        u8 c = v >> 7;
        v = (u8)(v << 1);
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doSRA (u8 v)
    {
        u8 c = v & 1;
        v = (u8)((v >> 1) | (v & 0x80));
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }
    u8 doSRL (u8 v)
    {
        u8 c = v & 1;
        v = v >> 1;
        setF (v & 0x80, v == 0, false, parity (v), false, c);
        return v;
    }

    // Nas instrucoes IN r,(C) / OUT (C),r o endereco de porta e o par BC
    // completo, com B na parte alta.
    u16 bcPort () const
    {
        return (u16)(regs.B << 8 | regs.C);
    }

    bool condCC (int cc) const
    {
        switch (cc)
        {
        case 0:
            return !zf ();
        case 1:
            return zf ();
        case 2:
            return !cf ();
        case 3:
            return cf ();
        case 4:
            return !pvf ();
        case 5:
            return pvf ();
        case 6:
            return !sf ();
        case 7:
            return sf ();
        }
        return false;
    }

    void executeCB (u16 iy_ix = 0, bool hasIdx = false, i8 d = 0)
    {
        u8 op2 = fetch8 ();
        u16 addr = hasIdx ? (u16)(iy_ix + d) : (u16)(regs.H << 8 | regs.L);
        int bx = op2 >> 6, by = (op2 >> 3) & 7, bz = op2 & 7;
        u8 val = (bz == 6 || hasIdx) ? rb (addr) : getR (bz);
        if (bx == 0)
        {
            switch (by)
            {
            case 0:
                val = doRLC (val);
                break;
            case 1:
                val = doRRC (val);
                break;
            case 2:
                val = doRL (val);
                break;
            case 3:
                val = doRR (val);
                break;
            case 4:
                val = doSLA (val);
                break;
            case 5:
                val = doSRA (val);
                break;
            case 6:
                val = doSLA (val);
                break;
            case 7:
                val = doSRL (val);
                break;
            }
            if (bz == 6 || hasIdx)
                wb (addr, val);
            else
                setR (bz, val);
        }
        else if (bx == 1)
        {
            bool b = (val >> by) & 1;
            regs.F =
                (u8)((regs.F & FLAG_C) | (b ? 0 : FLAG_Z) | FLAG_H | (b ? 0 : FLAG_PV));
        }
        else if (bx == 2)
        {
            val &= ~(1 << by);
            if (bz == 6 || hasIdx)
                wb (addr, val);
            else
                setR (bz, val);
        }
        else
        {
            val |= (1 << by);
            if (bz == 6 || hasIdx)
                wb (addr, val);
            else
                setR (bz, val);
        }
        cycles += 8;
    }

    void executeED ()
    {
        u8 op = fetch8 ();
        switch (op)
        {
        case 0x40:
            regs.B = ioRead (bcPort ());
            setF (regs.B & 0x80, regs.B == 0, false, parity (regs.B), false, cf ());
            break;
        case 0x48:
            regs.C = ioRead (bcPort ());
            setF (regs.C & 0x80, regs.C == 0, false, parity (regs.C), false, cf ());
            break;
        case 0x50:
            regs.D = ioRead (bcPort ());
            setF (regs.D & 0x80, regs.D == 0, false, parity (regs.D), false, cf ());
            break;
        case 0x58:
            regs.E = ioRead (bcPort ());
            setF (regs.E & 0x80, regs.E == 0, false, parity (regs.E), false, cf ());
            break;
        case 0x60:
            regs.H = ioRead (bcPort ());
            setF (regs.H & 0x80, regs.H == 0, false, parity (regs.H), false, cf ());
            break;
        case 0x68:
            regs.L = ioRead (bcPort ());
            setF (regs.L & 0x80, regs.L == 0, false, parity (regs.L), false, cf ());
            break;
        case 0x78:
            regs.A = ioRead (bcPort ());
            setF (regs.A & 0x80, regs.A == 0, false, parity (regs.A), false, cf ());
            break;
        case 0x41:
            ioWrite (bcPort (), regs.B);
            break;
        case 0x49:
            ioWrite (bcPort (), regs.C);
            break;
        case 0x51:
            ioWrite (bcPort (), regs.D);
            break;
        case 0x59:
            ioWrite (bcPort (), regs.E);
            break;
        case 0x61:
            ioWrite (bcPort (), regs.H);
            break;
        case 0x69:
            ioWrite (bcPort (), regs.L);
            break;
        case 0x79:
            ioWrite (bcPort (), regs.A);
            break;
        case 0x43: {
            u16 nn = fetch16 ();
            ww (nn, getReg16 (0));
            break;
        }
        case 0x4B: {
            u16 nn = fetch16 ();
            setReg16 (0, rw (nn));
            break;
        }
        case 0x53: {
            u16 nn = fetch16 ();
            ww (nn, getReg16 (1));
            break;
        }
        case 0x5B: {
            u16 nn = fetch16 ();
            setReg16 (1, rw (nn));
            break;
        }
        case 0x63: {
            u16 nn = fetch16 ();
            ww (nn, getReg16 (2));
            break;
        }
        case 0x6B: {
            u16 nn = fetch16 ();
            setReg16 (2, rw (nn));
            break;
        }
        case 0x73: {
            u16 nn = fetch16 ();
            ww (nn, regs.SP);
            break;
        }
        case 0x7B: {
            u16 nn = fetch16 ();
            regs.SP = rw (nn);
            break;
        }
        case 0x42: {
            u16 hl = getReg16 (2), bc = getReg16 (0);
            int r = (int)hl - (int)bc - (cf () ? 1 : 0);
            bool ov = ((hl ^ bc) & (hl ^ (u16)r) & 0x8000) != 0;
            setF (r & 0x8000, r == 0, ((hl ^ bc ^ r) & 0x1000) != 0, ov, true, r < 0);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x4A: {
            u16 hl = getReg16 (2), bc = getReg16 (0);
            u32 r = (u32)hl + bc + (cf () ? 1 : 0);
            bool ov = ((~(hl ^ bc)) & (hl ^ (u16)r) & 0x8000) != 0;
            setF (r & 0x8000, (u16)r == 0, ((hl ^ bc ^ r) & 0x1000) != 0, ov, false,
                  r > 0xFFFF);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x52: {
            u16 hl = getReg16 (2), de = getReg16 (1);
            int r = (int)hl - (int)de - (cf () ? 1 : 0);
            bool ov = ((hl ^ de) & (hl ^ (u16)r) & 0x8000) != 0;
            setF (r & 0x8000, r == 0, false, ov, true, r < 0);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x5A: {
            u16 hl = getReg16 (2), de = getReg16 (1);
            u32 r = (u32)hl + de + (cf () ? 1 : 0);
            bool ov = ((~(hl ^ de)) & (hl ^ (u16)r) & 0x8000) != 0;
            setF (r & 0x8000, (u16)r == 0, false, ov, false, r > 0xFFFF);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x72: {
            u16 hl = getReg16 (2), sp = regs.SP;
            int r = (int)hl - (int)sp - (cf () ? 1 : 0);
            setF (r & 0x8000, r == 0, false, false, true, r < 0);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x7A: {
            u16 hl = getReg16 (2), sp = regs.SP;
            u32 r = (u32)hl + sp + (cf () ? 1 : 0);
            setF (r & 0x8000, (u16)r == 0, false, false, false, r > 0xFFFF);
            setReg16 (2, (u16)r);
            break;
        }
        case 0x44:
        case 0x4C:
        case 0x54:
        case 0x5C:
        case 0x64:
        case 0x6C:
        case 0x74:
        case 0x7C: {
            u8 a = regs.A;
            regs.A = 0;
            doSUB (a);
            break;
        }
        case 0x45:
        case 0x55:
        case 0x65:
        case 0x75:
            regs.PC = pop16 ();
            regs.IFF1 = regs.IFF2;
            break;
        case 0x4D:
            regs.PC = pop16 ();
            break;
        case 0x47:
            regs.I = regs.A;
            break;
        case 0x4F:
            regs.R = regs.A;
            break;
        case 0x57:
            regs.A = regs.I;
            setF (regs.A & 0x80, regs.A == 0, false, regs.IFF2, false, cf ());
            break;
        case 0x5F:
            regs.A = regs.R;
            setF (regs.A & 0x80, regs.A == 0, false, regs.IFF2, false, cf ());
            break;
        case 0x46:
        case 0x56:
        case 0x5E:
        case 0x66:
        case 0x6E:
        case 0x76:
        case 0x7E:
            regs.IFF1 = regs.IFF2 = false;
            break;
        case 0xA0: {
            u8 v = rb ((u16)(regs.H << 8 | regs.L));
            wb ((u16)(regs.D << 8 | regs.E), v);
            setReg16 (2, getReg16 (2) + 1);
            setReg16 (1, getReg16 (1) + 1);
            u16 bc = getReg16 (0) - 1;
            setReg16 (0, bc);
            regs.F = (u8)((regs.F & ~(FLAG_H | FLAG_PV | FLAG_N)) | (bc ? FLAG_PV : 0));
            break;
        }
        case 0xA8: {
            u8 v = rb ((u16)(regs.H << 8 | regs.L));
            wb ((u16)(regs.D << 8 | regs.E), v);
            setReg16 (2, getReg16 (2) - 1);
            setReg16 (1, getReg16 (1) - 1);
            u16 bc = getReg16 (0) - 1;
            setReg16 (0, bc);
            regs.F = (u8)((regs.F & ~(FLAG_H | FLAG_PV | FLAG_N)) | (bc ? FLAG_PV : 0));
            break;
        }
        case 0xB0: {
            while (getReg16 (0))
            {
                u8 v = rb ((u16)(regs.H << 8 | regs.L));
                wb ((u16)(regs.D << 8 | regs.E), v);
                setReg16 (2, getReg16 (2) + 1);
                setReg16 (1, getReg16 (1) + 1);
                setReg16 (0, getReg16 (0) - 1);
                cycles += 21;
            }
            regs.F &= ~(FLAG_H | FLAG_PV | FLAG_N);
            break;
        }
        case 0xB8: {
            while (getReg16 (0))
            {
                u8 v = rb ((u16)(regs.H << 8 | regs.L));
                wb ((u16)(regs.D << 8 | regs.E), v);
                setReg16 (2, getReg16 (2) - 1);
                setReg16 (1, getReg16 (1) - 1);
                setReg16 (0, getReg16 (0) - 1);
                cycles += 21;
            }
            regs.F &= ~(FLAG_H | FLAG_PV | FLAG_N);
            break;
        }
        case 0xA1: {
            u8 v = rb ((u16)(regs.H << 8 | regs.L));
            setReg16 (2, getReg16 (2) + 1);
            u16 bc = getReg16 (0) - 1;
            setReg16 (0, bc);
            u8 r = regs.A - v;
            setF (r & 0x80, r == 0, ((regs.A ^ v ^ r) & 0x10) != 0, bc != 0, true, cf ());
            break;
        }
        case 0xA9: {
            u8 v = rb ((u16)(regs.H << 8 | regs.L));
            setReg16 (2, getReg16 (2) - 1);
            u16 bc = getReg16 (0) - 1;
            setReg16 (0, bc);
            u8 r = regs.A - v;
            setF (r & 0x80, r == 0, ((regs.A ^ v ^ r) & 0x10) != 0, bc != 0, true, cf ());
            break;
        }
        case 0xB1: {
            do
            {
                u8 v = rb ((u16)(regs.H << 8 | regs.L));
                setReg16 (2, getReg16 (2) + 1);
                u16 bc = getReg16 (0) - 1;
                setReg16 (0, bc);
                u8 r = regs.A - v;
                setF (r & 0x80, r == 0, ((regs.A ^ v ^ r) & 0x10) != 0, bc != 0, true,
                      cf ());
                cycles += 21;
            } while (getReg16 (0) && !zf ());
            break;
        }
        case 0xB9: {
            do
            {
                u8 v = rb ((u16)(regs.H << 8 | regs.L));
                setReg16 (2, getReg16 (2) - 1);
                u16 bc = getReg16 (0) - 1;
                setReg16 (0, bc);
                u8 r = regs.A - v;
                setF (r & 0x80, r == 0, ((regs.A ^ v ^ r) & 0x10) != 0, bc != 0, true,
                      cf ());
                cycles += 21;
            } while (getReg16 (0) && !zf ());
            break;
        }
        case 0x67: {
            u8 a = regs.A, t = rb ((u16)(regs.H << 8 | regs.L));
            wb ((u16)(regs.H << 8 | regs.L), (u8)((t >> 4) | (a << 4)));
            regs.A = (a & 0xF0) | (t & 0x0F);
            setF (regs.A & 0x80, regs.A == 0, false, parity (regs.A), false, cf ());
            break;
        }
        case 0x6F: {
            u8 a = regs.A, t = rb ((u16)(regs.H << 8 | regs.L));
            wb ((u16)(regs.H << 8 | regs.L), (u8)((t << 4) | (a & 0xF)));
            regs.A = (a & 0xF0) | (t >> 4);
            setF (regs.A & 0x80, regs.A == 0, false, parity (regs.A), false, cf ());
            break;
        }
        default:
            break;
        }
        cycles += 8;
    }

    void executeDDFD (bool isIY)
    {
        u16 &idx = isIY ? regs.IY : regs.IX;
        u8 op2 = fetch8 ();
        if (op2 == 0xCB)
        {
            i8 d = fetchS ();
            executeCB (idx, true, d);
            return;
        }
        switch (op2)
        {
        case 0x09:
            idx += getReg16 (0);
            break;
        case 0x19:
            idx += getReg16 (1);
            break;
        case 0x21:
            idx = fetch16 ();
            break;
        case 0x22: {
            u16 nn = fetch16 ();
            ww (nn, idx);
            break;
        }
        case 0x23:
            idx++;
            break;
        case 0x29: {
            u32 r = idx + idx;
            regs.F =
                (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (r > 0xFFFF ? FLAG_C : 0));
            idx = (u16)r;
            break;
        }
        case 0x2A: {
            u16 nn = fetch16 ();
            idx = rw (nn);
            break;
        }
        case 0x2B:
            idx--;
            break;
        case 0x34: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), doINC8 (rb ((u16)(idx + d))));
            break;
        }
        case 0x35: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), doDEC8 (rb ((u16)(idx + d))));
            break;
        }
        case 0x36: {
            i8 d = fetchS ();
            u8 n = fetch8 ();
            wb ((u16)(idx + d), n);
            break;
        }
        case 0x39: {
            u32 r = idx + regs.SP;
            regs.F =
                (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (r > 0xFFFF ? FLAG_C : 0));
            idx = (u16)r;
            break;
        }
        case 0x46: {
            i8 d = fetchS ();
            regs.B = rb ((u16)(idx + d));
            break;
        }
        case 0x4E: {
            i8 d = fetchS ();
            regs.C = rb ((u16)(idx + d));
            break;
        }
        case 0x56: {
            i8 d = fetchS ();
            regs.D = rb ((u16)(idx + d));
            break;
        }
        case 0x5E: {
            i8 d = fetchS ();
            regs.E = rb ((u16)(idx + d));
            break;
        }
        case 0x66: {
            i8 d = fetchS ();
            regs.H = rb ((u16)(idx + d));
            break;
        }
        case 0x6E: {
            i8 d = fetchS ();
            regs.L = rb ((u16)(idx + d));
            break;
        }
        case 0x7E: {
            i8 d = fetchS ();
            regs.A = rb ((u16)(idx + d));
            break;
        }
        case 0x70: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.B);
            break;
        }
        case 0x71: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.C);
            break;
        }
        case 0x72: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.D);
            break;
        }
        case 0x73: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.E);
            break;
        }
        case 0x74: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.H);
            break;
        }
        case 0x75: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.L);
            break;
        }
        case 0x77: {
            i8 d = fetchS ();
            wb ((u16)(idx + d), regs.A);
            break;
        }
        case 0x86: {
            i8 d = fetchS ();
            doADD (rb ((u16)(idx + d)));
            break;
        }
        case 0x8E: {
            i8 d = fetchS ();
            doADD (rb ((u16)(idx + d)), true);
            break;
        }
        case 0x96: {
            i8 d = fetchS ();
            doSUB (rb ((u16)(idx + d)));
            break;
        }
        case 0x9E: {
            i8 d = fetchS ();
            doSUB (rb ((u16)(idx + d)), true);
            break;
        }
        case 0xA6: {
            i8 d = fetchS ();
            doAND (rb ((u16)(idx + d)));
            break;
        }
        case 0xAE: {
            i8 d = fetchS ();
            doXOR (rb ((u16)(idx + d)));
            break;
        }
        case 0xB6: {
            i8 d = fetchS ();
            doOR (rb ((u16)(idx + d)));
            break;
        }
        case 0xBE: {
            i8 d = fetchS ();
            doCP (rb ((u16)(idx + d)));
            break;
        }
        case 0xE1:
            idx = pop16 ();
            break;
        case 0xE3: {
            u16 t = rw (regs.SP);
            ww (regs.SP, idx);
            idx = t;
            break;
        }
        case 0xE5:
            push16 (idx);
            break;
        case 0xE9:
            regs.PC = idx;
            break;
        case 0xF9:
            regs.SP = idx;
            break;
        default:
            break;
        }
        cycles += 8;
    }

    void executeOne ()
    {
        u8 op = fetch8 ();
        int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;

        if (x == 1)
        {
            if (y == 6 && z == 6)
            {
                regs.halted = true;
                cycles += 4;
                return;
            }
            setR (y, getR (z));
            cycles += (y == 6 || z == 6) ? 7 : 4;
            return;
        }
        if (x == 2)
        {
            u8 v = getR (z);
            cycles += z == 6 ? 7 : 4;
            switch (y)
            {
            case 0:
                doADD (v);
                break;
            case 1:
                doADD (v, true);
                break;
            case 2:
                doSUB (v);
                break;
            case 3:
                doSUB (v, true);
                break;
            case 4:
                doAND (v);
                break;
            case 5:
                doXOR (v);
                break;
            case 6:
                doOR (v);
                break;
            case 7:
                doCP (v);
                break;
            }
            return;
        }

        switch (op)
        {
        case 0x00:
            cycles += 4;
            break;
        case 0x76:
            regs.halted = true;
            cycles += 4;
            break;
        case 0x07: {
            u8 c = regs.A >> 7;
            regs.A = (u8)((regs.A << 1) | c);
            regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
            cycles += 4;
            break;
        }
        case 0x0F: {
            u8 c = regs.A & 1;
            regs.A = (u8)((regs.A >> 1) | (c << 7));
            regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
            cycles += 4;
            break;
        }
        case 0x17: {
            u8 c = regs.A >> 7;
            regs.A = (u8)((regs.A << 1) | (cf () ? 1 : 0));
            regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
            cycles += 4;
            break;
        }
        case 0x1F: {
            u8 c = regs.A & 1;
            regs.A = (u8)((regs.A >> 1) | (cf () ? 0x80 : 0));
            regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
            cycles += 4;
            break;
        }
        case 0x27: {
            u8 a = regs.A;
            if (!nf ())
            {
                if (hf () || ((a & 0xF) > 9))
                    a += 0x06;
                if (cf () || (a > 0x9F))
                {
                    a += 0x60;
                    regs.F |= FLAG_C;
                }
                else
                    regs.F &= ~(u8)FLAG_C;
            }
            else
            {
                if (hf () || ((a & 0xF) > 9))
                    a -= 0x06;
                if (cf () || a > 0x99)
                    a -= 0x60;
            }
            regs.A = a;
            regs.F = (u8)((regs.F & (FLAG_C | FLAG_N)) | (a & 0x80 ? FLAG_S : 0) |
                          (a == 0 ? FLAG_Z : 0) | (parity (a) ? FLAG_PV : 0));
            cycles += 4;
            break;
        }
        case 0x2F:
            regs.A = ~regs.A;
            regs.F |= FLAG_H | FLAG_N;
            cycles += 4;
            break;
        case 0x37:
            regs.F = (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | FLAG_C);
            cycles += 4;
            break;
        case 0x3F:
            regs.F =
                (u8)((regs.F & (FLAG_S | FLAG_Z | FLAG_PV)) | (cf () ? FLAG_H : FLAG_C));
            cycles += 4;
            break;
        case 0x08: {
            u8 a = regs.A, f = regs.F;
            regs.A = regs.A2;
            regs.F = regs.F2;
            regs.A2 = a;
            regs.F2 = f;
            cycles += 4;
            break;
        }
        case 0xD9: {
            std::swap (regs.B, regs.B2);
            std::swap (regs.C, regs.C2);
            std::swap (regs.D, regs.D2);
            std::swap (regs.E, regs.E2);
            std::swap (regs.H, regs.H2);
            std::swap (regs.L, regs.L2);
            cycles += 4;
            break;
        }
        case 0xEB: {
            u8 td = regs.D, te = regs.E;
            regs.D = regs.H;
            regs.E = regs.L;
            regs.H = td;
            regs.L = te;
            cycles += 4;
            break;
        }
        case 0xE3: {
            u16 t = rw (regs.SP);
            ww (regs.SP, getReg16 (2));
            setReg16 (2, t);
            cycles += 19;
            break;
        }
        case 0xF3:
            regs.IFF1 = regs.IFF2 = false;
            cycles += 4;
            break;
        case 0xFB:
            regs.IFF1 = regs.IFF2 = true;
            cycles += 4;
            break;
        case 0xDB: {
            u8 n = fetch8 ();
            regs.A = ioRead (n);
            cycles += 11;
            break;
        }
        case 0xD3: {
            u8 n = fetch8 ();
            ioWrite (n, regs.A);
            cycles += 11;
            break;
        }
        case 0xCB:
            executeCB ();
            break;
        case 0xED:
            executeED ();
            break;
        case 0xDD:
            executeDDFD (false);
            break;
        case 0xFD:
            executeDDFD (true);
            break;
        case 0xC9:
            regs.PC = pop16 ();
            cycles += 10;
            break;
        default:
            if (x == 0)
            {
                switch (z)
                {
                case 0:
                    if (y == 0)
                    {
                        cycles += 4;
                    }
                    else if (y == 1)
                    {
                        cycles += 4;
                    }
                    else if (y == 2)
                    {
                        // 0x10 DJNZ d: decrementa B e salta se B != 0
                        i8 d = fetchS ();
                        --regs.B;
                        if (regs.B != 0)
                        {
                            regs.PC = (u16)(regs.PC + d);
                            cycles += 13;
                        }
                        else
                            cycles += 8;
                    }
                    else if (y == 3)
                    {
                        i8 d = fetchS ();
                        regs.PC = (u16)(regs.PC + d);
                        cycles += 12;
                    }
                    else
                    {
                        i8 d = fetchS ();
                        if (condCC (y - 4))
                            regs.PC = (u16)(regs.PC + d);
                        cycles += 12;
                    }
                    break;
                case 1:
                    if (q == 0)
                    {
                        // 0x01/11/21/31 LD rr,nn
                        setReg16 (p, fetch16 ());
                        cycles += 10;
                    }
                    else
                    {
                        // 0x09/19/29/39 ADD HL,rr
                        doADDHL (getReg16 (p));
                        cycles += 11;
                    }
                    break;
                case 2:
                    if (p == 0 && q == 0)
                    {
                        wb ((u16)(regs.B << 8 | regs.C), regs.A);
                        cycles += 7;
                    }
                    else if (p == 1 && q == 0)
                    {
                        wb ((u16)(regs.D << 8 | regs.E), regs.A);
                        cycles += 7;
                    }
                    else if (p == 2 && q == 0)
                    {
                        u16 nn = fetch16 ();
                        ww (nn, getReg16 (2));
                        cycles += 16;
                    }
                    else if (p == 3 && q == 0)
                    {
                        u16 nn = fetch16 ();
                        wb (nn, regs.A);
                        cycles += 13;
                    }
                    else if (p == 0 && q == 1)
                    {
                        regs.A = rb ((u16)(regs.B << 8 | regs.C));
                        cycles += 7;
                    }
                    else if (p == 1 && q == 1)
                    {
                        regs.A = rb ((u16)(regs.D << 8 | regs.E));
                        cycles += 7;
                    }
                    else if (p == 2 && q == 1)
                    {
                        u16 nn = fetch16 ();
                        setReg16 (2, rw (nn));
                        cycles += 16;
                    }
                    else if (p == 3 && q == 1)
                    {
                        u16 nn = fetch16 ();
                        regs.A = rb (nn);
                        cycles += 13;
                    }
                    break;
                case 3:
                    if (q == 0)
                    {
                        setReg16 (p, getReg16 (p) + 1);
                        cycles += 6;
                    }
                    else
                    {
                        setReg16 (p, getReg16 (p) - 1);
                        cycles += 6;
                    }
                    break;
                case 4:
                    if (y == 6)
                    {
                        wb ((u16)(regs.H << 8 | regs.L),
                            doINC8 (rb ((u16)(regs.H << 8 | regs.L))));
                        cycles += 11;
                    }
                    else
                    {
                        regRef (y) = doINC8 (regRef (y));
                        cycles += 4;
                    }
                    break;
                case 5:
                    if (y == 6)
                    {
                        wb ((u16)(regs.H << 8 | regs.L),
                            doDEC8 (rb ((u16)(regs.H << 8 | regs.L))));
                        cycles += 11;
                    }
                    else
                    {
                        regRef (y) = doDEC8 (regRef (y));
                        cycles += 4;
                    }
                    break;
                case 6: {
                    u8 n = fetch8 ();
                    if (y == 6)
                    {
                        wb ((u16)(regs.H << 8 | regs.L), n);
                        cycles += 10;
                    }
                    else
                    {
                        regRef (y) = n;
                        cycles += 7;
                    }
                    break;
                }
                    // z == 7 (RLCA/RRCA/RLA/RRA/DAA/CPL/SCF/CCF) e tratado nos
                    // casos explicitos do switch principal
                }
            }
            else if (x == 3)
            {
                switch (z)
                {
                case 0:
                    // 0xC0..0xF8: RET cc para as oito condicoes
                    // (NZ, Z, NC, C, PO, PE, P, M)
                    if (condCC (y))
                    {
                        regs.PC = pop16 ();
                        cycles += 11;
                    }
                    else
                        cycles += 5;
                    break;
                case 1:
                    if (q == 0)
                    {
                        setReg16AF (p, pop16 ());
                        cycles += 10;
                    }
                    else
                    {
                        if (p == 0)
                        {
                            regs.PC = pop16 ();
                            cycles += 10;
                        }
                        else if (p == 1)
                        {
                            regs.PC = pop16 ();
                            regs.IFF1 = regs.IFF2;
                            cycles += 14;
                        }
                        else if (p == 2)
                        {
                            regs.PC = getReg16 (2);
                            cycles += 4;
                        }
                        else
                        {
                            regs.SP = getReg16 (2);
                            cycles += 6;
                        }
                    }
                    break;
                case 2: {
                    u16 nn = fetch16 ();
                    if (condCC (y))
                        regs.PC = nn;
                    cycles += 10;
                    break;
                }
                case 3:
                    switch (y)
                    {
                    case 0: {
                        u16 nn = fetch16 ();
                        regs.PC = nn;
                        cycles += 10;
                        break;
                    }
                    case 1:
                        executeCB ();
                        break;
                    case 2: {
                        u8 n = fetch8 ();
                        regs.A = ioRead (n);
                        cycles += 11;
                        break;
                    }
                    case 3: {
                        u8 n = fetch8 ();
                        ioWrite (n, regs.A);
                        cycles += 11;
                        break;
                    }
                    case 4: {
                        u16 t = rw (regs.SP);
                        ww (regs.SP, getReg16 (2));
                        setReg16 (2, t);
                        cycles += 19;
                        break;
                    }
                    case 5: {
                        u8 td = regs.D, te = regs.E;
                        regs.D = regs.H;
                        regs.E = regs.L;
                        regs.H = td;
                        regs.L = te;
                        cycles += 4;
                        break;
                    }
                    case 6:
                        regs.IFF1 = regs.IFF2 = false;
                        cycles += 4;
                        break;
                    case 7:
                        regs.IFF1 = regs.IFF2 = true;
                        cycles += 4;
                        break;
                    }
                    break;
                case 4: {
                    u16 nn = fetch16 ();
                    if (condCC (y))
                    {
                        push16 (regs.PC);
                        regs.PC = nn;
                        cycles += 17;
                    }
                    else
                        cycles += 10;
                    break;
                }
                case 5:
                    if (q == 0)
                    {
                        push16 (getReg16AF (p));
                        cycles += 11;
                    }
                    else
                    {
                        if (p == 0)
                        {
                            // 0xCD CALL nn: o endereco de retorno so pode ser
                            // empilhado DEPOIS de consumir os dois bytes do
                            // operando, senao o RET volta para dentro deles
                            u16 nn = fetch16 ();
                            push16 (regs.PC);
                            regs.PC = nn;
                            cycles += 17;
                        }
                        else if (p == 1)
                            executeDDFD (false); // 0xDD
                        else if (p == 2)
                            executeED (); // 0xED
                        else
                            executeDDFD (true); // 0xFD
                    }
                    break;
                case 6: {
                    u8 n = fetch8 ();
                    cycles += 7;
                    switch (y)
                    {
                    case 0:
                        doADD (n);
                        break;
                    case 1:
                        doADD (n, true);
                        break;
                    case 2:
                        doSUB (n);
                        break;
                    case 3:
                        doSUB (n, true);
                        break;
                    case 4:
                        doAND (n);
                        break;
                    case 5:
                        doXOR (n);
                        break;
                    case 6:
                        doOR (n);
                        break;
                    case 7:
                        doCP (n);
                        break;
                    }
                    break;
                }
                case 7:
                    push16 (regs.PC);
                    regs.PC = (u16)(y * 8);
                    cycles += 11;
                    break;
                }
            }
            break;
        }
    }
};