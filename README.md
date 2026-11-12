# LocalDB

Локальная встраиваемая база хранения данных на C++.

## Возможности

- Хранение данных на диске (каталог базы)
- Таблицы со схемой и типами столбцов: `INT`, `REAL`, `TEXT`, `BLOB`, `BOOL`
- Ограничения: `PRIMARY KEY`, `NOT NULL`, `UNIQUE`, `INDEX`
- SQL-подобный интерфейс: `CREATE TABLE`, `DROP TABLE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`
- Условия `WHERE` (=, !=, <, <=, >, >=, LIKE, IS NULL, IS NOT NULL), `ORDER BY`, `LIMIT`
- Индексы для поиска по равенству
- Персистентность между запусками
- Интерактивный CLI

## Сборка

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Запуск

```bash
./build/localdb_cli ./data
# Windows:
# .\build\Release\localdb_cli.exe .\data
```

## Примеры

```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT, active BOOL);
INSERT INTO users VALUES (1, 'Alice', 30, true);
INSERT INTO users (id, name, age, active) VALUES (2, 'Bob', 25, false);
SELECT * FROM users WHERE age >= 25 ORDER BY id DESC;
UPDATE users SET active = false WHERE id = 1;
DELETE FROM users WHERE id = 2;
SHOW TABLES;
DESCRIBE users;
```
