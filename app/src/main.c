#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/devicetree.h>

#include "ringbuf.h"

// #define MATCH_OUTPUT_RATE
#define APP_SENSOR_NODE DT_ALIAS(accel0)

#ifdef MATCH_OUTPUT_RATE
#define APP_SAMPLE_RATE_HZ DT_PROP(APP_SENSOR_NODE, odr_hz)
#else
#define APP_SAMPLE_RATE_HZ 100U
#endif

#define APP_SAMPLE_RATE_HZ 100U
#define APP_SAMPLE_PERIOD K_USEC(1000000U / APP_SAMPLE_RATE_HZ)
#define APP_DRAIN_PERIOD K_MSEC(50)
#define APP_DRAIN_MAX 20U
#define APP_CONSUMER_STACK_SIZE 4096
#define APP_CONSUMER_PRIORITY 7


static const struct device *const sensor =
	DEVICE_DT_GET(APP_SENSOR_NODE);

static ringbuf_t *sample_buffer;
static atomic_t dropped_samples;
static atomic_t sensor_errors;

static void sensor_work_handler(struct k_work *work)
{
	struct sensor_value axes[3];
	struct accel_sample sample;
	int ret;

	ARG_UNUSED(work);

	ret = sensor_sample_fetch(sensor);
	if (ret != 0)
	{
		atomic_inc(&sensor_errors);
		return;
	}

	sample.t_ms = k_uptime_get();

	ret = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, axes);
	if (ret != 0)
	{
		atomic_inc(&sensor_errors);
		return;
	}

	sample.ax_ms2 = sensor_value_to_float(&axes[0]);
	sample.ay_ms2 = sensor_value_to_float(&axes[1]);
	sample.az_ms2 = sensor_value_to_float(&axes[2]);

	if (!rb_push(sample_buffer, &sample))
	{
		atomic_inc(&dropped_samples);
	}
}
K_WORK_DEFINE(sensor_work, sensor_work_handler);

static void sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	k_work_submit(&sensor_work);
}
K_TIMER_DEFINE(sample_timer, sample_timer_handler, NULL);
K_TIMER_DEFINE(drain_timer, NULL, NULL);
static void consumer_entry(void *p1, void *p2, void *p3)
{
	struct accel_sample batch[APP_DRAIN_MAX];
	uint64_t consumed = 0;
	int64_t last_stats_ms = k_uptime_get();

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true)
	{
		k_timer_status_sync(&drain_timer);

		size_t count = rb_drain(sample_buffer, batch,
								ARRAY_SIZE(batch));

		for (size_t i = 0; i < count; ++i)
		{
			const struct accel_sample *sample = &batch[i];

			printk("{\"t_ms\":%lld,\"x_ms2\":%.6f,"
				   "\"y_ms2\":%.6f,\"z_ms2\":%.6f}\n",
				   (long long)sample->t_ms,
				   (double)sample->ax_ms2,
				   (double)sample->ay_ms2,
				   (double)sample->az_ms2);
		}

		consumed += count;

		int64_t now_ms = k_uptime_get();

		if (now_ms - last_stats_ms >= 1000)
		{
			printk("{\"type\":\"stats\",\"consumed\":%llu,"
				   "\"queued\":%u,\"dropped\":%ld,"
				   "\"sensor_errors\":%ld}\n",
				   (unsigned long long)consumed,
				   (unsigned int)rb_size(sample_buffer),
				   (long)atomic_get(&dropped_samples),
				   (long)atomic_get(&sensor_errors));

			last_stats_ms = now_ms;
		}
	}
}

K_THREAD_DEFINE(consumer_thread, APP_CONSUMER_STACK_SIZE,
		consumer_entry, NULL, NULL, NULL,
		APP_CONSUMER_PRIORITY, 0, SYS_FOREVER_MS);

int main(void)
{
	if (!device_is_ready(sensor))
	{
		printk("Sensor not ready\n");
		return 1;
	}

	sample_buffer = rb_create(RB_CAPACITY);
	if (sample_buffer == NULL)
	{
		printk("Ring buffer allocation failed\n");
		return 1;
	}

	k_timer_start(&sample_timer, APP_SAMPLE_PERIOD, APP_SAMPLE_PERIOD);
	k_timer_start(&drain_timer, APP_DRAIN_PERIOD, APP_DRAIN_PERIOD);
	k_thread_start(consumer_thread);

	return 0;
}