#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	const struct device *sensor = DEVICE_DT_GET(DT_ALIAS(accel0));
	struct sensor_value axes[3];
	int ret;

	if (!device_is_ready(sensor)) {
		printk("Sensor not ready\n");
		return 1;
	}

	ret = sensor_sample_fetch(sensor);
	if (ret != 0) {
		printk("Fetch failed: %d\n", ret);
		return 1;
	}

	ret = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, axes);
	if (ret != 0) {
		printk("Get failed: %d\n", ret);
		return 1;
	}

	printk("PASS: %s, Z = %d micro m/s^2\n",
	       sensor->name, axes[2].val1 * 1000000 + axes[2].val2);

	return 0;
}