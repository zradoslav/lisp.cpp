#include "environment.hpp"
#include "eval.hpp"
#include "read.hpp"

#include <fstream>
#include <functional>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace klmr { namespace lisp {

struct bind_args : boost::static_visitor<void> {
    environment& env;
    call::iterator begin;
    call::iterator end;

    bind_args(environment& env, call::iterator begin, call::iterator end)
        : env{env}, begin{begin}, end{end} {}

    auto operator ()(symbol const& sym) const -> void {
        env.add(sym, list(begin, end));
    }

    auto operator ()(list const& lst) const -> void {
        auto argsize = length(lst);
        auto paramsize = static_cast<decltype(argsize)>(std::distance(begin, end));
        if (argsize != paramsize)
            throw value_error{"Expected " + std::to_string(argsize) +
                " arguments, got " + std::to_string(paramsize)};

        auto i = begin;
        for (auto&& sym : lst)
            env.add(as_symbol(sym), *i++);
    }

    template <typename T>
    auto operator ()(T&& val) const -> void {
        throw invalid_node{val, "symbol"};
    }
};

environment::environment(
        environment& parent,
        value const& arglist,
        call::iterator a, call::iterator b)
        : parent{&parent} {
    // Bind arglist to corresponding args, or single symbol to list of args
    boost::apply_visitor(bind_args{*this, a, b}, arglist);
}

auto environment::operator[](symbol const& sym) -> value& {
    for (auto current = this; current != nullptr; current = current->parent) {
        auto&& it = current->frame.find(sym);
        if (it != current->frame.end())
            return it->second;
    }

    throw name_error{sym};
}

auto environment::add(symbol const& sym, value val) -> void {
    frame.emplace(sym, std::forward<value>(val));
}

auto parent(environment const& env) -> environment* {
    return env.parent;
}

template <typename R, template<typename> class Op>
struct binary_operation : boost::static_visitor<R> {
    template <typename T>
    auto operator ()(literal<T> const& a, literal<T> const& b) const -> R {
        return Op<T>{}(as_raw(a), as_raw(b));
    }

    template <typename T, typename U>
    auto operator ()(T const&, U const&) const -> R {
        throw value_error{"Mismatching or invalid operand types"};
    }
};

template <typename T>
struct not_equal_to : std::binary_negate<std::equal_to<T>> {
    not_equal_to() : std::binary_negate<std::equal_to<T>>{std::equal_to<T>{}} {}
};

auto get_global_environment() -> environment {
    auto env = environment{};

#   define VAR_OPERATOR(name, op, type) \
    env.add(symbol{name}, \
        call{env, "args", [] (environment& env) { \
            auto&& args = as_list(env["args"]); \
            return std::accumulate( \
                std::next(begin(args)), end(args), \
                *begin(args), [] (value const& a, value const& b) { \
                    return as_literal(as_raw<type>(a) op as_raw<type>(b)); \
                }); \
        }} \
    )

    VAR_OPERATOR("+", +, double);
    VAR_OPERATOR("-", -, double);
    VAR_OPERATOR("*", *, double);
    VAR_OPERATOR("/", /, double);
    VAR_OPERATOR("and", &&, bool);
    VAR_OPERATOR("or", ||, bool);

#   undef VAR_OPERATOR

#   define BIN_OPERATOR(name, op, ret) \
    env.add(symbol{name}, \
        call{env, {"a", "b"}, [] (environment& env) { \
            auto&& a = env["a"]; \
            auto&& b = env["b"]; \
            return as_literal(boost::apply_visitor(binary_operation<ret, op>{}, a, b)); \
        }} \
    )

    BIN_OPERATOR("==", std::equal_to, bool);
    BIN_OPERATOR("!=", not_equal_to, bool);
    BIN_OPERATOR("<", std::less, bool);
    BIN_OPERATOR(">", std::greater, bool);
    BIN_OPERATOR("<=", std::less_equal, bool);
    BIN_OPERATOR(">=", std::greater_equal, bool);

#   undef BIN_OPERATOR

    env.add(symbol{"not"},
        call{env, {"a"}, [] (environment& env) {
            return as_literal(not as_raw<bool>(env["a"]));
        }}
    );

    env.add(symbol{"empty?"},
        call{env, {"a"}, [] (environment& env) {
            auto&& a = as_list(env["a"]);
            return as_literal(empty(a));
        }}
    );

    env.add(symbol{"length"},
        call{env, {"a"}, [] (environment& env) {
            auto&& a = as_list(env["a"]);
            return as_literal<double>(length(a));
        }}
    );

    env.add(symbol{"quote"},
        macro{env, {"expr"}, [] (environment& env) { return env["expr"]; }}
    );

    env.add(symbol{"lambda"},
        macro{env, {"args", "expr"}, [] (environment& env) {
            auto&& args = env["args"];
            auto expr = env["expr"];
            // FIXME Capture by value incurs expensive copy. Solved in C++14.
            return call{env, std::move(args), [expr](environment& frame) {
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

    // Load all the rest from Lisp source

    auto&& lib_file = std::ifstream{"./common.lisp"};
    auto begin = stream_iterator_t{lib_file};
    auto end = stream_iterator_t{};
    while (auto&& expr = read(begin, end))
        eval(*expr, env);

    return env;
}

} } // namespace klmr::lisp
