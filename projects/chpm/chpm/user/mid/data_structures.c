#include "data_structures.h"
#include <stdlib.h>
#include <string.h>

// 哈希表（示例）
#define HASH_TABLE_SIZE 128
static void *hash_table[HASH_TABLE_SIZE];

void hash_table_insert(int key, void *value) {
    int index = key % HASH_TABLE_SIZE;
    hash_table[index] = value;
}

void *hash_table_lookup(int key) {
    int index = key % HASH_TABLE_SIZE;
    return hash_table[index];
}

// 二分查找（示例）
void *binary_search(void *array, size_t count, size_t size, int (*compare)(const void*, const void*)) {
    size_t left = 0, right = count - 1;
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        void *mid_element = (char*)array + mid * size;
        int cmp = compare(mid_element, array);
        if (cmp == 0) return mid_element;
        if (cmp < 0) left = mid + 1;
        else right = mid - 1;
    }
    return NULL;
}

// 队列实现
Queue *queue_create(size_t capacity) {
    Queue *queue = (Queue*)malloc(sizeof(Queue));
    queue->items = (void**)malloc(capacity * sizeof(void*));
    queue->front = queue->rear = 0;
    queue->size = 0;
    queue->capacity = capacity;
    return queue;
}

int queue_enqueue(Queue *queue, void *item) {
    if (queue->size >= queue->capacity) return -1;
    queue->items[queue->rear++] = item;
    queue->size++;
    return 0;
}

void *queue_dequeue(Queue *queue) {
    if (queue->size == 0) return NULL;
    void *item = queue->items[queue->front++];
    queue->size--;
    return item;
}
