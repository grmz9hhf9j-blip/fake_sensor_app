#include <zephyr/ztest.h>

#include "ringbuf.h"

static ringbuf_t *buffer;

static void before_test(void *fixture)
{
	ARG_UNUSED(fixture);

	buffer = rb_create(3);
	zassert_not_null(buffer);
}

static void after_test(void *fixture)
{
	ARG_UNUSED(fixture);

	rb_destroy(buffer);
	buffer = NULL;
}

ZTEST(ringbuf, test_push_pop_basic)
{
	struct accel_sample input = {
		.t_ms = 1234,
		.ax_ms2 = 1.25f,
		.ay_ms2 = -2.5f,
		.az_ms2 = 9.5f,
	};
	struct accel_sample output;

	zassert_equal(rb_capacity(buffer), 3);
	zassert_equal(rb_size(buffer), 0);
	zassert_false(rb_pop(buffer, &output));

	zassert_true(rb_push(buffer, &input));
	zassert_equal(rb_size(buffer), 1);

	zassert_true(rb_pop(buffer, &output));
	zassert_equal(output.t_ms, input.t_ms);
	zassert_true(output.ax_ms2 == input.ax_ms2);
	zassert_true(output.ay_ms2 == input.ay_ms2);
	zassert_true(output.az_ms2 == input.az_ms2);

	zassert_equal(rb_size(buffer), 0);
	zassert_false(rb_pop(buffer, &output));
}

ZTEST(ringbuf, test_wrap)
{
	struct accel_sample sample = {0};
	struct accel_sample output;

	for (int64_t i = 1; i <= 3; ++i) {
		sample.t_ms = i;
		zassert_true(rb_push(buffer, &sample));
	}

	for (int64_t i = 1; i <= 2; ++i) {
		zassert_true(rb_pop(buffer, &output));
		zassert_equal(output.t_ms, i);
	}

	for (int64_t i = 4; i <= 5; ++i) {
		sample.t_ms = i;
		zassert_true(rb_push(buffer, &sample));
	}

	zassert_equal(rb_size(buffer), 3);

	for (int64_t i = 3; i <= 5; ++i) {
		zassert_true(rb_pop(buffer, &output));
		zassert_equal(output.t_ms, i);
	}

	zassert_equal(rb_size(buffer), 0);
	zassert_false(rb_pop(buffer, &output));
}

ZTEST(ringbuf, test_full_and_drop)
{
	struct accel_sample sample = {0};
	struct accel_sample output;
	size_t capacity = rb_capacity(buffer);

	for (size_t i = 0; i < capacity; ++i) {
		sample.t_ms = (int64_t)i;
		sample.ax_ms2 = (float)i;
		sample.ay_ms2 = -(float)i;
		sample.az_ms2 = (float)i + 0.5f;

		zassert_true(rb_push(buffer, &sample));
	}

	zassert_equal(rb_size(buffer), capacity);

	sample = (struct accel_sample) {
		.t_ms = 999,
		.ax_ms2 = 999.0f,
		.ay_ms2 = 999.0f,
		.az_ms2 = 999.0f,
	};

	zassert_false(rb_push(buffer, &sample));
	zassert_equal(rb_size(buffer), capacity);

	for (size_t i = 0; i < capacity; ++i) {
		zassert_true(rb_pop(buffer, &output));
		zassert_equal(output.t_ms, (int64_t)i);
		zassert_true(output.ax_ms2 == (float)i);
		zassert_true(output.ay_ms2 == -(float)i);
		zassert_true(output.az_ms2 == (float)i + 0.5f);
	}

	zassert_false(rb_pop(buffer, &output));
	zassert_equal(rb_size(buffer), 0);

	zassert_true(rb_push(buffer, &sample));
	zassert_true(rb_pop(buffer, &output));
	zassert_equal(output.t_ms, 999);
}

ZTEST(ringbuf, test_bulk_drain)
{
	struct accel_sample sample = {0};
	struct accel_sample output[4] = {0};

	for (int64_t i = 1; i <= 3; ++i) {
		sample.t_ms = i;
		zassert_true(rb_push(buffer, &sample));
	}

	output[0].t_ms = 999;

	zassert_equal(rb_drain(buffer, output, 0), 0);
	zassert_equal(output[0].t_ms, 999);
	zassert_equal(rb_size(buffer), 3);

	output[2].t_ms = 999;

	zassert_equal(rb_drain(buffer, output, 2), 2);
	zassert_equal(output[0].t_ms, 1);
	zassert_equal(output[1].t_ms, 2);
	zassert_equal(output[2].t_ms, 999);
	zassert_equal(rb_size(buffer), 1);

	for (int64_t i = 4; i <= 5; ++i) {
		sample.t_ms = i;
		zassert_true(rb_push(buffer, &sample));
	}

	output[3].t_ms = 999;

	zassert_equal(rb_drain(buffer, output, 4), 3);
	zassert_equal(output[0].t_ms, 3);
	zassert_equal(output[1].t_ms, 4);
	zassert_equal(output[2].t_ms, 5);
	zassert_equal(output[3].t_ms, 999);
	zassert_equal(rb_size(buffer), 0);

	output[0].t_ms = 999;

	zassert_equal(rb_drain(buffer, output, 4), 0);
	zassert_equal(output[0].t_ms, 999);
}


#define SPSC_SAMPLE_COUNT 4096U
#define SPSC_ATTEMPT_LIMIT (SPSC_SAMPLE_COUNT * 16U)
#define SPSC_STACK_SIZE 2048
#define SPSC_PRIORITY 5

static struct k_thread producer_thread;
static struct k_thread consumer_thread;

K_THREAD_STACK_DEFINE(producer_stack, SPSC_STACK_SIZE);
K_THREAD_STACK_DEFINE(consumer_stack, SPSC_STACK_SIZE);

struct spsc_result {
	uint32_t count;
	bool bad_sample;
};

static struct accel_sample spsc_sample(uint32_t sequence)
{
	return (struct accel_sample) {
		.t_ms = ((int64_t)sequence << 32) | sequence,
		.ax_ms2 = (float)sequence,
		.ay_ms2 = -(float)sequence,
		.az_ms2 = (float)sequence + 0.5f,
	};
}

static bool spsc_matches(const struct accel_sample *sample,
			 uint32_t sequence)
{
	struct accel_sample expected = spsc_sample(sequence);

	return sample->t_ms == expected.t_ms &&
	       sample->ax_ms2 == expected.ax_ms2 &&
	       sample->ay_ms2 == expected.ay_ms2 &&
	       sample->az_ms2 == expected.az_ms2;
}

static void producer_entry(void *p1, void *p2, void *p3)
{
	struct spsc_result *result = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t attempt = 0;
	     attempt < SPSC_ATTEMPT_LIMIT &&
	     result->count < SPSC_SAMPLE_COUNT;
	     ++attempt) {
		struct accel_sample sample = spsc_sample(result->count);

		if (rb_push(buffer, &sample)) {
			++result->count;
		} else {
			k_yield();
		}
	}
}

static void consumer_entry(void *p1, void *p2, void *p3)
{
	struct spsc_result *result = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t attempt = 0;
	     attempt < SPSC_ATTEMPT_LIMIT &&
	     result->count < SPSC_SAMPLE_COUNT;
	     ++attempt) {
		struct accel_sample sample;

		if (!rb_pop(buffer, &sample)) {
			k_yield();
			continue;
		}

		if (!spsc_matches(&sample, result->count)) {
			result->bad_sample = true;
			return;
		}

		++result->count;
	}
}

ZTEST(ringbuf, test_spsc_threads)
{
	struct spsc_result produced = {0};
	struct spsc_result consumed = {0};
	int producer_status;
	int consumer_status;

	k_thread_create(&producer_thread, producer_stack,
			K_THREAD_STACK_SIZEOF(producer_stack),
			producer_entry, &produced, NULL, NULL,
			SPSC_PRIORITY, 0, K_FOREVER);

	k_thread_create(&consumer_thread, consumer_stack,
			K_THREAD_STACK_SIZEOF(consumer_stack),
			consumer_entry, &consumed, NULL, NULL,
			SPSC_PRIORITY, 0, K_FOREVER);

	k_thread_start(&producer_thread);
	k_thread_start(&consumer_thread);

	producer_status = k_thread_join(&producer_thread, K_SECONDS(3));
	consumer_status = k_thread_join(&consumer_thread, K_SECONDS(3));

	if (producer_status != 0) {
		k_thread_abort(&producer_thread);
	}

	if (consumer_status != 0) {
		k_thread_abort(&consumer_thread);
	}

	zassert_equal(producer_status, 0, "Producer did not finish");
	zassert_equal(consumer_status, 0, "Consumer did not finish");
	zassert_false(consumed.bad_sample,
		      "Sample corrupted or received out of order");
	zassert_equal(produced.count, SPSC_SAMPLE_COUNT,
		      "Producer exhausted its retry budget");
	zassert_equal(consumed.count, SPSC_SAMPLE_COUNT,
		      "Consumer exhausted its retry budget");
	zassert_equal(rb_size(buffer), 0);
}


ZTEST_SUITE(ringbuf, NULL, NULL, before_test, after_test, NULL);