#pragma once
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "types.hpp"

// Avaliador de expressoes do montador.
//
// Alem do valor numerico, o avaliador informa se a expressao e *relocavel*,
// isto e, se ela depende de um rotulo cujo endereco final so sera conhecido
// pelo ligador. Essa informacao e o que permite ao montador emitir uma entrada
// de relocacao para toda referencia a endereco (interna ou externa), e nao
// apenas para simbolos EXTERN.
//
// Regras de relocabilidade:
//   LABEL          -> relocavel   (1 referencia, addend 0)
//   LABEL+4        -> relocavel   (1 referencia, addend 4)
//   FIM-INICIO     -> absoluta    (2 referencias: e uma diferenca/tamanho)
//   LABEL*2        -> absoluta    (nao faz sentido relocar)
//   CONST (EQU)    -> absoluta    (constante simbolica, nao e endereco)
class ExprEval
{
  public:
    using SymMap = std::unordered_map< std::string, u16 >;
    using NameSet = std::unordered_map< std::string, bool >;

    struct Result
    {
        u16 value;            // valor calculado
        bool resolved;        // false se depende de simbolo ainda desconhecido
        std::string unresolved; // nome do simbolo desconhecido (EXTERN/indefinido)

        std::string relocSym; // simbolo contra o qual relocar ("" = absoluta)
        i16 relocAddend;      // valor a somar ao endereco final do simbolo

        Result () : value (0), resolved (true), relocAddend (0)
        {
        }
    };

    static std::string upper (std::string s)
    {
        for (char &c : s)
            c = (char)toupper ((unsigned char)c);
        return s;
    }

    static Result eval (const std::string &expr, const SymMap &syms, u16 pc,
                        bool secondPass, const NameSet *externs = nullptr,
                        const NameSet *absolutes = nullptr)
    {
        Ctx ctx (syms, pc, secondPass, externs, absolutes);
        Result res;

        std::string e = trim (expr);
        if (e.empty ())
            throw std::runtime_error ("Empty expression");

        try
        {
            size_t pos = 0;
            res.value = parseExpr (e, pos, ctx);
        }
        catch (std::runtime_error &)
        {
            if (secondPass)
                throw;

            res.resolved = false;
            return res;
        }

        res.resolved = ctx.resolved;
        res.unresolved = ctx.unres;

        // So vale a pena relocar quando a expressao referencia exatamente um
        // endereco, somado (nunca subtraido nem multiplicado).
        if (ctx.relocRefs == 1 && !ctx.relocInvalid)
        {
            res.relocSym = ctx.relocSym;
            res.relocAddend = (i16)(res.value - ctx.relocBase);
        }

        return res;
    }

  private:
    struct Ctx
    {
        const SymMap &syms;
        u16 pc;
        bool second;
        const NameSet *externs;
        const NameSet *absolutes;

        bool resolved = true;
        std::string unres;

        std::string relocSym;   // primeiro simbolo relocavel encontrado
        u16 relocBase = 0;      // quanto esse simbolo contribuiu para o valor
        int relocRefs = 0;      // quantos simbolos relocaveis apareceram
        bool relocInvalid = false; // usado em contexto que impede relocacao

        Ctx (const SymMap &s, u16 p, bool sec, const NameSet *ex, const NameSet *ab)
            : syms (s), pc (p), second (sec), externs (ex), absolutes (ab)
        {
        }

        void noteReloc (const std::string &name, u16 base)
        {
            if (relocRefs == 0)
            {
                relocSym = name;
                relocBase = base;
            }
            ++relocRefs;
        }
    };

    // Espacos em volta dos operadores sao ignorados: "FIM - INICIO" e
    // equivalente a "FIM-INICIO".
    static void skipSpaces (const std::string &e, size_t &pos)
    {
        while (pos < e.size () && (e[pos] == ' ' || e[pos] == '\t'))
            ++pos;
    }

    static std::string trim (const std::string &s)
    {
        size_t a = s.find_first_not_of (" \t");
        if (a == std::string::npos)
            return {};

        size_t b = s.find_last_not_of (" \t");

        return s.substr (a, b - a + 1);
    }

    static u16 parseExpr (const std::string &e, size_t &pos, Ctx &ctx)
    {
        u16 val = parseTerm (e, pos, ctx);

        while (true)
        {
            skipSpaces (e, pos);
            if (pos >= e.size ())
                break;

            char op = e[pos];

            if (op != '+' && op != '-' && op != '|' && op != '^')
                break;

            ++pos;

            int before = ctx.relocRefs;
            u16 rhs = parseTerm (e, pos, ctx);
            bool rhsHasReloc = ctx.relocRefs > before;

            if (op == '+')
                val += rhs;

            else if (op == '-')
            {
                // subtrair um endereco cancela a relocacao (e uma diferenca)
                if (rhsHasReloc)
                    ctx.relocInvalid = true;
                val -= rhs;
            }

            else if (op == '|')
            {
                if (rhsHasReloc)
                    ctx.relocInvalid = true;
                val |= rhs;
            }

            else if (op == '^')
            {
                if (rhsHasReloc)
                    ctx.relocInvalid = true;
                val ^= rhs;
            }
        }

        return val;
    }

    static u16 parseTerm (const std::string &e, size_t &pos, Ctx &ctx)
    {
        int before = ctx.relocRefs;
        u16 val = parseFactor (e, pos, ctx);

        while (true)
        {
            skipSpaces (e, pos);
            if (pos >= e.size ())
                break;

            char op = e[pos];

            if (op != '*' && op != '/' && op != '&' && op != '<' && op != '>')
                break;

            if ((op == '<' || op == '>') && (pos + 1 >= e.size () || e[pos + 1] != op))
                break;

            if (op == '<' || op == '>')
            {
                pos += 2;
                u16 rhs = parseFactor (e, pos, ctx);

                if (op == '<')
                    val = (u16)(val << rhs);
                else
                    val = (u16)(val >> rhs);
            }
            else
            {
                ++pos;
                u16 rhs = parseFactor (e, pos, ctx);

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

            // endereco em contexto multiplicativo/mascara nao e relocavel
            if (ctx.relocRefs > before)
                ctx.relocInvalid = true;
        }

        return val;
    }

    static u16 parseFactor (const std::string &e, size_t &pos, Ctx &ctx)
    {
        while (pos < e.size () && (e[pos] == ' ' || e[pos] == '\t'))
            ++pos;

        if (pos >= e.size ())
            throw std::runtime_error ("Unexpected end");

        if (e[pos] == '(')
        {
            ++pos;
            u16 v = parseExpr (e, pos, ctx);

            skipSpaces (e, pos);
            if (pos < e.size () && e[pos] == ')')
                ++pos;

            return v;
        }

        if (e[pos] == '-')
        {
            ++pos;
            int before = ctx.relocRefs;
            u16 v = (u16)(-(i16)parseFactor (e, pos, ctx));
            if (ctx.relocRefs > before)
                ctx.relocInvalid = true;
            return v;
        }
        if (e[pos] == '+')
        {
            ++pos;
            return parseFactor (e, pos, ctx);
        }

        if (e[pos] == '$' && pos + 1 < e.size () && isxdigit ((unsigned char)e[pos + 1]))
        {
            ++pos;
            size_t start = pos;

            while (pos < e.size () && isxdigit ((unsigned char)e[pos]))
                ++pos;

            return (u16)strtoul (e.substr (start, pos - start).c_str (), nullptr, 16);
        }

        if (e[pos] == '0' && pos + 1 < e.size () &&
            (e[pos + 1] == 'x' || e[pos + 1] == 'X'))
        {
            pos += 2;
            size_t start = pos;

            while (pos < e.size () && isxdigit ((unsigned char)e[pos]))
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

        // '$' sozinho = contador de posicao do montador
        if (e[pos] == '$')
        {
            ++pos;
            return ctx.pc;
        }

        if (isdigit ((unsigned char)e[pos]))
        {
            size_t start = pos;
            while (pos < e.size () && isxdigit ((unsigned char)e[pos]))
                ++pos;

            size_t end = pos; // fim dos digitos, sem o sufixo
            int base = 10;

            if (pos < e.size () && (e[pos] == 'h' || e[pos] == 'H'))
            {
                base = 16;
                ++pos;
            }
            else if (pos < e.size () && (e[pos] == 'o' || e[pos] == 'O'))
            {
                base = 8;
                ++pos;
            }
            else
            {
                // o sufixo 'b' de binario ja foi consumido como digito hexa
                std::string tok = e.substr (start, end - start);
                if (tok.size () > 1 && (tok.back () == 'b' || tok.back () == 'B'))
                {
                    bool onlyBits = true;
                    for (size_t i = 0; i + 1 < tok.size (); ++i)
                        if (tok[i] != '0' && tok[i] != '1')
                            onlyBits = false;

                    if (onlyBits)
                    {
                        base = 2;
                        --end;
                    }
                }
            }

            return (u16)strtoul (e.substr (start, end - start).c_str (), nullptr, base);
        }

        if (isalpha ((unsigned char)e[pos]) || e[pos] == '_' || e[pos] == '.')
        {
            size_t start = pos;

            while (pos < e.size () &&
                   (isalnum ((unsigned char)e[pos]) || e[pos] == '_' || e[pos] == '.'))
                ++pos;

            // simbolos sao tratados sem distincao de maiusculas/minusculas
            std::string name = upper (e.substr (start, pos - start));
            auto it = ctx.syms.find (name);

            if (it != ctx.syms.end ())
            {
                // rotulo (endereco) e relocavel; constante EQU nao e
                bool isAbsolute = ctx.absolutes && ctx.absolutes->count (name);
                if (!isAbsolute)
                    ctx.noteReloc (name, it->second);

                return it->second;
            }

            if (ctx.externs != nullptr && ctx.externs->count (name))
            {
                // simbolo de outro modulo: contribui 0 e sera resolvido na ligacao
                ctx.resolved = false;
                ctx.unres = name;
                ctx.noteReloc (name, 0);
                return 0;
            }

            if (ctx.second)
                throw std::runtime_error ("Undefined symbol: " + name);

            ctx.resolved = false;
            ctx.unres = name;
            ctx.noteReloc (name, 0);

            return 0;
        }

        throw std::runtime_error ("Parse error at: " + e.substr (pos));
    }
};
