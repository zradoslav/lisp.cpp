#include "eval.hpp"
#include "read.hpp"

#include <iostream>
#include <iterator>
#include <numeric>
#include <boost/optional.hpp>

using klmr::lisp::value;
using klmr::lisp::environment;

auto get_global_environment() -> environment {
    using namespace klmr::lisp;
    auto env = environment{};

#   define VAR_OPERATOR(name, op, type) \
    env.add(symbol{name}, \
        call{env, "args", [] (environment& env) { \
            auto&& args = as_list(env["args"]); \
            return std::accumulate( \
                std::next(begin(args)), end(args), \
                *begin(args), [] (value const& a, value const& b) { \
                    return literal<type>{as_raw<type>(a) op as_raw<type>(b)}; \
                }); \
        }} \
    )

    VAR_OPERATOR("+", +, double);
    VAR_OPERATOR("-", -, double);
    VAR_OPERATOR("*", *, double);
    VAR_OPERATOR("/", /, double);
    VAR_OPERATOR("and", &&, bool);
    VAR_OPERATOR("or", ||, bool);
    // TODO Implement remaining (non-variadic) operators

#   undef VAR_OPERATOR

    env.add(symbol{"quote"},
        macro{env, std::vector<symbol>{"expr"},
            [] (environment& env) { return env["expr"]; }}
    );

    env.add(symbol{"lambda"},
        macro{env, {"args", "expr"}, [] (environment& env) {
            auto&& args = as_list(env["args"]);
            auto formals = std::vector<symbol>(length(args));
            std::transform(begin(args), end(args), begin(formals), as_symbol);
            auto expr = env["expr"];
            // FIXME Capture by value incurs expensive copy. Solved in C++14.
            return call{env, formals, [expr](environment& frame) {
                return eval(expr, frame);
            }};
        }}
    );

    env.add(symbol{"define"},
        macro{env, {"name", "expr"}, [] (environment& env) {
            auto&& name = as_symbol(env["name"]);
            parent(env)->add(name, eval(env["expr"], *parent(env)));
            return eval(nil, env);
        }}
    );

    env.add(symbol{"if"},
        macro{env, {"cond", "conseq", "alt"}, [] (environment& env) {
            auto&& cond = eval(env["cond"], *parent(env));
            return eval(is_true(cond) ? env["conseq"] : env["alt"], *parent(env));
        }}
    );

    env.add(symbol{"set!"},
        macro{env, {"name", "expr"}, [] (environment& env) {
            auto&& name = as_symbol(env["name"]);
            (*parent(env))[name] = eval(env["expr"], *parent(env));
            return eval(nil, env);
        }}
    );

    env.add(symbol{"begin"},
        macro{env, "args", [] (environment& env) {
            auto&& result = value{};
            for (auto&& expr : as_list(env["args"]))
                result = eval(expr, *parent(env));
            return result;
        }}
    );

    return env;
}

auto repl(std::string prompt) -> void {
    auto global_env = get_global_environment();

    for (;;) {
        std::cout << prompt << std::flush;
        auto line = std::string{};
        if (not getline(std::cin, line))
            return;

        auto&& expr = klmr::lisp::read_full(begin(line), end(line));
        if (not expr)
            return;
        auto&& result = eval(*expr, global_env);
        std::cout << result << '\n';
    }
}

auto main() -> int
try {
    repl(">>> ");
} catch (klmr::lisp::name_error const& msg) {
    std::cerr << msg.what() << '\n';
    return 1;
}
