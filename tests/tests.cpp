#include "localdb/database.hpp"
#include <iostream>
#include <cassert>
#include <cstdio>

using namespace localdb;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::cerr << "FAIL: " << #cond << " at line " << __LINE__ << "\n"; ++failures; } } while(0)

int main() {
    std::filesystem::path p = std::filesystem::temp_directory_path() / "localdb_test_db";
    std::error_code ec; std::filesystem::remove_all(p, ec);
    {
        Database db(p);
        db.open();
        auto r1 = db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT, active BOOL)");
        CHECK(r1.ok);
        CHECK(db.execute("INSERT INTO users VALUES (1, 'Alice', 30, true)").ok);
        CHECK(db.execute("INSERT INTO users VALUES (2, 'Bob', 25, false)").ok);
        CHECK(db.execute("INSERT INTO users (id, name, age, active) VALUES (3, 'Cara', 40, true)").ok);
        auto s = db.execute("SELECT * FROM users");
        CHECK(s.ok);
        CHECK(s.rows.size() == 3);
        auto s2 = db.execute("SELECT name, age FROM users WHERE id = 2");
        CHECK(s2.rows.size() == 1);
        CHECK(s2.rows[0][0].toString() == "Bob");
        CHECK(db.execute("UPDATE users SET age = 26 WHERE id = 2").affected == 1);
        auto s3 = db.execute("SELECT age FROM users WHERE id = 2");
        CHECK(s3.rows[0][0].toString() == "26");
        CHECK(db.execute("DELETE FROM users WHERE id = 1").affected == 1);
        CHECK(db.execute("SELECT * FROM users").rows.size() == 2);
        CHECK(db.execute("SHOW TABLES").ok);
        CHECK(db.execute("DESCRIBE users").rows.size() == 4);
        db.close();
    }
    {
        Database db(p);
        db.open();
        auto s = db.execute("SELECT * FROM users");
        CHECK(s.rows.size() == 2); // persistence
        db.close();
    }
    std::filesystem::remove_all(p, ec);
    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
