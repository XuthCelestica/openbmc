/*
 *  The i2c driver for CPU error
 *
 * Copyright 2019-present Celestica. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

//#define DEBUG

#include <linux/errno.h>
#include <linux/module.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#ifdef DEBUG

#define CPU_ERROR_DEBUG(fmt, ...) do {                     \
    printk(KERN_DEBUG "%s:%d " fmt "\n",            \
           __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
  } while (0)

#else /* !DEBUG */

#define CPU_ERROR_DEBUG(fmt, ...)
#endif

#define CPU_MCA_GPIO 13
#define CPU_AER_0_GPIO 14
#define CPU_AER_1_GPIO 15
#define CPU_AER_2_GPIO 208

#define MAC_ERROR_POLL_TIME 10 //10s

static struct gpio_data_struct {
	int gpio;
	int irq;
};

static struct cpu_error_struct {
	struct gpio_data_struct mca_error;
	struct gpio_data_struct aer_0_error;
	struct gpio_data_struct aer_1_error;
	struct gpio_data_struct aer_2_error;
};

static struct cpu_error_struct *cpu_error;
static int mca_low = 0;
static int mca_high = 0;
static struct workqueue_struct *mca_wq = NULL;
static struct delayed_work mca_dwq;

static irqreturn_t cpu_error_handler(int irq, void *dev_id)
{
	int gpio;
	int value;

	gpio = irq_to_gpio(irq);
	value = ast_get_gpio_value(gpio);
	CPU_ERROR_DEBUG("irq=%d, gpio=%d, value=%d\n", irq, gpio, value);
	switch(gpio) {
		case CPU_AER_0_GPIO:
			if(value)
				printk(KERN_ALERT "AER ERR[0] is recovered\n");
			else
				printk(KERN_ALERT "AER ERR[0] is asserted\n");
			break;
		case CPU_AER_1_GPIO:
			if(value)
				printk(KERN_ALERT "AER ERR[1] is recovered\n");
			else
				printk(KERN_ALERT "AER ERR[1] is asserted\n");
			break;
		case CPU_AER_2_GPIO:
			if(value)
				printk(KERN_ALERT "AER ERR[2] is recovered\n");
			else
				printk(KERN_ALERT "AER ERR[2] is asserted\n");
			break;
		case CPU_MCA_GPIO:
			if(value) {
				mca_high++;
			} else {
				mca_low++;
			}
			break;
	}

	return IRQ_HANDLED;
}

static void mca_error_delay_worker(struct work_struct *work)
{
	struct workqueue_struct *wq = mca_wq;

	if(mca_low == 1) {
		printk(KERN_ALERT "CATERR# remains asserted\n");
	} else if(mca_low >= 8) {
		printk(KERN_ALERT "CATERR# is asserted for 16 BCLKs\n");
	}

	if(mca_high == 1) {
		printk(KERN_ALERT "CATERR# is recovered\n");
	}
	mca_low = 0;
	mca_high = 0;

	queue_delayed_work(wq, work, MAC_ERROR_POLL_TIME * HZ);

	return;
}


static int gpio_data_init(struct cpu_error_struct *cpu_error)
{
	int ret;

	if(!cpu_error) {
		printk(KERN_ERR "%s: cpu error gpio data is null!\n", __func__);
		return -1;
	}
	cpu_error->mca_error.gpio = CPU_MCA_GPIO;
	cpu_error->mca_error.irq = gpio_to_irq(CPU_MCA_GPIO);
	CPU_ERROR_DEBUG("mca_error gpio=%d, irq = %d\n", cpu_error->mca_error.gpio, cpu_error->mca_error.irq);
	ret = request_irq(cpu_error->mca_error.irq, cpu_error_handler , IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "MCA error interrupt", NULL);
	if(ret) {
		printk(KERN_ERR "request irq %d IRQF_TRIGGER_FALLING error: %d!\n", cpu_error->mca_error.irq, ret);
		return -1;
	}

	cpu_error->aer_0_error.gpio = CPU_AER_0_GPIO;
	cpu_error->aer_0_error.irq = gpio_to_irq(CPU_AER_0_GPIO);
	CPU_ERROR_DEBUG("aer_0_error gpio=%d, irq = %d\n", cpu_error->aer_0_error.gpio, cpu_error->aer_0_error.irq);
	ret = request_irq(cpu_error->aer_0_error.irq, cpu_error_handler , IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "AER_0 error interrupt", NULL);
	if(ret) {
		printk(KERN_ERR "request irq %d IRQF_TRIGGER_FALLING error: %d!\n", cpu_error->aer_0_error.irq, ret);
		return -1;
	}

	cpu_error->aer_1_error.gpio = CPU_AER_1_GPIO;
	cpu_error->aer_1_error.irq = gpio_to_irq(CPU_AER_1_GPIO);
	CPU_ERROR_DEBUG("aer_1_error gpio=%d, irq = %d\n", cpu_error->aer_1_error.gpio, cpu_error->aer_1_error.irq);
	ret = request_irq(cpu_error->aer_1_error.irq, cpu_error_handler , IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "AER_1 error interrupt", NULL);
	if(ret) {
		printk(KERN_ERR "request irq %d error: %d!\n", cpu_error->aer_1_error.irq, ret);
		return -1;
	}

	cpu_error->aer_2_error.gpio = CPU_AER_2_GPIO;
	cpu_error->aer_2_error.irq = gpio_to_irq(CPU_AER_2_GPIO);
	CPU_ERROR_DEBUG("aer_2_error gpio=%d, irq = %d\n", cpu_error->aer_2_error.gpio, cpu_error->aer_2_error.irq);
	ret = request_irq(cpu_error->aer_2_error.irq, cpu_error_handler , IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "AER_2 error interrupt", NULL);
	if(ret) {
		printk(KERN_ERR "request irq %d IRQF_TRIGGER_FALLING error: %d!\n", cpu_error->aer_2_error.irq, ret);
		return -1;
	}

	return 0;
}


static int __init cpu_error_init(void)
{
	int ret;
	int irqno;

	printk(KERN_INFO "%s: run\n", __func__);
	cpu_error = (struct cpu_error_struct *)kzalloc(sizeof(struct cpu_error_struct), GFP_KERNEL);
	if(!cpu_error) {
		printk(KERN_ERR "malloc struct cpu_error_struct error!\n");
		return -1;
	}

	if(gpio_data_init(cpu_error) < 0)
		return -1;

	mca_wq = create_workqueue("CPU error workqueue");
	if (mca_wq == NULL) {
		printk(KERN_ERR "%s: error creating CPU error workqueue", __func__);
		return -1;
	} else {
		INIT_DELAYED_WORK(&mca_dwq, mca_error_delay_worker);
		queue_delayed_work(mca_wq, &mca_dwq, MAC_ERROR_POLL_TIME * HZ);
	}

	CPU_ERROR_DEBUG("done\n");
	return 0;
}

static void __exit cpu_error_exit(void)
{
	kfree(cpu_error);
	return ;
}

MODULE_AUTHOR("Mickey Zhan");
MODULE_DESCRIPTION("CPU Error Driver");
MODULE_LICENSE("GPL");

module_init(cpu_error_init);
module_exit(cpu_error_exit);
