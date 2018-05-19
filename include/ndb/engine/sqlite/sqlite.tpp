#include <ndb/expression/sql.hpp>
#include <ndb/engine/sqlite/function.hpp>
#include <ndb/engine/sqlite/type.hpp>
#include <ndb/expression/deduce.hpp>
#include <ndb/option.hpp>


namespace ndb
{
    sqlite::sqlite() {}

    template<class Database>
    void sqlite::connect()
    {
        std::string db_name = Database::group::name;
        db_name += "_D" + std::to_string(ndb::database_id<Database>);
        auto conn = std::make_unique<sqlite_connection>(db_name);
        connection_list_.emplace(ndb::database_id<Database>, std::move(conn));

        // database connected, add foreign keys and create model
        exec<Database>("PRAGMA foreign_keys = ON;");
        make<Database>();
    }

    template<class Database>
    sqlite_connection& sqlite::connection() const
    {
        if (!connection_list_.count(ndb::database_id<Database>)) ndb_error("database connection not found : D" + std::to_string(ndb::database_id<Database>));
        return *connection_list_.at(ndb::database_id<Database>).get();
    }

    template<class T>
    void sqlite::bind(sqlite3_stmt* statement, int bind_index, const T& v) const
    {
        if constexpr (std::is_same_v<int, T>) sqlite3_bind_int(statement, bind_index, v);
        if constexpr (std::is_same_v<double, T>) sqlite3_bind_double(statement, bind_index, v);
        if constexpr (std::is_same_v<std::string, T>) sqlite3_bind_text(statement, bind_index, v.c_str(), -1, SQLITE_TRANSIENT); //TODO: use SQLITE_STATIC
        if constexpr (std::is_same_v<std::vector<char>, T>) sqlite3_bind_blob(statement, bind_index, v.data(), 100, SQLITE_TRANSIENT);
    };

    template<class Database>
    void sqlite::exec(const char* str_query) const
    {
        exec<Database>(std::string(str_query));
    }

    template<class Database>
    void sqlite::exec(const std::string& str_query) const
    {
        sqlite3_stmt* statement;
        int step = SQLITE_DONE;

        if(sqlite3_prepare_v2(connection<Database>().database(), str_query.c_str(), -1, &statement, nullptr) == SQLITE_OK)
        {
            step = sqlite3_step(statement);
            sqlite3_finalize(statement);
        }
        else ndb_error("exec error : " + str_query);
    }

    template<class Database, class Query_option, class Expr>
    auto sqlite::exec(const Expr& expr) const
    {
        constexpr auto str_query = ndb::sql_expression<Expr>{};

        using Result_type = typename
        std::conditional_t<
            ndb::has_option_v<query_option::object, Query_option>,
            typename ndb::deduce<Expr>::main_table::Detail_::object_type,
            ndb::line<sqlite>
        >;


        sqlite3_stmt* statement;
        int step = SQLITE_DONE;

        ndb::result<sqlite, Result_type> result;

        if(sqlite3_prepare_v2(connection<Database>().database(), str_query.c_str(), -1, &statement, nullptr) == SQLITE_OK)
        {
            // bind values from expression
            int bind_index = 1;
            expr.eval([&](auto&& e)
            {
                using expr_type = std::decay_t<decltype(e)>;
                // e is expr_value
                if constexpr (ndb::expr_is_value<expr_type>)
                {
                    using value_type = std::decay_t<decltype(e.value())>;
                    using native_type = typename ndb::native_type<sqlite, value_type>::type;

                    // bind native type value or encode custom type value
                    if constexpr (is_native<value_type>) bind(statement, bind_index++, e.value());
                    else bind(statement, bind_index++, type<sqlite>::encode(e.value())); //check encoders if you have an error here
                }
            });

            step = sqlite3_step(statement);

            // select
            while (step == SQLITE_ROW)
            {
                ndb::line<sqlite> line;
                int field_count = sqlite3_column_count(statement);

                for(int field_it = 0; field_it < field_count; field_it++)
                {
                    const char* field_name = sqlite3_column_name(statement, field_it);
                    int field_id = -1;
                    if (field_name[0] == 'F') field_id = std::stoi(std::string(field_name + 1));

                    int field_type = sqlite3_column_type(statement, field_it);
                    sqlite3_value* field_value = sqlite3_column_value(statement, field_it);

                    if (field_type == ndb::type_id<sqlite, int>::value)
                        line.add(field_id, sqlite3_value_int(field_value));

                    if (field_type == ndb::type_id<sqlite, double>::value)
                        line.add(field_id, sqlite3_value_double(field_value));

                    if (field_type == ndb::type_id<sqlite, std::string>::value)
                        line.add(field_id, std::string(reinterpret_cast<const char*>(sqlite3_value_text(field_value))));

                    if (field_type == ndb::type_id<sqlite, std::vector<char>>::value)
                    {
                        auto data = reinterpret_cast<const char*>(sqlite3_value_blob(field_value));
                        line.add(field_id, std::vector<char>(data, data + 100));
                    }
                    //TODO null value
                }

                auto result_entity = ndb::result_encoder<Result_type, sqlite>::decode(line);
                result.add(std::move(result_entity));

                step = sqlite3_step(statement);
            }
            sqlite3_finalize(statement);

        }
        else ndb_error("exec error : " + str_query.to_string());

        std::string error = sqlite3_errmsg(connection<Database>().database());
        if(error != "not an error") ndb_error("query error : " + error);

        return result;
    }

    template<class Database>
    void sqlite::make()
    {
        using Model = typename Database::model;

        std::string output;

        ndb::for_each_entity<Model>([this, &output](auto&& index, auto&& table)
        {
            using Table = std::decay_t<decltype(table)>;

            output += "\ncreate table if not exists `T" + std::to_string(ndb::table_id<Table>) + "` (";

            ndb::for_each_entity(table, [&output](auto&& i, auto&& field)
            {
                using Field = std::decay_t<decltype(field)>;

                if constexpr (decltype(i){} != 0) output += ",";

                // field id
                output += "\nF" + std::to_string(ndb::field_id<Field>);
                bool need_size = false;

                // field type
                using native_value_type = typename native_type<sqlite, typename std::decay_t<decltype(field)>::value_type>::type;
                if constexpr (std::is_same_v<int, native_value_type>) output += " integer ";
                if constexpr (std::is_same_v<double, native_value_type>) output += " float ";
                if constexpr (std::is_same_v<std::string, native_value_type>)
                {
                    output += " text ";
                    need_size = true;
                }
                if constexpr (std::is_same_v<std::vector<char>, native_value_type>) output += " blob ";

                // field size
                if (field.detail_.size > 0 && need_size) output += "(" + std::to_string(field.detail_.size) + ")";

                // field option
                if (field.detail_.is_primary || field.detail_.is_oid) output += " primary key";
                if (field.detail_.is_auto_increment) output += " autoincrement";
                if (field.detail_.is_not_null) output += " not null";
                if (field.detail_.is_unique) output += " unique";

                if constexpr (ndb::is_field_entity<Field>)
                {
                    /*
                    auto store_type = typename Field::type{};

                    //output += "\n\tentity type " + std::string(store_type.detail_.name);
                    output += "\n\tentity count : " + std::to_string(field.detail_.size);
                    output += "\n\ttotal size : " + std::to_string(store_type.detail_.size);

                    if constexpr (ndb::is_field_entity_vector<Field>) output += " VECTOR";*/
                }
            });
            output += ");";

            // exec create table
            exec<Database>(output);
            output = "";
        });
    }

    template<class Expr>
    std::string sqlite::to_string(const Expr&)
    {
        constexpr auto str_query = ndb::sql_expression<Expr>{};
        return str_query.to_string();
    }

    constexpr auto sqlite::expr_category()
    {
        return expr_category_code::sql;
    }
} // ndb
