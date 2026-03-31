#pragma once
#include "data_source.hpp"
#include <concepts>
#include <expected>
#include <string>
#include <vector>

namespace Adapters {

/// Minimal requirement: execute raw SQL and get a QueryResult back.
/// Satisfied by IDataSource and any duck-typed wrapper around it.
template<typename DB>
concept DatabaseLike = requires(DB& db, const std::string& sql) {
    { db.Execute(sql) } -> std::same_as<QueryResult>;
};

/// Extends DatabaseLike with connection management, schema introspection,
/// and paginated structured queries — the full IDataSource surface minus
/// the write operations.
template<typename DB>
concept DataSourceLike = DatabaseLike<DB> && requires(DB& db, const std::string& name, const DataQuery& q) {
    { db.IsConnected() } -> std::convertible_to<bool>;
    { db.AdapterName() } -> std::convertible_to<std::string>;
    { db.GetCatalogs() } -> std::same_as<std::vector<std::string>>;
    { db.GetTables(name) } -> std::same_as<std::vector<TableInfo>>;
    { db.GetColumns(name) } -> std::same_as<std::vector<ColumnInfo>>;
    { db.ExecuteQuery(q) } -> std::same_as<QueryResult>;
    { db.CountQuery(q) } -> std::convertible_to<int>;
};

/// DataSourceLike that additionally exposes INSERT / UPDATE / DELETE.
/// SupportsWrite() may still return false at run-time (e.g. read-only mode).
template<typename DB>
concept WritableDataSource =
    DataSourceLike<DB> &&
    requires(DB& db, const std::string& table, const std::unordered_map<std::string, std::string>& kv) {
        { db.SupportsWrite() } -> std::convertible_to<bool>;
        { db.InsertRow(table, kv) } -> std::same_as<std::expected<void, Error>>;
        { db.DeleteRow(table, kv) } -> std::same_as<std::expected<void, Error>>;
        { db.UpdateRow(table, kv, kv) } -> std::same_as<std::expected<void, Error>>;
    };

/// An adapter that supports registering user-defined scalar functions.
/// SupportsUDF() returns true when the backend can execute them.
///
/// Note: the full RegisterScalar / RegisterVectorized API lives in
/// IUDFProvider (udf_provider.hpp) — this concept only checks the feature
/// flag so you can guard code paths without including that header.
template<typename DB>
concept UDFCapable = requires(DB& db) {
    { db.SupportsUDF() } -> std::convertible_to<bool>;
};

template<typename DB>
concept FullAdapter = WritableDataSource<DB> && UDFCapable<DB>;

/// Adapter that can be connected/disconnected programmatically.
/// Useful when writing generic connection-pool or retry logic.
template<typename DB>
concept ConnectableAdapter = DataSourceLike<DB> && requires(DB& db, const ConnectionParams& p) {
    { db.Connect(p) } -> std::same_as<std::expected<void, Error>>;
    { db.Disconnect() };
    { db.LastError() } -> std::convertible_to<std::string>;
};

} // namespace Adapters
