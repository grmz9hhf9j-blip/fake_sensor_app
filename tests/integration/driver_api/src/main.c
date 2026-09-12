#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "ringbuf.h"

#define SAMPLE_COUNT 20U
#define DRAIN_MAX 20U
#define CONSUMER_STACK_SIZE 4096
#define CONSUMER_PRIORITY 5

static const struct device *const sensor =
	DEVICE_DT_GET(DT_ALIAS(accel0));

static ringbuf_t *buffer;
static uint32_t produced;
static int producer_error;

static struct k_thread consumer_thread;

K_THREAD_STACK_DEFINE(consumer_stack, CONSUMER_STACK_SIZE);

struct consumer_result {
	uint32_t count;
	int64_t first_timestamp;
	int64_t last_timestamp;
	float first_x;
	float first_y;
	bool changed;
	int error;
};

static struct consumer_result result;

static bool sample_is_finite(const struct accel_sample *sample)
{
	return isfinite(sample->ax_ms2) &&
	       isfinite(sample->ay_ms2) &&
	       isfinite(sample->az_ms2);
}

static void sample_work_handler(struct k_work *work)
{
	struct sensor_value axes[3];
	struct accel_sample sample;
	int ret;

	ARG_UNUSED(work);

	if (produced >= SAMPLE_COUNT || producer_error != 0) {
		return;
	}

	ret = sensor_sample_fetch(sensor);
	if (ret != 0) {
		producer_error = ret;
		return;
	}

	sample.t_ms = k_uptime_get();

	ret = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, axes);
	if (ret != 0) {
		producer_error = ret;
		return;
	}

	sample.ax_ms2 = sensor_value_to_float(&axes[0]);
	sample.ay_ms2 = sensor_value_to_float(&axes[1]);
	sample.az_ms2 = sensor_value_to_float(&axes[2]);

	if (!sample_is_finite(&sample)) {
		producer_error = -ERANGE;
		return;
	}

	if (!rb_push(buffer, &sample)) {
		producer_error = -ENOSPC;
		return;
	}

	++produced;
}

K_WORK_DEFINE(sample_work, sample_work_handler);

static void sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	k_work_submit(&sample_work);
}

K_TIMER_DEFINE(sample_timer, sample_timer_handler, NULL);
K_TIMER_DEFINE(drain_timer, NULL, NULL);

static void consumer_entry(void *p1, void *p2, void *p3)
{
	struct accel_sample batch[DRAIN_MAX];
	int64_t deadline = k_uptime_get() + 1000;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (result.count < SAMPLE_COUNT) {
		k_timer_status_sync(&drain_timer);

		if (k_uptime_get() >= deadline) {
			result.error = -ETIMEDOUT;
			return;
		}

		size_t count = rb_drain(buffer, batch, ARRAY_SIZE(batch));

		for (size_t i = 0; i < count; ++i) {
			const struct accel_sample *sample = &batch[i];

			if (!sample_is_finite(sample)) {
				result.error = -ERANGE;
				return;
			}

			if (sample->t_ms > k_uptime_get()) {
				result.error = -EINVAL;
				return;
			}

			if (result.count == 0) {
				result.first_timestamp = sample->t_ms;
				result.first_x = sample->ax_ms2;
				result.first_y = sample->ay_ms2;
			} else {
				if (sample->t_ms < result.last_timestamp) {
					result.error = -EINVAL;
					return;
				}

				if (sample->ax_ms2 != result.first_x ||
				    sample->ay_ms2 != result.first_y) {
					result.changed = true;
				}
			}

			result.last_timestamp = sample->t_ms;
			++result.count;
		}
	}
}

ZTEST(driver_api, test_sensor_to_buffer_pipeline)
{
	struct k_work_sync sync;
	int consumer_status;
	size_t remaining;

	zassert_true(device_is_ready(sensor), "Sensor is not ready");

	buffer = rb_create(RB_CAPACITY);
	zassert_not_null(buffer, "Buffer allocation failed");

	produced = 0;
	producer_error = 0;
	result = (struct consumer_result){0};

	k_thread_create(&consumer_thread, consumer_stack,
			K_THREAD_STACK_SIZEOF(consumer_stack),
			consumer_entry, NULL, NULL, NULL,
			CONSUMER_PRIORITY, 0, K_FOREVER);

	k_timer_start(&sample_timer, K_MSEC(10), K_MSEC(10));
	k_timer_start(&drain_timer, K_MSEC(50), K_MSEC(50));
	k_thread_start(&consumer_thread);

	consumer_status = k_thread_join(&consumer_thread, K_SECONDS(2));

	k_timer_stop(&sample_timer);
	k_work_cancel_sync(&sample_work, &sync);

	if (consumer_status != 0) {
		k_thread_abort(&consumer_thread);
	}

	k_timer_stop(&drain_timer);

	remaining = rb_size(buffer);
	rb_destroy(buffer);
	buffer = NULL;

	zassert_equal(consumer_status, 0, "Consumer did not finish");
	zassert_equal(producer_error, 0,
		      "Producer failed: %d", producer_error);
	zassert_equal(result.error, 0,
		      "Consumer validation failed: %d", result.error);
	zassert_equal(produced, SAMPLE_COUNT);
	zassert_equal(result.count, SAMPLE_COUNT);
	zassert_equal(remaining, 0);
	zassert_true(result.last_timestamp > result.first_timestamp,
		     "Capture timestamps did not advance");
	zassert_true(result.changed, "X/Y readings never changed");
}

ZTEST_SUITE(driver_api, NULL, NULL, NULL, NULL, NULL);