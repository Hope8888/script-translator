#include "parser.h"

static inline struct schema_node * schema_node_create(struct schema *, enum schema_type, int, struct pool *);
static inline void schema_node_destroy(struct schema *, struct schema_node *);
void schema_node_print(struct schema_node *, int, char *);

int parser_node(struct parser *, enum event_type, struct string *, struct schema_node *);
int parser_event(enum event_type, struct string *, void *);

static inline struct store_node * store_node_create(struct store *);
static inline void store_node_destroy(struct store *, struct store_node *);
static inline void * store_node_alloc(struct store_node *, size_t);

static inline struct schema_node * schema_node_create(struct schema * schema, enum schema_type type, int mark, struct pool * pool) {
    int status = 0;
    struct schema_node * node;

    node = pool_get(schema->pool);
    if(node) {
        node->type = type;
        node->mark = mark;
        node->data = NULL;
        node->next = NULL;
        if(map_create(&node->map, (map_compare_cb) strcmp, pool))
            status = panic("failed to create map object");
        if(status)
            pool_put(schema->pool, node);
    }

    return status ? NULL : node;
}

static inline void schema_node_destroy(struct schema * schema, struct schema_node * node) {
    struct map_kv kv;

    kv = map_start(&node->map);
    while(kv.key) {
        schema_node_destroy(schema, kv.value);
        kv = map_next(&node->map);
    }

    if(node->data)
        schema_node_destroy(schema, node->data);

    map_destroy(&node->map);
    pool_put(schema->pool, node);
}

void schema_node_print(struct schema_node * node, int indent, char * key) {
    int i;
    struct map_kv kv;

    for(i = 0; i < indent; i++)
        fputs("    ", stdout);

    if(key)
        fprintf(stdout, "[%s]", key);

    switch(node->type) {
        case list:
            fprintf(stdout, "[list:%d]\n", node->mark);
            schema_node_print(node->data, indent + 1, NULL);
            break;
        case map:
            fprintf(stdout, "[map:%d]\n", node->mark);
            kv = map_start(&node->map);
            while(kv.key) {
                schema_node_print(kv.value, indent + 1, kv.key);
                kv = map_next(&node->map);
            }
            break;
        case string:
            fprintf(stdout, "[string:%d]\n", node->mark);
            break;
    }
}


int schema_create(struct schema * schema, struct heap * heap) {
    int status = 0;

    schema->pool = heap_pool(heap, sizeof(struct schema_node));
    if(!schema->pool) {
        status = panic("failed to pool heap object");
    } else {
        schema->root = schema_node_create(schema, list, 0, heap->map_pool);
        if(!schema->root)
            status = panic("failed to create schema node object");
    }

    return status;
}

void schema_destroy(struct schema * schema) {
    schema_node_destroy(schema, schema->root);
}

void schema_clear(struct schema * schema) {
    if(schema->root->data) {
        schema_node_destroy(schema, schema->root->data);
        schema->root->data = NULL;
    }
}

int schema_push(struct schema * schema, enum schema_type type, int mark, char * key) {
    int status = 0;
    struct schema_node * node;

    node = schema_node_create(schema, type, mark, schema->root->map.pool);
    if(!node) {
        status = panic("failed to create schema node object");
    } else {
        if(schema->root->type == list) {
            if(schema->root->data) {
                status = panic("invalid data");
            } else {
                schema->root->data = node;
            }
        } else if(schema->root->type == map) {
            if(!key) {
                status = panic("invalid key");
            } else if(map_insert(&schema->root->map, key, node)) {
                status = panic("failed to insert map object");
            }
        }
        if(status) {
            /* skip push on error */
        } else if(node->type == list || node->type == map) {
            node->next = schema->root;
            schema->root = node;
        }
        if(status)
            schema_node_destroy(schema, node);
    }

    return status;
}

void schema_pop(struct schema * schema) {
    if(schema->root->next)
        schema->root = schema->root->next;
}

struct schema_node * schema_top(struct schema * schema) {
    return schema->root->data;
}

struct schema_node * schema_load(struct schema * schema, struct schema_markup * markup) {
    int status = 0;
    struct schema_markup * root = NULL;

    schema_clear(schema);

    while(markup->level) {
        while(root && root->level >= markup->level) {
            schema_pop(schema);
            root = root->next;
        }
        if(schema_push(schema, markup->type, markup->mark, markup->key)) {
            status = panic("failed to push schema object");
        } else if(markup->type == list || markup->type == map) {
            markup->next = root;
            root = markup;
        }
        markup++;
    }

    while(root) {
        schema_pop(schema);
        root = root->next;
    }

    return status ? NULL : schema_top(schema);
}

void schema_print(struct schema * schema) {
    struct schema_node * node;

    node = schema_top(schema);
    if(node)
        schema_node_print(node, 0, NULL);
}

int parser_create(struct parser * parser, size_t size, struct heap * heap) {
    int status = 0;

    parser->size = size;

    if(csv_create(&parser->csv, parser->size, heap)) {
        status = panic("failed to create csv object");
    } else {
        if(json_create(&parser->json, parser->size)) {
            status = panic("failed to create json object");
        } else {
            if(yaml_create(&parser->yaml, parser->size, heap))
                status = panic("failed to create yaml object");
            if(status)
                json_destroy(&parser->json);
        }
        if(status)
            csv_destroy(&parser->csv);
    }

    return status;
}

void parser_destroy(struct parser * parser) {
    yaml_destroy(&parser->yaml);
    json_destroy(&parser->json);
    csv_destroy(&parser->csv);
}

int parser_node(struct parser * parser, enum event_type event, struct string * string, struct schema_node * data) {
    int status = 0;

    if(data->type == list) {
        if(event != list_begin) {
            status = panic("invalid event");
        } else {
            if(parser->callback(start, data->mark, NULL, parser->context)) {
                status = panic("failed to process start event");
            } else {
                data->next = parser->root;
                parser->root = data;
            }
        }
    } else if(data->type == map) {
        if(event != map_begin) {
            status = panic("invalid event");
        } else {
            if(parser->callback(start, data->mark, NULL, parser->context)) {
                status = panic("failed to process start event");
            } else {
                data->next = parser->root;
                parser->root = data;
            }
        }
    } else {
        if(event != scalar) {
            status = panic("invalid event");
        } else if(parser->callback(next, data->mark, string, parser->context)) {
            status = panic("failed to process next event");
        }
    }

    return status;
}

int parser_event(enum event_type event, struct string * string, void * context) {
    int status = 0;
    struct parser * parser = context;

    if(parser->data) {
        if(parser_node(parser, event, string, parser->data)) {
            status = panic("failed to node parser object");
        } else {
            parser->data = NULL;
        }
    } else {
        if(!parser->root) {
            status = panic("invalid data");
        } else {
            if(parser->root->type == list) {
                if(event == list_end) {
                    if(parser->callback(end, parser->root->mark, NULL, parser->context)) {
                        status = panic("failed to process end event");
                    } else {
                        parser->root = parser->root->next;
                    }
                } else if(parser_node(parser, event, string, parser->root->data)) {
                    status = panic("failed to node parser object");
                }
            } else if(parser->root->type == map) {
                if(event == map_end) {
                    if(parser->callback(end, parser->root->mark, NULL, parser->context)) {
                        status = panic("failed to process end event");
                    } else {
                        parser->root = parser->root->next;
                    }
                } else if(event == scalar) {
                    parser->data = map_search(&parser->root->map, string->string);
                    if(!parser->data)
                        status = panic("invalid key - %s", string->string);
                } else {
                    status = panic("invalid event");
                }
            }
        }
    }

    return status;
}

int parser_parse(struct parser * parser, const char * path, struct schema_node * data, parser_cb callback, void * context) {
    int status = 0;
    char * ext;

    parser->root = NULL;
    parser->data = data;
    parser->callback = callback;
    parser->context = context;

    ext = strrchr(path, '.');
    if(!ext) {
        status = panic("failed to get file extension - %s", path);
    } else {
        if(!strcmp(ext, ".txt")) {
            if(csv_parse(&parser->csv, path, parser->size, parser_event, parser))
                status = panic("failed to parse csv object");
        } else if(!strcmp(ext, ".json")) {
            if(json_parse(&parser->json, path, parser->size, parser_event, parser))
                status = panic("failed to parse json object");
        } else if(!strcmp(ext, ".yaml") || !strcmp(ext, ".yml")) {
            if(yaml_parse(&parser->yaml, path, parser->size, parser_event, parser))
                status = panic("failed to parse yaml object");
        }

        if(parser->root)
            status = panic("failed to parse %s", path);
    }

    return status;
}

static inline struct store_node * store_node_create(struct store * store) {
    struct store_node * node;

    node = pool_get(&store->pool);
    if(node) {
        node->offset = 0;
        node->length = store->pool.size - sizeof(struct store_node);
        node->buffer = (char *) node + sizeof(struct store_node);
        node->next = NULL;
    }

    return node;
}

static inline void store_node_destroy(struct store * store, struct store_node * node) {
    memset(node->buffer, 0, node->offset);
    pool_put(&store->pool, node);
}

static inline void * store_node_alloc(struct store_node * node, size_t length) {
    void * object = NULL;

    if(node && node->length >= length) {
        object = node->buffer + node->offset;
        node->offset += length;
        node->length -= length;
    }

    return object;
}

int store_create(struct store * store, size_t size) {
    int status = 0;

    if(pool_create(&store->pool, sizeof(struct store_node) + size, 1)) {
        status = panic("failed to create pool object");
    } else {
        store->root = NULL;
    }

    return status;
}

void store_destroy(struct store * store) {
    store_clear(store);
    pool_destroy(&store->pool);
}

size_t store_size(struct store * store) {
    size_t size = 0;
    struct store_node * node;

    node = store->root;
    while(node) {
        size++;
        node = node->next;
    }

    return size *= store->pool.size;
}

void store_clear(struct store * store) {
    struct store_node * node;

    while(store->root) {
        node = store->root;
        store->root = store->root->next;
        store_node_destroy(store, node);
    }
}

void * store_object(struct store * store, size_t size) {
    void * object;
    struct store_node * node;

    object = store_node_alloc(store->root, size);
    if(!object) {
        node = store_node_create(store);
        if(node) {
            object = store_node_alloc(node, size);
            if(!object) {
                store_node_destroy(store, node);
            } else {
                node->next = store->root;
                store->root = node;
            }
        }
    }

    return object;
}

struct string * store_string(struct store * store, struct string * string) {
    struct string * object;

    object = store_object(store, sizeof(*string) + string->length + 1);
    if(object) {
        object->string = (char *) object + sizeof(*string);
        object->length = string->length;

        memcpy(object->string, string->string, string->length);
        object->string[object->length] = 0;
    }

    return object;
}
