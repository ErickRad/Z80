#pragma once
#include "encoding.hpp"
#include "expr.hpp"
#include "objfmt.hpp"
#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class Assembler
{
  public:
    ObjectFile assemble (const std::string &src, const std::string &filename)
    {
        srcLines_.clear ();
        symTable_.clear ();
        externSyms_.clear ();

        obj_ = {};
        obj_.filename = filename;

        errors_.clear ();
        currentSeg_ = "CODE";
        pc_ = 0;

        ensureSeg (currentSeg_);

        auto lines = splitLines (src);

        parseAll (lines);
        pass1 (lines);
        pass2 (lines);

        if (!errors_.empty ())
        {
            std::string msg;
            for (auto &e : errors_)
                msg += e + "\n";

            throw Z80Error (msg);
        }
        return obj_;
    }

    const std::vector< std::string > &errors () const
    {
        return errors_;
    }
    const std::unordered_map< std::string, u16 > &symbols () const
    {
        return symTable_;
    }

  private:
    std::vector< std::string > srcLines_;
    std::unordered_map< std::string, u16 > symTable_;
    std::unordered_map< std::string, bool > externSyms_;
    ObjectFile obj_;
    std::vector< std::string > errors_;
    std::string currentSeg_;
    u16 pc_;

    static std::string trim (const std::string &s)
    {
        size_t a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos)
            return {};

        size_t b = s.find_last_not_of (" \t\r\n");
        return s.substr (a, b - a + 1);
    }

    static std::string upper (std::string s)
    {
        for (char &c : s)
            c = toupper (c);

        return s;
    }

    static std::string stripComment (const std::string &line)
    {
        bool inStr = false;
        for (size_t i = 0; i < line.size (); ++i)
        {
            if (line[i] == '\'')
                inStr = !inStr;

            if (!inStr && line[i] == ';')
                return line.substr (0, i);
        }
        return line;
    }

    std::vector< std::string > splitLines (const std::string &src)
    {
        std::vector< std::string > out;
        std::istringstream ss (src);
        std::string l;

        while (std::getline (ss, l))
            out.push_back (l);
        return out;
    }

    void ensureSeg (const std::string &name)
    {
        for (auto &s : obj_.segments)
            if (s.name == name)
                return;
        ObjSegment seg;
        seg.name = name;
        seg.hasOrigin = false;
        seg.origin = 0;
        obj_.segments.push_back (seg);
    }

    ObjSegment &getSeg (const std::string &name)
    {
        for (auto &s : obj_.segments)
            if (s.name == name)
                return s;
        throw Z80Error ("Segment not found: " + name);
    }

    void parseAll (const std::vector< std::string > &lines)
    {
        (void)lines;
    }

    ParsedLine parseLine (const std::string &raw, int n)
    {
        ParsedLine pl;
        pl.raw = raw;
        pl.lineNum = n;

        std::string line = trim (stripComment (raw));
        if (line.empty ())
            return pl;

        size_t i = 0;
        if (!line.empty () && (isalpha (line[0]) || line[0] == '_' || line[0] == '.'))
        {
            
            size_t j = 0;
            while (j < line.size () &&
                   (isalnum (line[j]) || line[j] == '_' || line[j] == '.'))
                ++j;

            if (j < line.size () && line[j] == ':')
            {
                pl.label = line.substr (0, j);
                i = j + 1;
                while (i < line.size () && isspace (line[i]))
                    ++i;

                line = line.substr (i);
                i = 0;
            }
            else if (j < line.size () && isspace (line[j]))
            {
                size_t k = j;
                while (k < line.size () && isspace (line[k]))
                    ++k;

                size_t kEnd = k;
                while (kEnd < line.size () && !isspace (line[kEnd]))
                    ++kEnd;

                std::string nextTok = upper (line.substr (k, kEnd - k));
                if (nextTok == "EQU" || nextTok == "=")
                {
                    pl.label = line.substr (0, j);
                    line = line.substr (k)c;
                    i = 0;
                }
            }
        }

        size_t opEnd = i;
        while (opEnd < line.size () && !isspace (line[opEnd]))
            ++opEnd;
        pl.op = upper (trim (line.substr (i, opEnd - i)));
        std::string rest = trim (line.substr (opEnd));

        if (!rest.empty ())
        {
            pl.operands = splitOperands (rest);
        }
        return pl;
    }

    std::vector< std::string > splitOperands (const std::string &s)
    {
        std::vector< std::string > ops;
        std::string cur;
        int depth = 0;
        bool inStr = false;
        for (size_t i = 0; i < s.size (); ++i)
        {
            char c = s[i];
            if (c == '\'')
            {
                inStr = !inStr;
                cur += c;
                continue;
            }
            if (inStr)
            {
                cur += c;
                continue;
            }
            if (c == '(')
            {
                ++depth;
                cur += c;
            }
            else if (c == ')')
            {
                --depth;
                cur += c;
            }
            else if (c == ',' && depth == 0)
            {
                ops.push_back (trim (cur));
                cur.clear ();
            }
            else
                cur += c;
        }
        if (!trim (cur).empty ())
            ops.push_back (trim (cur));
        return ops;
    }

    void addError (int line, const std::string &msg)
    {
        errors_.push_back ("Line " + std::to_string (line) + ": " + msg);
    }

    bool resolveExpr (const std::string &expr, u16 &val, bool second, int lineNum,
                      std::string &sym, RelocType &rtype)
    {
        try
        {
            auto res = ExprEval::eval (expr, symTable_, pc_, second);
            val = res.value;
            sym = res.unresolved;
            rtype = RelocType::ABS16;
            return res.resolved;
        }
        catch (std::exception &e)
        {
            if (second)
                addError (lineNum, e.what ());
            return false;
        }
    }

    void processDirective (const ParsedLine &pl, bool second)
    {
        const std::string &op = pl.op;
        if (op == "ORG")
        {
            u16 val = 0;
            std::string s;
            RelocType rt;
            resolveExpr (pl.operands.empty () ? "0" : pl.operands[0], val, second,
                         pl.lineNum, s, rt);
            pc_ = val;
            if (!second)
            {
                ObjSegment &seg = getSeg (currentSeg_);
                seg.hasOrigin = true;
                seg.origin = val;
            }
        }
        else if (op == "SECTION" || op == "SEGMENT")
        {
            if (!pl.operands.empty ())
            {
                currentSeg_ = pl.operands[0];
                ensureSeg (currentSeg_);
            }
        }
        else if (op == "GLOBAL" || op == "PUBLIC")
        {
            for (auto &sym : pl.operands)
            {
                if (!second)
                {
                    SymbolEntry se;
                    se.name = sym;
                    se.value = 0;
                    se.defined = false;
                    se.global = true;
                    se.segment = currentSeg_;
                    obj_.symbols.push_back (se);
                }
                else
                {
                    for (auto &s : obj_.symbols)
                    {
                        if (s.name == sym)
                        {
                            s.global = true;
                            s.value = symTable_.count (sym) ? symTable_[sym] : 0;
                            s.defined = symTable_.count (sym) > 0;
                        }
                    }
                }
            }
        }
        else if (op == "EQU" || op == "=")
        {
            if (!pl.label.empty () && !pl.operands.empty ())
            {
                u16 val = 0;
                std::string s;
                RelocType rt;
                resolveExpr (pl.operands[0], val, second, pl.lineNum, s, rt);
                symTable_[pl.label] = val;
            }
        }
        else if (op == "DB" || op == "DEFB" || op == "BYTE")
        {
            ObjSegment &seg = getSeg (currentSeg_);
            for (auto &op_ : pl.operands)
            {
                std::string trimmed = trim (op_);
                if (!trimmed.empty () && trimmed[0] == '\'')
                {
                    for (size_t i = 1; i < trimmed.size () && trimmed[i] != '\''; ++i)
                    {
                        if (second)
                            seg.data.push_back ((u8)trimmed[i]);
                        ++pc_;
                    }
                }
                else
                {
                    u16 val = 0;
                    std::string sym_;
                    RelocType rt;
                    bool ok = resolveExpr (trimmed, val, second, pl.lineNum, sym_, rt);
                    if (second)
                    {
                        if (!ok && !sym_.empty ())
                        {
                            RelocEntry re;
                            re.offset = seg.data.size ();
                            re.type = RelocType::ABS8;
                            re.symbol = sym_;
                            re.addend = 0;
                            re.segment = currentSeg_;
                            obj_.relocs.push_back (re);
                        }
                        seg.data.push_back ((u8)val);
                    }
                    ++pc_;
                }
            }
        }
        else if (op == "DW" || op == "DEFW" || op == "WORD")
        {
            ObjSegment &seg = getSeg (currentSeg_);
            for (auto &op_ : pl.operands)
            {
                u16 val = 0;
                std::string sym_;
                RelocType rt;
                bool ok = resolveExpr (trim (op_), val, second, pl.lineNum, sym_, rt);
                if (second)
                {
                    if (!ok && !sym_.empty ())
                    {
                        RelocEntry re;
                        re.offset = seg.data.size ();
                        re.type = RelocType::ABS16;
                        re.symbol = sym_;
                        re.addend = 0;
                        re.segment = currentSeg_;
                        obj_.relocs.push_back (re);
                    }
                    seg.data.push_back ((u8)(val & 0xFF));
                    seg.data.push_back ((u8)(val >> 8));
                }
                pc_ += 2;
            }
        }
        else if (op == "DS" || op == "DEFS" || op == "SPACE")
        {
            u16 n = 0;
            std::string s;
            RelocType rt;
            resolveExpr (pl.operands.empty () ? "0" : pl.operands[0], n, second,
                         pl.lineNum, s, rt);
            u8 fill = 0;
            if (pl.operands.size () > 1)
            {
                u16 fv = 0;
                resolveExpr (pl.operands[1], fv, second, pl.lineNum, s, rt);
                fill = (u8)fv;
            }
            ObjSegment &seg = getSeg (currentSeg_);
            if (second)
                for (u16 i = 0; i < n; ++i)
                    seg.data.push_back (fill);
            pc_ += n;
        }
        else if (op == "EXTERN" || op == "EXTRN")
        {
            for (auto &sym : pl.operands)
            {
                externSyms_[sym] = true;
                SymbolEntry se;
                se.name = sym;
                se.value = 0;
                se.defined = false;
                se.global = true;
                se.segment = "";
                if (!second)
                    obj_.symbols.push_back (se);
            }
        }
    }

    u16 instrSize (const ParsedLine &pl)
    {
        auto sz = estimateSize (pl);
        return sz;
    }

    u16 estimateSize (const ParsedLine &pl)
    {
        const std::string &op = pl.op;
        if (op.empty ())
            return 0;
        if (op == "NOP" || op == "RLCA" || op == "RRCA" || op == "RLA" || op == "RRA" ||
            op == "DAA" || op == "CPL" || op == "SCF" || op == "CCF" || op == "HALT" ||
            op == "EXX" || op == "DI" || op == "EI" || op == "RET" || op == "RLC" ||
            op == "RRC" || op == "RL" || op == "RR" || op == "SLA" || op == "SRA" ||
            op == "SRL")
        {
            if (pl.operands.size () == 1)
            {
                const auto &a = pl.operands[0];
                if (a == "(HL)")
                    return 2;
                if (a.substr (0, 2) == "(I")
                    return 4;
            }
            return 1;
        }
        if (op == "LD")
            return ldSize (pl);
        if (op == "ADD" || op == "ADC" || op == "SUB" || op == "SBC" || op == "AND" ||
            op == "OR" || op == "XOR" || op == "CP")
            return aluSize (pl);
        if (op == "INC" || op == "DEC")
            return incDecSize (pl);
        if (op == "JP")
        {
            if (pl.operands.size () == 1)
            {
                std::string a = upper (pl.operands[0]);
                if (a == "(HL)")
                    return 1;
                if (a == "(IX)")
                    return 2;
                if (a == "(IY)")
                    return 2;
            }
            return 3;
        }
        if (op == "JR")
            return 2;
        if (op == "DJNZ")
            return 2;
        if (op == "CALL")
            return 3;
        if (op == "RET")
            return 1;
        if (op == "RETI" || op == "RETN")
            return 2;
        if (op == "PUSH" || op == "POP")
        {
            std::string a = upper (pl.operands.empty () ? "" : pl.operands[0]);
            if (a == "IX" || a == "IY")
                return 2;
            return 1;
        }
        if (op == "EX")
            return exSize (pl);
        if (op == "BIT" || op == "SET" || op == "RES")
        {
            if (pl.operands.size () >= 2)
            {
                std::string a = upper (pl.operands[1]);
                if (a.substr (0, 2) == "(I")
                    return 4;
                if (a == "(HL)")
                    return 2;
            }
            return 2;
        }
        if (op == "LDIR" || op == "LDDR" || op == "CPIR" || op == "CPDR" ||
            op == "INIR" || op == "INDR" || op == "OTIR" || op == "OTDR" || op == "LDI" ||
            op == "LDD" || op == "CPI" || op == "CPD" || op == "NEG" || op == "IM")
            return 2;
        if (op == "IN")
            return inOutSize (pl);
        if (op == "OUT")
            return inOutSize (pl);
        if (op == "RRCA" || op == "RLCA" || op == "RLA" || op == "RRA")
            return 1;
        if (op == "RST")
            return 1;
        if (op == "DB" || op == "DEFB" || op == "BYTE")
            return 0;
        if (op == "DW" || op == "DEFW" || op == "WORD")
            return 0;
        if (op == "DS" || op == "DEFS" || op == "SPACE")
            return 0;
        if (op == "ORG" || op == "EQU" || op == "=" || op == "SECTION" || op == "SEGMENT")
            return 0;
        if (op == "GLOBAL" || op == "PUBLIC" || op == "EXTERN" || op == "EXTRN")
            return 0;
        if (op == "END")
            return 0;
        return 1;
    }

    u16 ldSize (const ParsedLine &pl)
    {
        if (pl.operands.size () < 2)
            return 1;
        std::string a = upper (pl.operands[0]);
        std::string b = upper (pl.operands[1]);
        if (a == "I" || a == "R")
            return 2;
        if (b == "I" || b == "R")
            return 2;
        if (a == "IX" || a == "IY")
        {
            if (b[0] == '(')
                return 4;
            return 4;
        }
        if (b == "IX" || b == "IY")
        {
            if (a[0] == '(')
                return 4;
            return 4;
        }
        if (a == "SP")
        {
            if (b == "HL")
                return 1;
            if (b == "IX" || b == "IY")
                return 2;
            if (b[0] == '(')
                return 4;
            return 3;
        }
        if (a == "HL" || a == "BC" || a == "DE")
        {
            if (b[0] == '(')
                return 3;
            return 3;
        }
        if (a == "(HL)")
        {
            if (RegCode::isR8 (b))
                return 1;
            return 2;
        }
        if (a[0] == '(')
        {
            if (b == "HL" || b == "BC" || b == "DE" || b == "SP")
                return 3;
            if (b == "IX" || b == "IY")
                return 4;
            return 3;
        }
        if (RegCode::isR8 (a) && RegCode::isR8 (b))
            return 1;
        if (RegCode::isR8 (a) && b[0] == '(')
        {
            if (b.substr (0, 3) == "(IX" || b.substr (0, 3) == "(IY")
                return 3;
            return 1;
        }
        if (a[0] == '(' && (a.substr (1, 2) == "IX" || a.substr (1, 2) == "IY"))
            return 3;
        if (RegCode::isR8 (a))
            return 2;
        return 2;
    }

    u16 aluSize (const ParsedLine &pl)
    {
        size_t opIdx = (pl.op == "ADD" || pl.op == "ADC" || pl.op == "SBC") ? 1 : 0;
        if (pl.operands.size () <= opIdx)
            return 1;
        std::string a = upper (pl.operands[opIdx]);
        if (a == "HL" || a == "IX" || a == "IY")
            return 2;
        if (RegCode::isR8 (a))
            return 1;
        if (a.substr (0, 2) == "(I")
            return 3;
        return 2;
    }

    u16 incDecSize (const ParsedLine &pl)
    {
        if (pl.operands.empty ())
            return 1;
        std::string a = upper (pl.operands[0]);
        if (a == "IX" || a == "IY")
            return 2;
        if (a.substr (0, 3) == "(IX" || a.substr (0, 3) == "(IY")
            return 3;
        return 1;
    }

    u16 exSize (const ParsedLine &pl)
    {
        if (pl.operands.size () < 2)
            return 1;
        std::string a = upper (pl.operands[0]);
        std::string b = upper (pl.operands[1]);
        if (a == "(SP)")
        {
            if (b == "IX" || b == "IY")
                return 2;
            return 1;
        }
        return 1;
    }

    u16 inOutSize (const ParsedLine &pl)
    {
        if (pl.operands.size () < 2)
            return 2;
        std::string a = upper (pl.operands[0]);
        if (a == "(C)")
            return 2;
        return 2;
    }

    void pass1 (const std::vector< std::string > &lines)
    {
        currentSeg_ = "CODE";
        pc_ = 0;
        ensureSeg (currentSeg_);
        for (int i = 0; i < (int)lines.size (); ++i)
        {
            auto pl = parseLine (lines[i], i + 1);
            if (!pl.label.empty () && pl.op != "EQU" && pl.op != "=")
            {
                symTable_[pl.label] = pc_;
                SymbolEntry se;
                se.name = pl.label;
                se.value = pc_;
                se.defined = true;
                se.global = false;
                se.segment = currentSeg_;
                bool found = false;
                for (auto &s : obj_.symbols)
                    if (s.name == pl.label)
                    {
                        s.value = pc_;
                        s.defined = true;
                        s.segment = currentSeg_;
                        found = true;
                    }
                if (!found)
                    obj_.symbols.push_back (se);
            }
            if (isDirective (pl.op))
            {
                processDirective (pl, false);
                continue;
            }
            if (!pl.op.empty ())
                pc_ += instrSize (pl);
        }
    }

    void pass2 (const std::vector< std::string > &lines)
    {
        currentSeg_ = "CODE";
        pc_ = 0;
        for (int i = 0; i < (int)lines.size (); ++i)
        {
            auto pl = parseLine (lines[i], i + 1);
            if (isDirective (pl.op))
            {
                processDirective (pl, true);
                continue;
            }
            if (pl.op.empty ())
                continue;
            try
            {
                auto enc = encodeInstr (pl);
                ObjSegment &seg = getSeg (currentSeg_);
                for (auto &re : enc.relocs_)
                {
                    re.offset += seg.data.size ();
                    obj_.relocs.push_back (re);
                }
                for (u8 b : enc.bytes)
                    seg.data.push_back (b);
                pc_ += (u16)enc.bytes.size ();
            }
            catch (std::exception &e)
            {
                addError (pl.lineNum, e.what ());
                pc_ += instrSize (pl);
            }
        }
        for (auto &sym : obj_.symbols)
        {
            if (sym.global && symTable_.count (sym.name))
            {
                sym.value = symTable_[sym.name];
                sym.defined = true;
            }
        }
    }

    bool isDirective (const std::string &op)
    {
        static const std::vector< std::string > dirs = {
            "ORG",    "DB",     "DW",     "DS",    "DEFB", "DEFW",    "DEFS",
            "BYTE",   "WORD",   "SPACE",  "EQU",   "=",    "SECTION", "SEGMENT",
            "GLOBAL", "PUBLIC", "EXTERN", "EXTRN", "END"
        };
        for (auto &d : dirs)
            if (d == op)
                return true;
        return false;
    }

    struct EncOut
    {
        std::vector< u8 > bytes;
        std::vector< RelocEntry > relocs_;
    };

    EncOut encodeInstr (const ParsedLine &pl)
    {
        EncOut out;
        const std::string &op = pl.op;
        auto ops = pl.operands;
        for (auto &o : ops)
            o = trim (upper (o));

        auto emit = [&] (std::initializer_list< u8 > bs) {
            for (u8 b : bs)
                out.bytes.push_back (b);
        };
        auto o0 = ops.size () > 0 ? ops[0] : "";
        auto o1 = ops.size () > 1 ? ops[1] : "";

        auto evalU16 = [&] (const std::string &e, std::string &sym,
                            RelocType &rt) -> u16 {
            u16 val = 0;
            bool ok = resolveExpr (e, val, true, pl.lineNum, sym, rt);
            (void)ok;
            return val;
        };
        auto evalU8 = [&] (const std::string &e) -> u8 {
            u16 val = 0;
            std::string s;
            RelocType rt;
            resolveExpr (e, val, true, pl.lineNum, s, rt);
            return (u8)val;
        };
        auto addReloc16 = [&] (const std::string &sym, size_t prefixLen = 0,
                               i16 addend = 0) {
            if (!sym.empty ())
            {
                RelocEntry re;
                re.offset = out.bytes.size () + prefixLen;
                re.type = RelocType::ABS16;
                re.symbol = sym;
                re.addend = addend;
                re.segment = currentSeg_;
                out.relocs_.push_back (re);
            }
        };
        auto addRelocRel = [&] (const std::string &sym, size_t prefixLen = 0,
                                i16 addend = 0) {
            if (!sym.empty ())
            {
                RelocEntry re;
                re.offset = out.bytes.size () + prefixLen;
                re.type = RelocType::REL8;
                re.symbol = sym;
                re.addend = addend;
                re.segment = currentSeg_;
                out.relocs_.push_back (re);
            }
        };

        int r0 = RegCode::r8 (o0);
        int r1 = RegCode::r8 (o1);

        if (op == "NOP")
        {
            emit ({ 0x00 });
            return out;
        }
        if (op == "HALT")
        {
            emit ({ 0x76 });
            return out;
        }
        if (op == "DI")
        {
            emit ({ 0xF3 });
            return out;
        }
        if (op == "EI")
        {
            emit ({ 0xFB });
            return out;
        }
        if (op == "RLCA")
        {
            emit ({ 0x07 });
            return out;
        }
        if (op == "RRCA")
        {
            emit ({ 0x0F });
            return out;
        }
        if (op == "RLA")
        {
            emit ({ 0x17 });
            return out;
        }
        if (op == "RRA")
        {
            emit ({ 0x1F });
            return out;
        }
        if (op == "DAA")
        {
            emit ({ 0x27 });
            return out;
        }
        if (op == "CPL")
        {
            emit ({ 0x2F });
            return out;
        }
        if (op == "SCF")
        {
            emit ({ 0x37 });
            return out;
        }
        if (op == "CCF")
        {
            emit ({ 0x3F });
            return out;
        }
        if (op == "EXX")
        {
            emit ({ 0xD9 });
            return out;
        }
        if (op == "NEG")
        {
            emit ({ 0xED, 0x44 });
            return out;
        }
        if (op == "RETI")
        {
            emit ({ 0xED, 0x4D });
            return out;
        }
        if (op == "RETN")
        {
            emit ({ 0xED, 0x45 });
            return out;
        }
        if (op == "LDIR")
        {
            emit ({ 0xED, 0xB0 });
            return out;
        }
        if (op == "LDDR")
        {
            emit ({ 0xED, 0xB8 });
            return out;
        }
        if (op == "LDI")
        {
            emit ({ 0xED, 0xA0 });
            return out;
        }
        if (op == "LDD")
        {
            emit ({ 0xED, 0xA8 });
            return out;
        }
        if (op == "CPIR")
        {
            emit ({ 0xED, 0xB1 });
            return out;
        }
        if (op == "CPDR")
        {
            emit ({ 0xED, 0xB9 });
            return out;
        }
        if (op == "CPI")
        {
            emit ({ 0xED, 0xA1 });
            return out;
        }
        if (op == "CPD")
        {
            emit ({ 0xED, 0xA9 });
            return out;
        }
        if (op == "OTIR")
        {
            emit ({ 0xED, 0xB3 });
            return out;
        }
        if (op == "OTDR")
        {
            emit ({ 0xED, 0xBB });
            return out;
        }
        if (op == "INIR")
        {
            emit ({ 0xED, 0xB2 });
            return out;
        }
        if (op == "INDR")
        {
            emit ({ 0xED, 0xBA });
            return out;
        }

        if (op == "IM")
        {
            u8 m = evalU8 (o0);
            if (m == 0)
                emit ({ 0xED, 0x46 });
            else if (m == 1)
                emit ({ 0xED, 0x56 });
            else
                emit ({ 0xED, 0x5E });
            return out;
        }

        if (op == "RST")
        {
            u8 v = evalU8 (o0) & 0x38;
            emit ({ (u8)(0xC7 | v) });
            return out;
        }

        if (op == "LD")
        {
            if (o0 == "I" && o1 == "A")
            {
                emit ({ 0xED, 0x47 });
                return out;
            }
            if (o0 == "R" && o1 == "A")
            {
                emit ({ 0xED, 0x4F });
                return out;
            }
            if (o0 == "A" && o1 == "I")
            {
                emit ({ 0xED, 0x57 });
                return out;
            }
            if (o0 == "A" && o1 == "R")
            {
                emit ({ 0xED, 0x5F });
                return out;
            }
            if (o0 == "SP" && o1 == "HL")
            {
                emit ({ 0xF9 });
                return out;
            }
            if (o0 == "SP" && o1 == "IX")
            {
                emit ({ 0xDD, 0xF9 });
                return out;
            }
            if (o0 == "SP" && o1 == "IY")
            {
                emit ({ 0xFD, 0xF9 });
                return out;
            }

            if (o0.substr (0, 3) == "(IX")
            {
                i8 d = 0;
                if (o0.size () > 4)
                {
                    auto sub = o0.substr (3);
                    if (sub[0] == '+')
                        d = (i8)evalU8 (sub.substr (1));
                    else if (sub[0] == '-')
                        d = -(i8)evalU8 (sub.substr (1));
                }
                if (r1 >= 0)
                {
                    emit ({ 0xDD, (u8)(0x70 | (u8)r1), (u8)d });
                    return out;
                }
                emit ({ 0xDD, 0x36, (u8)d, evalU8 (o1) });
                return out;
            }
            if (o0.substr (0, 3) == "(IY")
            {
                i8 d = 0;
                if (o0.size () > 4)
                {
                    auto sub = o0.substr (3);
                    if (sub[0] == '+')
                        d = (i8)evalU8 (sub.substr (1));
                    else if (sub[0] == '-')
                        d = -(i8)evalU8 (sub.substr (1));
                }
                if (r1 >= 0)
                {
                    emit ({ 0xFD, (u8)(0x70 | (u8)r1), (u8)d });
                    return out;
                }
                emit ({ 0xFD, 0x36, (u8)d, evalU8 (o1) });
                return out;
            }
            if (r0 >= 0 && o1.substr (0, 3) == "(IX")
            {
                i8 d = 0;
                if (o1.size () > 4)
                {
                    auto sub = o1.substr (3);
                    if (sub[0] == '+')
                        d = (i8)evalU8 (sub.substr (1));
                    else if (sub[0] == '-')
                        d = -(i8)evalU8 (sub.substr (1));
                }
                emit ({ 0xDD, (u8)(0x46 | (u8)r0 << 3), (u8)d });
                return out;
            }
            if (r0 >= 0 && o1.substr (0, 3) == "(IY")
            {
                i8 d = 0;
                if (o1.size () > 4)
                {
                    auto sub = o1.substr (3);
                    if (sub[0] == '+')
                        d = (i8)evalU8 (sub.substr (1));
                    else if (sub[0] == '-')
                        d = -(i8)evalU8 (sub.substr (1));
                }
                emit ({ 0xFD, (u8)(0x46 | (u8)r0 << 3), (u8)d });
                return out;
            }
            if (r0 >= 0 && r1 >= 0)
            {
                emit ({ (u8)(0x40 | (u8)r0 << 3 | (u8)r1) });
                return out;
            }
            if (r0 >= 0 && o1[0] == '(')
            {
                if (o1 == "(BC)")
                {
                    emit ({ 0x0A });
                    return out;
                }
                if (o1 == "(DE)")
                {
                    emit ({ 0x1A });
                    return out;
                }
                if (o1 == "(HL)")
                {
                    emit ({ (u8)(0x46 | (u8)r0 << 3) });
                    return out;
                }
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o1.substr (1, o1.size () - 2), sym2, rt);
                addReloc16 (sym2, 1, (i16)addr);
                emit ({ 0x3A, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if (r0 >= 0 && o1[0] != '(')
            {
                emit ({ (u8)(0x06 | (u8)r0 << 3), evalU8 (o1) });
                return out;
            }
            if (o0 == "(HL)" && r1 >= 0)
            {
                emit ({ (u8)(0x70 | (u8)r1) });
                return out;
            }
            if (o0 == "(HL)")
            {
                emit ({ 0x36, evalU8 (o1) });
                return out;
            }
            if (o0 == "(BC)" && o1 == "A")
            {
                emit ({ 0x02 });
                return out;
            }
            if (o0 == "(DE)" && o1 == "A")
            {
                emit ({ 0x12 });
                return out;
            }
            if (o0[0] == '(' && o1 == "A")
            {
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o0.substr (1, o0.size () - 2), sym2, rt);
                addReloc16 (sym2, 1, (i16)addr);
                emit ({ 0x32, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if (o0[0] == '(' && (o1 == "HL" || o1 == "BC" || o1 == "DE" || o1 == "SP"))
            {
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o0.substr (1, o0.size () - 2), sym2, rt);
                u8 opc = o1 == "HL" ? 0x22 : o1 == "BC" ? 0x43 : o1 == "DE" ? 0x53 : 0x73;
                u8 pfx = (o1 == "BC" || o1 == "DE" || o1 == "SP") ? 0xED : 0;
                addReloc16 (sym2, pfx ? 2 : 1, (i16)addr);
                if (pfx)
                    emit ({ pfx, opc, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                else
                    emit ({ opc, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if (o0[0] == '(' && (o1 == "IX" || o1 == "IY"))
            {
                u8 pfx = o1 == "IX" ? 0xDD : 0xFD;
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o0.substr (1, o0.size () - 2), sym2, rt);
                addReloc16 (sym2, 2, (i16)addr);
                emit ({ pfx, 0x22, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if ((o0 == "HL" || o0 == "BC" || o0 == "DE" || o0 == "SP") && o1[0] == '(')
            {
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o1.substr (1, o1.size () - 2), sym2, rt);
                u8 opc = o0 == "HL" ? 0x2A : o0 == "BC" ? 0x4B : o0 == "DE" ? 0x5B : 0x7B;
                u8 pfx = (o0 == "BC" || o0 == "DE" || o0 == "SP") ? 0xED : 0;
                addReloc16 (sym2, pfx ? 2 : 1, (i16)addr);
                if (pfx)
                    emit ({ pfx, opc, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                else
                    emit ({ opc, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if ((o0 == "IX" || o0 == "IY") && o1[0] == '(')
            {
                u8 pfx = o0 == "IX" ? 0xDD : 0xFD;
                std::string sym2;
                RelocType rt;
                u16 addr = evalU16 (o1.substr (1, o1.size () - 2), sym2, rt);
                addReloc16 (sym2, 2, (i16)addr);
                emit ({ pfx, 0x2A, (u8)(addr & 0xFF), (u8)(addr >> 8) });
                return out;
            }
            if (o0 == "BC" || o0 == "DE" || o0 == "HL" || o0 == "SP")
            {
                int rpi = RegCode::rp (o0);
                std::string sym2;
                RelocType rt;
                u16 v = evalU16 (o1, sym2, rt);
                addReloc16 (sym2, 1, (i16)v);
                emit ({ (u8)(0x01 | (u8)rpi << 4), (u8)(v & 0xFF), (u8)(v >> 8) });
                return out;
            }
            if (o0 == "IX")
            {
                std::string sym2;
                RelocType rt;
                u16 v = evalU16 (o1, sym2, rt);
                addReloc16 (sym2, 2, (i16)v);
                emit ({ 0xDD, 0x21, (u8)(v & 0xFF), (u8)(v >> 8) });
                return out;
            }
            if (o0 == "IY")
            {
                std::string sym2;
                RelocType rt;
                u16 v = evalU16 (o1, sym2, rt);
                addReloc16 (sym2, 2, (i16)v);
                emit ({ 0xFD, 0x21, (u8)(v & 0xFF), (u8)(v >> 8) });
                return out;
            }
            throw Z80Error ("Unknown LD variant: " + o0 + "," + o1);
        }

        if (op == "ADD" || op == "ADC" || op == "SUB" || op == "SBC" || op == "AND" ||
            op == "OR" || op == "XOR" || op == "CP")
        {
            static std::unordered_map< std::string, u8 > baseOp = {
                { "ADD", 0x80 }, { "ADC", 0x88 }, { "SUB", 0x90 }, { "SBC", 0x98 },
                { "AND", 0xA0 }, { "XOR", 0xA8 }, { "OR", 0xB0 },  { "CP", 0xB8 }
            };
            std::string src_ =
                (op == "ADD" || op == "ADC" || op == "SBC") && ops.size () >= 2 ? o1 : o0;
            if (op == "ADD" || op == "ADC" || op == "SBC")
            {
                if ((o0 == "HL" || o0 == "IX" || o0 == "IY") && ops.size () >= 2)
                {
                    int rpi = RegCode::rp (o1);
                    if (rpi < 0 && o1 == "SP")
                        rpi = 3;
                    if (op == "ADD")
                    {
                        if (o0 == "IX")
                        {
                            emit ({ 0xDD, (u8)(0x09 | (u8)rpi << 4) });
                            return out;
                        }
                        if (o0 == "IY")
                        {
                            emit ({ 0xFD, (u8)(0x09 | (u8)rpi << 4) });
                            return out;
                        }
                        emit ({ (u8)(0x09 | (u8)rpi << 4) });
                        return out;
                    }
                    if (op == "ADC")
                    {
                        emit ({ 0xED, (u8)(0x4A | (u8)rpi << 4) });
                        return out;
                    }
                    if (op == "SBC")
                    {
                        emit ({ 0xED, (u8)(0x42 | (u8)rpi << 4) });
                        return out;
                    }
                }
            }
            u8 base = baseOp[op];
            int rs = RegCode::r8 (src_);
            if (rs >= 0)
            {
                emit ({ (u8)(base | rs) });
                return out;
            }
            if (src_ == "(HL)")
            {
                emit ({ (u8)(base | 6) });
                return out;
            }
            if (src_.substr (0, 3) == "(IX")
            {
                i8 d = 0;
                if (src_.size () > 4)
                {
                    auto s = src_.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                emit ({ 0xDD, (u8)(base | 6), (u8)d });
                return out;
            }
            if (src_.substr (0, 3) == "(IY")
            {
                i8 d = 0;
                if (src_.size () > 4)
                {
                    auto s = src_.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                emit ({ 0xFD, (u8)(base | 6), (u8)d });
                return out;
            }
            emit ({ (u8)(base | 0x46), evalU8 (src_) });
            return out;
        }

        if (op == "INC" || op == "DEC")
        {
            u8 base = op == "INC" ? 0x04 : 0x05;
            if (o0 == "IX")
            {
                emit ({ 0xDD, (u8)(op == "INC" ? 0x23 : 0x2B) });
                return out;
            }
            if (o0 == "IY")
            {
                emit ({ 0xFD, (u8)(op == "INC" ? 0x23 : 0x2B) });
                return out;
            }
            int rpi = RegCode::rp (o0);
            if (rpi >= 0)
            {
                emit ({ (u8)((u8)(op == "INC" ? 0x03 : 0x0B) | (u8)(rpi << 4)) });
                return out;
            }
            int ri = RegCode::r8 (o0);
            if (ri >= 0)
            {
                emit ({ (u8)(base | (u8)(ri << 3)) });
                return out;
            }
            if (o0 == "(HL)")
            {
                emit ({ (u8)(base | 0x30) });
                return out;
            }
            if (o0.substr (0, 3) == "(IX")
            {
                i8 d = 0;
                if (o0.size () > 4)
                {
                    auto s = o0.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                emit ({ 0xDD, (u8)(op == "INC" ? 0x34 : 0x35), (u8)d });
                return out;
            }
            if (o0.substr (0, 3) == "(IY")
            {
                i8 d = 0;
                if (o0.size () > 4)
                {
                    auto s = o0.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                emit ({ 0xFD, (u8)(op == "INC" ? 0x34 : 0x35), (u8)d });
                return out;
            }
            throw Z80Error ("Unknown INC/DEC: " + o0);
        }

        if (op == "JP")
        {
            static std::unordered_map< std::string, u8 > cc = { { "NZ", 0 }, { "Z", 1 },
                                                                { "NC", 2 }, { "C", 3 },
                                                                { "PO", 4 }, { "PE", 5 },
                                                                { "P", 6 },  { "M", 7 } };
            if (o0 == "(HL)")
            {
                emit ({ 0xE9 });
                return out;
            }
            if (o0 == "(IX)")
            {
                emit ({ 0xDD, 0xE9 });
                return out;
            }
            if (o0 == "(IY)")
            {
                emit ({ 0xFD, 0xE9 });
                return out;
            }
            if (ops.size () == 2 && cc.count (o0))
            {
                std::string sym2;
                RelocType rt;
                u16 a = evalU16 (o1, sym2, rt);
                addReloc16 (sym2, 1, (i16)a);
                emit ({ (u8)(0xC2 | (cc[o0] << 3)), (u8)(a & 0xFF), (u8)(a >> 8) });
                return out;
            }
            std::string sym2;
            RelocType rt;
            u16 a = evalU16 (o0, sym2, rt);
            addReloc16 (sym2, 1, (i16)a);
            emit ({ 0xC3, (u8)(a & 0xFF), (u8)(a >> 8) });
            return out;
        }

        if (op == "JR")
        {
            static std::unordered_map< std::string, u8 > cc = {
                { "NZ", 0x20 }, { "Z", 0x28 }, { "NC", 0x30 }, { "C", 0x38 }
            };
            if (ops.size () == 2 && cc.count (o0))
            {
                std::string sym2;
                RelocType rt;
                u16 tgt = 0;
                resolveExpr (o1, tgt, true, pl.lineNum, sym2, rt);
                i8 d = (i8)((i16)tgt - (i16)(pc_ + 2));
                if (!sym2.empty ())
                    addRelocRel (sym2, 1, (i16)tgt);
                emit ({ cc[o0], (u8)d });
                return out;
            }
            std::string sym2;
            RelocType rt;
            u16 tgt = 0;
            resolveExpr (o0, tgt, true, pl.lineNum, sym2, rt);
            i8 d = (i8)((i16)tgt - (i16)(pc_ + 2));
            if (!sym2.empty ())
                addRelocRel (sym2, 1, (i16)tgt);
            emit ({ 0x18, (u8)d });
            return out;
        }

        if (op == "DJNZ")
        {
            std::string sym2;
            RelocType rt;
            u16 tgt = 0;
            resolveExpr (o0, tgt, true, pl.lineNum, sym2, rt);
            i8 d = (i8)((i16)tgt - (i16)(pc_ + 2));
            if (!sym2.empty ())
                addRelocRel (sym2, 1, (i16)tgt);
            emit ({ 0x10, (u8)d });
            return out;
        }

        if (op == "CALL")
        {
            static std::unordered_map< std::string, u8 > cc = { { "NZ", 0 }, { "Z", 1 },
                                                                { "NC", 2 }, { "C", 3 },
                                                                { "PO", 4 }, { "PE", 5 },
                                                                { "P", 6 },  { "M", 7 } };
            if (ops.size () == 2 && cc.count (o0))
            {
                std::string sym2;
                RelocType rt;
                u16 a = evalU16 (o1, sym2, rt);
                addReloc16 (sym2, 1, (i16)a);
                emit ({ (u8)(0xC4 | (cc[o0] << 3)), (u8)(a & 0xFF), (u8)(a >> 8) });
                return out;
            }
            std::string sym2;
            RelocType rt;
            u16 a = evalU16 (o0, sym2, rt);
            addReloc16 (sym2, 1, (i16)a);
            emit ({ 0xCD, (u8)(a & 0xFF), (u8)(a >> 8) });
            return out;
        }

        if (op == "RET")
        {
            static std::unordered_map< std::string, u8 > cc = {
                { "NZ", 0xC0 }, { "Z", 0xC8 },  { "NC", 0xD0 }, { "C", 0xD8 },
                { "PO", 0xE0 }, { "PE", 0xE8 }, { "P", 0xF0 },  { "M", 0xF8 }
            };
            if (!o0.empty () && cc.count (o0))
            {
                emit ({ cc[o0] });
                return out;
            }
            emit ({ 0xC9 });
            return out;
        }

        if (op == "PUSH" || op == "POP")
        {
            u8 base = op == "PUSH" ? 0xC5 : 0xC1;
            if (o0 == "IX")
            {
                emit ({ 0xDD, (u8)(op == "PUSH" ? 0xE5 : 0xE1) });
                return out;
            }
            if (o0 == "IY")
            {
                emit ({ 0xFD, (u8)(op == "PUSH" ? 0xE5 : 0xE1) });
                return out;
            }
            int rpi = RegCode::rpAF (o0);
            if (rpi >= 0)
            {
                emit ({ (u8)(base | (u8)rpi << 4) });
                return out;
            }
            throw Z80Error ("Unknown PUSH/POP: " + o0);
        }

        if (op == "EX")
        {
            if (o0 == "AF" && o1 == "AF'")
            {
                emit ({ 0x08 });
                return out;
            }
            if (o0 == "DE" && o1 == "HL")
            {
                emit ({ 0xEB });
                return out;
            }
            if (o0 == "(SP)" && o1 == "HL")
            {
                emit ({ 0xE3 });
                return out;
            }
            if (o0 == "(SP)" && o1 == "IX")
            {
                emit ({ 0xDD, 0xE3 });
                return out;
            }
            if (o0 == "(SP)" && o1 == "IY")
            {
                emit ({ 0xFD, 0xE3 });
                return out;
            }
            throw Z80Error ("Unknown EX: " + o0 + "," + o1);
        }

        auto encodeCB = [&] (u8 base) -> EncOut {
            EncOut r;
            std::string reg_ = o0;
            if (base == 0x40 || base == 0x80 || base == 0xC0)
            {
                reg_ = o1;
            }
            int ri = RegCode::r8 (reg_);
            if (ri < 0)
                ri = 6;
            if (reg_.substr (0, 3) == "(IX")
            {
                i8 d = 0;
                if (reg_.size () > 4)
                {
                    auto s = reg_.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                u8 bit_ = 0;
                if (base >= 0x40)
                {
                    u16 bv = 0;
                    std::string ss;
                    RelocType rt;
                    resolveExpr (o0, bv, true, pl.lineNum, ss, rt);
                    bit_ = (u8)(bv & 7);
                }
                r.bytes = { 0xDD, 0xCB, (u8)d, (u8)(base | (u8)bit_ << 3 | 6) };
                return r;
            }
            if (reg_.substr (0, 3) == "(IY")
            {
                i8 d = 0;
                if (reg_.size () > 4)
                {
                    auto s = reg_.substr (3);
                    if (s[0] == '+')
                        d = (i8)evalU8 (s.substr (1));
                    else if (s[0] == '-')
                        d = -(i8)evalU8 (s.substr (1));
                }
                u8 bit_ = 0;
                if (base >= 0x40)
                {
                    u16 bv = 0;
                    std::string ss;
                    RelocType rt;
                    resolveExpr (o0, bv, true, pl.lineNum, ss, rt);
                    bit_ = (u8)(bv & 7);
                }
                r.bytes = { 0xFD, 0xCB, (u8)d, (u8)(base | (u8)bit_ << 3 | 6) };
                return r;
            }
            u8 bit_ = 0;
            if (base >= 0x40)
            {
                u16 bv = 0;
                std::string ss;
                RelocType rt;
                resolveExpr (o0, bv, true, pl.lineNum, ss, rt);
                bit_ = (u8)(bv & 7);
            }
            r.bytes = { 0xCB, (u8)(base | (u8)bit_ << 3 | (u8)ri) };
            return r;
        };

        if (op == "RLC")
            return encodeCB (0x00);
        if (op == "RRC")
            return encodeCB (0x08);
        if (op == "RL")
            return encodeCB (0x10);
        if (op == "RR")
            return encodeCB (0x18);
        if (op == "SLA")
            return encodeCB (0x20);
        if (op == "SRA")
            return encodeCB (0x28);
        if (op == "SRL")
            return encodeCB (0x38);
        if (op == "BIT")
            return encodeCB (0x40);
        if (op == "RES")
            return encodeCB (0x80);
        if (op == "SET")
            return encodeCB (0xC0);

        if (op == "IN")
        {
            if (o1 == "(C)")
            {
                int ri = RegCode::r8 (o0);
                if (ri >= 0)
                {
                    emit ({ 0xED, (u8)(0x40 | (u8)ri << 3) });
                    return out;
                }
            }
            emit ({ 0xDB, evalU8 (o1.substr (1, o1.size () - 2)) });
            return out;
        }
        if (op == "OUT")
        {
            if (o0 == "(C)")
            {
                int ri = RegCode::r8 (o1);
                if (ri >= 0)
                {
                    emit ({ 0xED, (u8)(0x41 | (u8)ri << 3) });
                    return out;
                }
            }
            emit ({ 0xD3, evalU8 (o0.substr (1, o0.size () - 2)) });
            return out;
        }

        throw Z80Error ("Unknown instruction: " + op);
    }
};