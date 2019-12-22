#ifndef db_h
#define db_h

#include "csv.h"
#include "json.h"
#include "yaml.h"

enum type {
    list,
    map,
    string
};

struct data {
    enum type type;
    int mark;
    struct data * data;
    struct data * next;
    struct map map;
};

struct schema {
    struct pool * pool;
    struct data * root;
    struct list list;
};

struct markup {
    int level;
    enum type type;
    int mark;
    char * key;
    struct markup * next;
};

int schema_create(struct schema *, struct heap *);
void schema_destroy(struct schema *);
int schema_push(struct schema *, enum type, int, char *);
void schema_pop(struct schema *);
struct data * schema_top(struct schema *);
struct data * schema_load(struct schema *, struct markup *);
void schema_print(struct schema *);

struct parser {
    size_t size;
    struct csv csv;
    struct json json;
    struct yaml yaml;
    struct data * root;
    struct data * data;
};

int parser_create(struct parser *, size_t, struct heap *);
void parser_destroy(struct parser *);
int parser_parse(struct parser *, const char *, struct data *);

#endif
