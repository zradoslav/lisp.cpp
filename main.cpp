#include "eval.hpp"

#include <iostream>

using klmr::lisp::value;
using klmr::lisp::environment;

auto read(std::string const& input) -> value {
    (void) input;
    using namespace klmr::lisp;

    return list{
        symbol{"define"},
        list{
            symbol{"lambda"},
            list{symbol{"n"}},
            list{
                symbol{"*"},
                symbol{"n"},
                symbol{"n"}
            }
        }
    }; // FIXME placeholder
}

auto get_global_environment() -> environment {
    using namespace klmr::lisp;
    auto env = environment{};
    env.set(symbol{"lambda"},
        macro{env, {"args", "expr"},
        [] (environment& env) {
            auto&& fformals = as_list(env["formals"]);
            auto formals = std::vector<symbol>(length(fformals));
            std::transform(begin(fformals), end(fformals), begin(formals), as_symbol);
            auto expr = env["expr"];
            // FIXME Capture by value incurs expensive copy. Solved in C++14.
            return call{env, formals, [expr](environment& frame) {
                return eval(expr, frame);
            }};
        }}
    );
    env.set(symbol{"define"},
        macro{env, {"name", "expr"}, [] (environment& env) {
            auto&& name = as_symbol(env["name"]);
            env.set(name, eval(env["expr"], env));
            return nil;
        }}
    );
    return env;
}

auto repl(std::string prompt) -> void {
    auto input = std::string{};
    auto global_env = get_global_environment();
    for (;;) {
        std::cout << prompt << std::flush;
        if (not getline(std::cin, input))
            return;
        auto&& result = eval(read(input), global_env);
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
