#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <stdint.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

struct ring_buffer
{
    uint8_t *data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
};

int32_t rb_init(struct ring_buffer *rb, size_t capacity);
void rb_free(struct ring_buffer *rb);
int32_t rb_is_full(const struct ring_buffer *rb);
int32_t rb_is_empty(const struct ring_buffer *rb);
size_t rb_space(const struct ring_buffer *rb);
size_t rb_append(struct ring_buffer *rb, const uint8_t* buf, size_t len);
ssize_t rb_flush(struct ring_buffer *rb, int32_t fd);

#endif // __RING_BUFFER_H__
