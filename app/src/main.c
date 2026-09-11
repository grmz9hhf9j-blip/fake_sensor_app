#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

// #define MATCH_OUTPUT_RATE
#define APP_SENSOR_NODE DT_ALIAS(accel0)

#ifdef MATCH_OUTPUT_RATE
#define APP_SAMPLE_RATE_HZ DT_PROP(APP_SENSOR_NODE, odr_hz)
#else
#define APP_SAMPLE_RATE_HZ 100U
#endif

#define APP_SAMPLE_PERIOD K_USEC(DIV_ROUND_UP(1000000U, APP_SAMPLE_RATE_HZ))

static const struct device *const sensor =
	DEVICE_DT_GET(APP_SENSOR_NODE);

static void sensor_work_handler(struct k_work *work)
{
	struct sensor_value axes[3];
	int64_t timestamp_ms;
	int ret;

	ARG_UNUSED(work);

	ret = sensor_sample_fetch(sensor);
	if (ret != 0)
	{
		printk("Fetch failed: %d\n", ret);
		return;
	}

	timestamp_ms = k_uptime_get();

	ret = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, axes);
	if (ret != 0)
	{
		printk("Get failed %d\n", ret);
		return;
	}
	printk("t=%lld ms, X=%lld, Y=%lld, Z=%lld micro m/s^2\n",
		   (long long)timestamp_ms,
		   (long long)sensor_value_to_micro(&axes[0]),
		   (long long)sensor_value_to_micro(&axes[1]),
		   (long long)sensor_value_to_micro(&axes[2]));
}
K_WORK_DEFINE(sensor_work, sensor_work_handler);

static void sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	k_work_submit(&sensor_work);
}
K_TIMER_DEFINE(sample_timer, sample_timer_handler, NULL);

int main(void)
{
	if (!device_is_ready(sensor))
	{
		printk("Sensor not ready\n");
		return 1;
	}
	printk("App sampling rate: %u Hz\n", (unsigned int)APP_SAMPLE_RATE_HZ);
	k_timer_start(&sample_timer, APP_SAMPLE_PERIOD, APP_SAMPLE_PERIOD);

	return 0;
}