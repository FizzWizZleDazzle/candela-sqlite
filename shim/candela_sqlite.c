/*
 * candela_sqlite: the C side of the candela `sqlite` package.
 *
 * candela's foreign interface carries int32, double, and null-terminated
 * strings, and nothing that could hold a pointer. Databases and prepared
 * statements therefore live in tables here and cross the boundary as small
 * integer handles. A handle is an index into its table; -1 means the call
 * failed and sq_errmsg says why.
 *
 * Strings returned to candela are copied out by the caller before the next
 * call, so pointers that sqlite owns (error messages, column text) are
 * returned as they are.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/sqlite3.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* Handle tables. Slot 0 is never handed out so that 0 is never a handle. */

typedef struct {
    void **items;
    int32_t len;
    int32_t cap;
} table;

static table dbs;
static table stmts;

static int32_t table_add(table *t, void *item) {
    if (t->len == 0) {
        t->cap = 8;
        t->items = calloc((size_t)t->cap, sizeof(void *));
        if (t->items == NULL) {
            return -1;
        }
        t->len = 1;
    }
    for (int32_t i = 1; i < t->len; i++) {
        if (t->items[i] == NULL) {
            t->items[i] = item;
            return i;
        }
    }
    if (t->len == t->cap) {
        int32_t cap = t->cap * 2;
        void **grown = realloc(t->items, (size_t)cap * sizeof(void *));
        if (grown == NULL) {
            return -1;
        }
        memset(grown + t->cap, 0, (size_t)(cap - t->cap) * sizeof(void *));
        t->items = grown;
        t->cap = cap;
    }
    t->items[t->len] = item;
    return t->len++;
}

static void *table_get(const table *t, int32_t handle) {
    if (handle <= 0 || handle >= t->len) {
        return NULL;
    }
    return t->items[handle];
}

static void table_remove(table *t, int32_t handle) {
    if (handle > 0 && handle < t->len) {
        t->items[handle] = NULL;
    }
}

/* The message of the last failure that had no database to ask. */
static const char *last_error = "";

static sqlite3 *db_of(int32_t handle) {
    return (sqlite3 *)table_get(&dbs, handle);
}

static sqlite3_stmt *stmt_of(int32_t handle) {
    return (sqlite3_stmt *)table_get(&stmts, handle);
}

/* Library */

EXPORT const char *sq_version(void) {
    return sqlite3_libversion();
}

/* Databases */

EXPORT int32_t sq_open(const char *path) {
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        last_error = db ? sqlite3_errmsg(db) : "out of memory";
        if (db) {
            /* Keep the handle so the message stays readable; close it on
             * the next open instead. sqlite documents that errmsg outlives
             * the failed open until close. */
            static sqlite3 *failed = NULL;
            if (failed) {
                sqlite3_close(failed);
            }
            failed = db;
        }
        return -1;
    }
    int32_t handle = table_add(&dbs, db);
    if (handle < 0) {
        sqlite3_close(db);
        last_error = "out of memory";
    }
    return handle;
}

EXPORT const char *sq_errmsg(int32_t handle) {
    sqlite3 *db = db_of(handle);
    if (db == NULL) {
        return last_error;
    }
    return sqlite3_errmsg(db);
}

EXPORT int32_t sq_close(int32_t handle) {
    sqlite3 *db = db_of(handle);
    if (db == NULL) {
        last_error = "not an open database";
        return SQLITE_MISUSE;
    }
    /* Statements still open on this database are finalized first, as
     * sqlite3_close requires. */
    for (int32_t i = 1; i < stmts.len; i++) {
        sqlite3_stmt *stmt = (sqlite3_stmt *)stmts.items[i];
        if (stmt != NULL && sqlite3_db_handle(stmt) == db) {
            sqlite3_finalize(stmt);
            stmts.items[i] = NULL;
        }
    }
    int rc = sqlite3_close(db);
    if (rc == SQLITE_OK) {
        table_remove(&dbs, handle);
    }
    return rc;
}

EXPORT int32_t sq_exec(int32_t handle, const char *sql) {
    sqlite3 *db = db_of(handle);
    if (db == NULL) {
        last_error = "not an open database";
        return SQLITE_MISUSE;
    }
    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

EXPORT int32_t sq_changes(int32_t handle) {
    sqlite3 *db = db_of(handle);
    return db ? sqlite3_changes(db) : 0;
}

/* The rowid is 64 bits in sqlite and 32 in candela; a rowid past that range
 * comes back truncated. */
EXPORT int32_t sq_last_rowid(int32_t handle) {
    sqlite3 *db = db_of(handle);
    return db ? (int32_t)sqlite3_last_insert_rowid(db) : 0;
}

/* Statements */

EXPORT int32_t sq_prepare(int32_t handle, const char *sql) {
    sqlite3 *db = db_of(handle);
    if (db == NULL) {
        last_error = "not an open database";
        return -1;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (stmt == NULL) {
        /* Whitespace or a comment: nothing to run. */
        last_error = "the SQL holds no statement";
        return -1;
    }
    int32_t out = table_add(&stmts, stmt);
    if (out < 0) {
        sqlite3_finalize(stmt);
        last_error = "out of memory";
    }
    return out;
}

EXPORT int32_t sq_bind_int(int32_t handle, int32_t index, int32_t value) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_bind_int(stmt, index, value) : SQLITE_MISUSE;
}

EXPORT int32_t sq_bind_float(int32_t handle, int32_t index, double value) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_bind_double(stmt, index, value) : SQLITE_MISUSE;
}

EXPORT int32_t sq_bind_text(int32_t handle, int32_t index, const char *value) {
    sqlite3_stmt *stmt = stmt_of(handle);
    /* The buffer candela passes lives for this call only; SQLITE_TRANSIENT
     * makes sqlite take its own copy. */
    return stmt ? sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT) : SQLITE_MISUSE;
}

EXPORT int32_t sq_bind_null(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_bind_null(stmt, index) : SQLITE_MISUSE;
}

EXPORT int32_t sq_bind_count(int32_t handle) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_bind_parameter_count(stmt) : 0;
}

/* SQLITE_ROW (100) when a row is ready, SQLITE_DONE (101) at the end, and
 * another code on failure. */
EXPORT int32_t sq_step(int32_t handle) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_step(stmt) : SQLITE_MISUSE;
}

EXPORT int32_t sq_reset(int32_t handle) {
    sqlite3_stmt *stmt = stmt_of(handle);
    if (stmt == NULL) {
        return SQLITE_MISUSE;
    }
    int rc = sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return rc;
}

EXPORT int32_t sq_finalize(int32_t handle) {
    sqlite3_stmt *stmt = stmt_of(handle);
    if (stmt == NULL) {
        return SQLITE_MISUSE;
    }
    int rc = sqlite3_finalize(stmt);
    table_remove(&stmts, handle);
    return rc;
}

/* Columns of the current row */

EXPORT int32_t sq_column_count(int32_t handle) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_column_count(stmt) : 0;
}

EXPORT const char *sq_column_name(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    const char *name = stmt ? sqlite3_column_name(stmt, index) : NULL;
    return name ? name : "";
}

/* One of SQLITE_INTEGER (1), SQLITE_FLOAT (2), SQLITE_TEXT (3), SQLITE_BLOB
 * (4), SQLITE_NULL (5). */
EXPORT int32_t sq_column_type(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_column_type(stmt, index) : SQLITE_NULL;
}

EXPORT int32_t sq_column_int(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_column_int(stmt, index) : 0;
}

EXPORT double sq_column_float(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    return stmt ? sqlite3_column_double(stmt, index) : 0.0;
}

EXPORT const char *sq_column_text(int32_t handle, int32_t index) {
    sqlite3_stmt *stmt = stmt_of(handle);
    const unsigned char *text = stmt ? sqlite3_column_text(stmt, index) : NULL;
    return text ? (const char *)text : "";
}
