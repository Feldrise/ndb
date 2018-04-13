#ifndef EXPRESSION_FUNCTION_H_NDB
#define EXPRESSION_FUNCTION_H_NDB

#include <ndb/expression.hpp>
#include <ndb/expression/code.hpp>
#include <ndb/expression/utility.hpp>
#include <ndb/expression/operator.hpp>

namespace ndb
{
    namespace expr_function
    {
        struct count;
        struct now;
    } // expr_function


    /*
    template<class... Ts>
    constexpr auto test(const Ts&... t)
    {
        auto func = ndb::expression<expr_function::count, expr_type_code::function>();

        auto expr = (ndb::expr_make(t), ...);

        return ndb::expression<decltype(func), expr_type_code::op_function, decltype(expr)> { func, expr };
    }*/

    template<class T>
    constexpr auto count(const T& t)
    {
        auto func = ndb::expr_make_keyword<expr_keyword_code::count>();
        auto expr = ndb::expr_make(t);
        return ndb::expression<decltype(func), expr_type_code::op_function, decltype(expr)> { func, expr };
    }

    constexpr auto now()
    {
        auto func = ndb::expr_make_keyword<expr_keyword_code::now>();
        auto expr = ndb::expr_make();
        return ndb::expression<decltype(func), expr_type_code::op_function, decltype(expr)> { func, expr };
    }

    constexpr auto limit(int count, int offset = 0)
    {
        auto func = ndb::expr_make_keyword<expr_keyword_code::limit>();
        auto expr = ndb::expression<int, expr_type_code::value>{ count };
        return ndb::expression<decltype(func), expr_type_code::keyword, decltype(expr)> { func, expr };
    }
} // ndb

#endif // EXPRESSION_FUNCTION_H_NDB
