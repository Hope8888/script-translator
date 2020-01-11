#ifndef script_h
#define script_h

#include "lookup.h"

enum script_type {
    token,
    integer,
    identifier
};

struct script_node {
    enum script_type type;
    union {
        int token;
        long integer;
        struct string * identifier;
    };
    struct script_node * node;
    struct script_node * next;
};

struct script_state {
    struct script_state * next;
};

struct script {
    struct heap * heap;
    struct lookup * lookup;
    struct strbuf strbuf;
    void * scanner;
    void * parser;
    struct store store;
    struct script_state * state;
};

int script_create(struct script *, size_t, struct heap *, struct lookup *);
void script_destroy(struct script *);
int script_translate(struct script *, struct string *);

struct script_node * script_node_create(struct script *, enum script_type);
int script_node_token(struct script *, int, struct script_node **);
int script_node_integer(struct script *, char *, size_t, int, struct script_node **);
int script_node_identifier(struct script *, char *, size_t, struct script_node **);

#endif
