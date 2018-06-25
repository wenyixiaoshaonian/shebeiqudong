#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/irq.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>

struct pin_desc{
    int irq;
	char *name;
	unsigned int pin;
	unsigned int key_val;   
};

struct pin_desc pins_desc[4] = 
{
    {IRQ_EINT0,  "s2",S3C2410_GPF0, KEY_L},
	{IRQ_EINT2,  "s3",S3C2410_GPF2, KEY_S},
	{IRQ_EINT11, "s4",S3C2410_GPG3, KEY_ENTER},
	{IRQ_EINT19 ,"s5",S3C2410_GPG11,KEY_LEFTSHIFT},
};
	
static struct input_dev *buttons_dev;   //定义结构体

static struct pin_desc *irq_pd;

static struct timer_list buttons_timer;    // 定义定时器

static irqreturn_t buttons_irq(int irq, void *dev_id)
{
    irq_pd = (struct pin_desc *)dev_id;

	mod_timer(&buttons_timer,jiffies+HZ/100);
	
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void buttons_timer_function(unsigned long data)
{
  struct pin_desc * pindesc = irq_pd;
	unsigned int pinval;

	if(!pindesc)
		return;
	pinval = s3c2410_gpio_getpin(pindesc->pin);
	if(pinval)
		{
       input_event(buttons_dev,EV_KEY,pindesc->key_val,0);   // 上报事件
       input_sync(buttons_dev);                              // 上报同步事件
	    }
	else 
		{
       input_event(buttons_dev,EV_KEY,pindesc->key_val,1);
	   input_sync(buttons_dev);
	    }
}

static int buttons_init(void)
{
  int i;
  buttons_dev = input_allocate_device();      // 1 分配一个input_dev结构体

  set_bit(EV_KEY,buttons_dev->evbit);          // 2 能产生哪类事件
  set_bit(EV_REP,buttons_dev->evbit);
  
  set_bit(KEY_L,buttons_dev->keybit);
  set_bit(KEY_S,buttons_dev->keybit);
  set_bit(KEY_ENTER,buttons_dev->keybit);
  set_bit(KEY_LEFTSHIFT,buttons_dev->keybit);  //  能产生哪些按键事件

  input_register_device(buttons_dev);          // 3 注册  将buttons放入链表 与evdev.c等文件配对 以调用connect函数
  /*  4. 硬件相关的操作 */
  init_timer(&buttons_timer);                  // 初始化定时器
  buttons_timer.function = buttons_timer_function;  //处理函数
  add_timer(&buttons_timer);
  
  for(i = 0;i < 4;i++)
  	{
  	  request_irq(pins_desc[i].irq, buttons_irq,IRQT_BOTHEDGE,pins_desc[i].name,&pins_desc[i]);     //4 硬件相关的设置，注册中断
  	}
  return 0; 
}

static int buttons_exit(void)
{
  int i;
  for(i = 0;i < 4;i++)
  	{
  	  free_irq(pins_desc[i].irq,&pins_desc[i]);
  	}
  del_timer(&buttons_timer);
  input_unregister_device(buttons_dev);
  input_free_device(buttons_dev);

}



module_init(buttons_init);

module_exit(buttons_exit);

MODULE_LICENSE("GPL");

