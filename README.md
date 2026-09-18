# sqlite

SQLite for candela: open a database file, run SQL with bound parameters, and
read rows back as typed values.

Use it when a program needs to keep structured data between runs: settings,
records, anything a table fits. The package ships the SQLite library itself,
built for each desktop platform, so there is nothing to install beside it.

## Quick start

```sh
candela add sqlite
```

```rust
import "sqlite";

fn main() {
    let db = open("notes.db");
    db.exec("create table if not exists notes (id integer primary key, body text)");
    db.insert("notes", {"body": Value::Text("first")});
    for row in db.query("select id, body from notes order by id", []) {
        print(row.get<int>("id"), row.get<string>("body"));
    }
    db.close();
}
```

`open` takes a file path or `":memory:"`. `exec` runs SQL that returns
nothing; `query` binds a list of values in order and returns the rows, and
`run` does the same for a statement that returns nothing and gives back the
number of rows changed; pass `[]` when there is nothing to bind. `insert`
builds the statement from a map of column names to values. `prepare` gives a
`Statement` to bind and step yourself when one statement runs many times. The
package is imported bare rather than with `as`, because an enum variant such
as `Value::Text` cannot be spelled through an alias yet.

## Values

A cell or a parameter is a `Value`: `Int(n)`, `Real(f)`, `Text(s)` or `Null`.
`row.get<int>("id")`, `row.get<float>("score")` and `row.get<string>("body")`
read a column by name and convert it; `row.cell("score")` returns the `Value`
itself, for a null check or a match.

## The schema module

```rust
import "sqlite/schema" as schema;

schema::tables(db)           // table names
schema::columns(db, "notes") // name, type, not_null, primary_key per column
```

## Errors

A failing call raises `sqlite_error: <sqlite's message>`; `open` raises
`sqlite_open_failed`, a missing column `sqlite_no_such_column`, and a parameter
list of the wrong length `sqlite_bind_count`. Catch them with `try`/`catch`.

## Limitations

Blob columns are returned as text. The library is built single-threaded.

## Building from source

```sh
./build-native.sh              # writes dist/sqlite.so, .dylib or .dll
candela run tests/test_sqlite.cdl
```

The release workflow runs the same script on every desktop target and packs one
archive per target.
