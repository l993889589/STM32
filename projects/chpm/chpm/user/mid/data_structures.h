#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <stddef.h>
#include <stdint.h>

// 哈希表 API
void hash_table_insert(int key, void *value);
void *hash_table_lookup(int key);

// 二分查找 API
void *binary_search(void *array, size_t count, size_t size, int (*compare)(const void*, const void*));

// 队列 API
typedef struct {
    void **items;
    size_t front, rear, size, capacity;
} Queue;

Queue *queue_create(size_t capacity);
int queue_enqueue(Queue *queue, void *item);
void *queue_dequeue(Queue *queue);

#endif // DATA_STRUCTURES_H
