#include "ringbuf.h"

#include <limits.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

struct ringbuf {
	atomic_t write_index;
	atomic_t read_index;
	size_t slots;
	struct accel_sample samples[];
};

static size_t rb_next(const ringbuf_t *rb, size_t index)
{
	++index;

	return index == rb->slots ? 0 : index;
}

ringbuf_t *rb_create(size_t capacity)
{
	ringbuf_t *rb;
	size_t slots;
	size_t bytes;

	if (capacity == 0 || capacity > INT_MAX) {
		return NULL;
	}

	slots = capacity + 1;

	if (slots > (SIZE_MAX - sizeof(*rb)) /
		    sizeof(struct accel_sample)) {
		return NULL;
	}

	bytes = sizeof(*rb) + slots * sizeof(struct accel_sample);
	rb = k_malloc(bytes);

	if (rb == NULL) {
		return NULL;
	}

	atomic_set(&rb->write_index, 0);
	atomic_set(&rb->read_index, 0);
	rb->slots = slots;

	return rb;
}

void rb_destroy(ringbuf_t *rb)
{
	k_free(rb);
}

bool rb_push(ringbuf_t *rb, const struct accel_sample *sample)
{
	size_t write_index;
	size_t next;

	if (rb == NULL || sample == NULL) {
		return false;
	}

	write_index = (size_t)atomic_get(&rb->write_index);
	next = rb_next(rb, write_index);

	if (next == (size_t)atomic_get(&rb->read_index)) {
		return false;
	}

	rb->samples[write_index] = *sample;
	atomic_set(&rb->write_index, (atomic_val_t)next);

	return true;
}

bool rb_pop(ringbuf_t *rb, struct accel_sample *out)
{
	size_t read_index;

	if (rb == NULL || out == NULL) {
		return false;
	}

	read_index = (size_t)atomic_get(&rb->read_index);

	if (read_index == (size_t)atomic_get(&rb->write_index)) {
		return false;
	}

	*out = rb->samples[read_index];
	atomic_set(&rb->read_index,
		   (atomic_val_t)rb_next(rb, read_index));

	return true;
}

size_t rb_size(const ringbuf_t *rb)
{
	size_t read_index;
	size_t write_index;

	if (rb == NULL) {
		return 0;
	}

	read_index = (size_t)atomic_get(&rb->read_index);
	write_index = (size_t)atomic_get(&rb->write_index);

	if (write_index >= read_index) {
		return write_index - read_index;
	}

	return rb->slots - read_index + write_index;
}

size_t rb_capacity(const ringbuf_t *rb)
{
	return rb == NULL ? 0 : rb->slots - 1;
}

size_t rb_drain(ringbuf_t *rb, struct accel_sample *dst, size_t max)
{
	size_t count = 0;

	if (rb == NULL || dst == NULL) {
		return 0;
	}

	while (count < max && rb_pop(rb, &dst[count])) {
		++count;
	}

	return count;
}