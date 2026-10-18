# SQL

`Engine::SQL` is the SQLite-backed persistence layer.

- `content/` defines bindable SQL values.
- `modern/` holds parameterized query types.
- `storage/` owns SQLite connections and executes queries.
- `plus/` provides transaction helpers.
- `orm/` describes tables and creates safe schema SQL.

The implementation is built as `XERSQL` when SQLite3 is available. Values are
bound through prepared statements; query values are never interpolated into SQL.