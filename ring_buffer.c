#include "ring_buffer.h"

int32_t rb_init(struct ring_buffer *rb, size_t capacity)
{
    rb->data = calloc(capacity, sizeof(uint8_t));
    if (rb->data == NULL) return -1;
    rb->capacity = capacity;
    rb->head = rb->tail = rb->size = 0;
    return 0;
}

void rb_free(struct ring_buffer *rb)
{
    free(rb->data);
    rb->data = NULL;
}

int32_t rb_is_full(const struct ring_buffer *rb)
{
    return rb->size == rb->capacity;
}

int32_t rb_is_empty(const struct ring_buffer *rb)
{
    return rb->size == 0;
}

size_t rb_space(const struct ring_buffer *rb)
{
    return rb->capacity - rb->size;
}

size_t rb_append(struct ring_buffer *rb, const uint8_t* buf, size_t len)
{
    size_t space = rb_space(rb);
    if (space == 0) return 0;

    if (len > space) len = space;

    size_t chunk = rb->capacity - rb->tail;
    if (len <= chunk)
    {
        memcpy(rb->data + rb->tail, buf, len);
        rb->tail += len;
        if (rb->tail == rb->capacity) rb->tail = 0;
    } else {
        memcpy(rb->data + rb->tail, buf, chunk);
        memcpy(rb->data, buf + chunk, len - chunk);
        rb->tail = len - chunk;
    }

    rb->size += len;
    return len;
}

ssize_t rb_flush(struct ring_buffer *rb, int32_t fd)
{
    if (rb->size == 0) return 0;

    struct iovec iov[2] = { 0 };
    int32_t iov_cnt = 0;
    size_t chunk = rb->capacity - rb->head;

    if (rb->size <= chunk)
    {
        iov[0].iov_base = rb->data + rb->head;
        iov[0].iov_len = rb->size;
        iov_cnt = 1;
    } else {
        iov[0].iov_base = rb->data + rb->head;
        iov[0].iov_len = chunk;
        iov[1].iov_base = rb->data;
        iov[1].iov_len = rb->size - chunk;
        iov_cnt = 2;
    }

    ssize_t ret = writev(fd, iov, iov_cnt);
    if (ret > 0)
    {
        rb->size -= ret;
        rb->head = (rb->head + ret) % rb->capacity;
    }

    return ret;
}
