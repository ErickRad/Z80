#pragma once
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "types.hpp"

class ExprEval
{
  public:
    using SymMap = std::unordered_map< std::string, u16 >;
    using ExternSet = std::unordered_map< std::string, bool >;

    struct Result
    {
        u16 value;
        bool resolved;
        std::string unresolved;
    };

    static Result eval (const std::string &expr, const SymMap &syms, u16 pc,
                        bool secondPass, const ExternSet *externs = nullptr)
    {
        std::string e = trim (expr);
        Result res;
        res.resolved = true;
        res.value = 0;

        if (e.empty ())
            throw std::runtime_error ("Empty expression");

        try
        {
            size_t pos = 0;
            res.value = parseExpr (e, pos, syms, pc, secondPass, res.unresolved,
                                   res.resolved, externs);
        }
        catch (std::runtime_error &ex)
        {
            if (secondPass)
                throw;

            res.resolved = false;
        }

        return res;
    }

  private:
    static std::string trim (const std::string &s)
    {
        size_t a = s.find_first_not_of (" \t");
        if (a == std::string::npos)
            return {};

        size_t b = s.find_last_not_of (" \t");

        return s.substr (a, b - a + 1);
    }

    static u16 parseExpr (const std::string &e, size_t &pos, const SymMap &syms, u16 pc,
                          bool second, std::string &unres, bool &resolved,
                          const ExternSet *externs)
    {
        u16 val = parseTerm (e, pos, syms, pc, second, unres, resolved, externs);

        while (pos < e.size ())
        {
            char op = e[pos];

            if (op != '+' && op != '-' && op != '|' && op != '^')
                break;

            ++pos;
            u16 rhs = parseTerm (e, pos, syms, pc, second, unres, resolved, externs);

            if (op == '+')
                val += rhs;

            else if (op == '-')
                val -= rhs;

            else if (op == '|')
                val |= rhs;

            else if (op == '^')
                val ^= rhs;
        }

        return val;
    }

    static u16 parseTerm (const std::string &e, size_t &pos, const SymMap &syms, u16 pc,
                          bool second, std::string &unres, bool &resolved,
                          const ExternSet *externs)
    {
        u16 val = parseFactor (e, pos, syms, pc, second, unres, resolved, externs);

        while (pos < e.size ())
        {
            char op = e[pos];

            if (op != '*' && op != '/' && op != '&' && op != '<' && op != '>')
                break;

            if ((op == '<' || op == '>') && (pos + 1 >= e.size () || e[pos + 1] != op))
                break;

            if (op == '<' || op == '>')
            {
                ++pos;
                ++pos;
                u16 rhs =
                    parseFactor (e, pos, syms, pc, second, unres, resolved, externs);

                if (op == '<')
                    val = (u16)(val << rhs);
                else
                    val = (u16)(val >> rhs);
            }
            else
            {
                ++pos;
                u16 rhs =
                    parseFactor (e, pos, syms, pc, second, unres, resolved, externs);

                if (op == '*')
                    val *= rhs;

                else if (op == '/')
                {
                    if (rhs == 0)
                        throw std::runtime_error ("Div by zero");
                    val /= rhs;
                }
                else if (op == '&')
                    val &= rhs;
            }
        }

        return val;
    }

    static u16 parseFactor (const std::string &e, size_t &pos, const SymMap &syms, u16 pc,
                            bool second, std::string &unres, bool &resolved,
                            const ExternSet *externs)
    {
        while (pos < e.size () && e[pos] == ' ')
            ++pos;

        if (pos >= e.size ())
            throw std::runtime_error ("Unexpected end");

        if (e[pos] == '(')
        {
            ++pos;
            u16 v = parseExpr (e, pos, syms, pc, second, unres, resolved, externs);

            if (pos < e.size () && e[pos] == ')')
                ++pos;

            return v;
        }

        if (e[pos] == '-')
        {
            ++pos;
            return (u16)(-(i16)parseFactor (e, pos, syms, pc, second, unres, resolved,
                                            externs));
        }
        if (e[pos] == '+')
        {
            ++pos;
            return parseFactor (e, pos, syms, pc, second, unres, resolved, externs);
        }

        if (e[pos] == '$' && pos + 1 < e.size () && isxdigit (e[pos + 1]))
        {
            ++pos;
            size_t start = pos;

            while (pos < e.size () && isxdigit (e[pos]))
                ++pos;

            return (u16)strtoul (e.substr (start, pos - start).c_str (), nullptr, 16);
        }

        if (e[pos] == '0' && pos + 1 < e.size () &&
            (e[pos + 1] == 'x' || e[pos + 1] == 'X'))
        {
            pos += 2;
            size_t start = pos;

            while (pos < e.size () && isxdigit (e[pos]))
                ++pos;

            return (u16)strtoul (e.substr (start, pos - start).c_str (), nullptr, 16);
        }

        if (e[pos] == '%' && pos + 1 < e.size () &&
            (e[pos + 1] == '0' || e[pos + 1] == '1'))
        {
            ++pos;
            size_t start = pos;

            while (pos < e.size () && (e[pos] == '0' || e[pos] == '1'))
                ++pos;

            return (u16)strtoul (e.substr (start, pos - start).c_str (), nullptr, 2);
        }

        if (e[pos] == '\'')
        {
            ++pos;
            u16 ch = 0;

            if (pos < e.size ())
            {
                ch = (u8)e[pos];
                ++pos;
            }

            if (pos < e.size () && e[pos] == '\'')
                ++pos;

            return ch;
        }

        if (e[pos] == '$' && (pos + 1 >= e.size () || !isxdigit (e[pos + 1])))
        {
            ++pos;
            return pc;
        }

        if (isdigit (e[pos]))
        {
            size_t start = pos;
            while (pos < e.size () && isxdigit (e[pos]))
                ++pos;

            int base = 10;

            if (pos < e.size () && (e[pos] == 'h' || e[pos] == 'H'))
            {
                base = 16;
                ++pos;
            }
            else if (pos < e.size () && (e[pos] == 'b' || e[pos] == 'B'))
            {
                base = 2;
                ++pos;
            }
            else if (pos < e.size () && (e[pos] == 'o' || e[pos] == 'O'))
            {
                base = 8;
                ++pos;
            }

            return (u16)strtoul (e.substr (start, pos - start).c_str (), nullptr, base);
        }

        if (isalpha (e[pos]) || e[pos] == '_' || e[pos] == '.')
        {
            size_t start = pos;

            while (pos < e.size () &&
                   (isalnum (e[pos]) || e[pos] == '_' || e[pos] == '.'))
                ++pos;

            std::string name = e.substr (start, pos - start);
            auto it = syms.find (name);

            if (it != syms.end ())
                return it->second;

            if (externs != nullptr && externs->count (name))
            {
                resolved = false;
                unres = name;
                return 0;
            }

            if (second)
                throw std::runtime_error ("Undefined symbol: " + name);

            resolved = false;
            unres = name;

            return 0;
        }

        throw std::runtime_error ("Parse error at: " + e.substr (pos));
    }
};