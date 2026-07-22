#pragma once
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Processador de macros de UMA passagem.
//
// O fonte e lido uma unica vez: as definicoes sao registradas na tabela de
// macros a medida que aparecem e as chamadas sao expandidas imediatamente ao
// serem reconhecidas. A expansao de uma macro reentra no mesmo laco de
// processamento, o que da, de graca e sem segunda varredura:
//
//   - definicao de macro dentro de macro (a interna so passa a existir quando
//     a externa e expandida, com os parametros da externa ja substituidos);
//   - chamada de macro dentro de macro, em qualquer profundidade.
//
// Sintaxes de definicao aceitas (todas equivalentes):
//     MACRO NOME p1, p2
//     NOME MACRO p1, p2
//     NOME: MACRO p1, p2
//         ...corpo...
//     ENDM

struct MacroDef
{
    std::string name;
    std::vector< std::string > params;
    std::vector< std::string > body;
};

class MacroProcessor
{
  public:
    static constexpr int MAX_DEPTH = 64;

    std::string process (const std::string &src)
    {
        macros_.clear ();

        std::istringstream in (src);
        std::ostringstream out;

        processStream (in, out, {}, {}, 0);

        return out.str ();
    }

    const std::unordered_map< std::string, MacroDef > &macros () const
    {
        return macros_;
    }

  private:
    std::unordered_map< std::string, MacroDef > macros_;

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
            c = (char)toupper ((unsigned char)c);
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

    static std::vector< std::string > splitArgs (const std::string &s)
    {
        std::vector< std::string > args;
        std::string cur;
        int depth = 0;
        bool inStr = false;

        for (char c : s)
        {
            if (c == '\'')
            {
                inStr = !inStr;
                cur += c;
            }
            else if (inStr)
            {
                cur += c;
            }
            else if (c == '(')
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
                args.push_back (trim (cur));
                cur.clear ();
            }
            else
            {
                cur += c;
            }
        }

        if (!trim (cur).empty ())
            args.push_back (trim (cur));

        return args;
    }

    static std::string getLabel (const std::string &line)
    {
        if (!line.empty () && (line[0] == '_' || isalpha ((unsigned char)line[0])))
        {
            size_t i = 0;

            while (i < line.size () &&
                   (isalnum ((unsigned char)line[i]) || line[i] == '_'))
                ++i;

            if (i < line.size () && line[i] == ':')
                return line.substr (0, i);
        }
        return {};
    }

    static std::string afterLabel (const std::string &line)
    {
        size_t col = line.find (':');
        if (col == std::string::npos)
            return line;
        return trim (line.substr (col + 1));
    }

    // Separa a primeira palavra do resto da linha.
    static void splitFirst (const std::string &s, std::string &first, std::string &rest)
    {
        std::string t = trim (s);
        size_t i = 0;

        while (i < t.size () && !isspace ((unsigned char)t[i]))
            ++i;

        first = t.substr (0, i);
        rest = trim (t.substr (i));
    }

    static std::string firstToken (const std::string &line)
    {
        std::string f, r;
        splitFirst (line, f, r);
        return upper (f);
    }

    // Reconhece uma linha de definicao de macro nas tres sintaxes aceitas.
    // Retorna false se a linha nao for uma definicao.
    static bool parseMacroHeader (const std::string &label, const std::string &rest,
                                  std::string &name, std::vector< std::string > &params)
    {
        std::string tok1, r1;
        splitFirst (rest, tok1, r1);

        if (upper (tok1) == "MACRO")
        {
            if (!label.empty ())
            {
                // NOME: MACRO p1, p2
                name = upper (label);
                params = splitArgs (r1);
                return true;
            }

            // MACRO NOME p1, p2
            std::string tok2, r2;
            splitFirst (r1, tok2, r2);

            if (tok2.empty ())
                throw std::runtime_error ("MACRO sem nome");

            name = upper (tok2);
            params = splitArgs (r2);
            return true;
        }

        // NOME MACRO p1, p2
        std::string tok2, r2;
        splitFirst (r1, tok2, r2);

        if (!tok1.empty () && upper (tok2) == "MACRO")
        {
            name = upper (tok1);
            params = splitArgs (r2);
            return true;
        }

        return false;
    }

    // Substituicao textual dos parametros formais pelos argumentos reais,
    // respeitando limites de palavra (X nao casa dentro de MAX ou X1).
    static std::string applyParams (const std::string &line,
                                    const std::vector< std::string > &pnames,
                                    const std::vector< std::string > &pvals)
    {
        std::string result = line;

        for (size_t i = 0; i < pnames.size (); ++i)
        {
            const std::string &key = pnames[i];
            const std::string val = i < pvals.size () ? pvals[i] : std::string ();

            if (key.empty ())
                continue;

            std::string out;
            size_t pos = 0;

            while (pos < result.size ())
            {
                size_t found = result.find (key, pos);

                if (found == std::string::npos)
                {
                    out += result.substr (pos);
                    break;
                }

                bool leftOk = (found == 0 ||
                               (!isalnum ((unsigned char)result[found - 1]) &&
                                result[found - 1] != '_'));

                bool rightOk = (found + key.size () >= result.size () ||
                                (!isalnum ((unsigned char)result[found + key.size ()]) &&
                                 result[found + key.size ()] != '_'));

                out += result.substr (pos, found - pos);

                if (leftOk && rightOk)
                {
                    out += val;
                    pos = found + key.size ();
                }
                else
                {
                    out += key;
                    pos = found + key.size ();
                }
            }

            result = out;
        }

        return result;
    }

    // Le o corpo da macro ate o ENDM correspondente.
    //
    // Definicoes aninhadas sao copiadas INTEGRALMENTE para o corpo (contando
    // niveis de MACRO/ENDM). Elas so serao registradas quando a macro externa
    // for expandida - que e a semantica correta de macro dentro de macro.
    // Os parametros do nivel externo ja sao substituidos aqui, permitindo
    // parametrizacao em varios niveis de aninhamento.
    void collectMacroBody (std::istream &in, MacroDef &def,
                           const std::vector< std::string > &pnames,
                           const std::vector< std::string > &pvals)
    {
        std::string line;
        int level = 1;

        while (std::getline (in, line))
        {
            std::string subst = applyParams (line, pnames, pvals);
            std::string clean = trim (stripComment (subst));

            std::string lbl = getLabel (clean);
            std::string rest = lbl.empty () ? clean : trim (afterLabel (clean));

            std::string dummyName;
            std::vector< std::string > dummyParams;

            if (!rest.empty () && parseMacroHeader (lbl, rest, dummyName, dummyParams))
                ++level;
            else if (firstToken (rest) == "ENDM")
            {
                --level;
                if (level == 0)
                    return;
            }

            def.body.push_back (subst);
        }

        throw std::runtime_error ("ENDM ausente na macro " + def.name);
    }

    void processStream (std::istream &in, std::ostream &out,
                        const std::vector< std::string > &pnames,
                        const std::vector< std::string > &pvals, int depth)
    {
        std::string line;

        while (std::getline (in, line))
        {
            std::string subst = applyParams (line, pnames, pvals);
            std::string clean = trim (stripComment (subst));

            std::string lbl = getLabel (clean);
            std::string rest = lbl.empty () ? clean : trim (afterLabel (clean));

            if (!rest.empty ())
            {
                MacroDef def;

                if (parseMacroHeader (lbl, rest, def.name, def.params))
                {
                    collectMacroBody (in, def, pnames, pvals);
                    macros_[def.name] = def;
                    continue;
                }
            }

            std::string tok, args;
            splitFirst (rest, tok, args);
            tok = upper (tok);

            auto it = macros_.find (tok);
            if (!tok.empty () && it != macros_.end ())
            {
                if (!lbl.empty ())
                    out << lbl << ":\n";

                expandMacro (tok, splitArgs (args), out, depth + 1);
                continue;
            }

            out << subst << "\n";
        }
    }

    void expandMacro (const std::string &name, const std::vector< std::string > &args,
                      std::ostream &out, int depth)
    {
        if (depth > MAX_DEPTH)
            throw std::runtime_error ("Expansao de macro profunda demais (recursao?): " +
                                      name);

        auto it = macros_.find (name);
        if (it == macros_.end ())
            throw std::runtime_error ("Macro nao definida: " + name);

        // copia: o corpo pode ser redefinido durante a propria expansao
        const MacroDef def = it->second;

        std::string bodyStr;
        for (auto &bl : def.body)
            bodyStr += bl + "\n";

        std::istringstream bodyIn (bodyStr);

        // Reentra no mesmo processamento: definicoes aninhadas sao registradas
        // agora e chamadas aninhadas sao expandidas recursivamente, tudo na
        // mesma passagem, escrevendo direto na saida.
        processStream (bodyIn, out, def.params, args, depth);
    }
};
