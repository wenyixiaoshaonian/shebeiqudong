#include <linux/module.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <linux/poll.h>
static struct class *seventhdrv_class;
static struct class_device *seventhdrv_class_dev;

volatile unsigned long *gpfcon = NULL;
volatile unsigned long *gpfdat = NULL;

volatile unsigned long *gpgcon = NULL;
volatile unsigned long *gpgdat = NULL;


static unsigned char  key_val;     

static DECLARE_WAIT_QUEUE_HEAD(button_waitq);

static volatile int ev_press = 0;  // 中断服务程序将它置一  seventh_drv_read 将它清零

static struct fasync_struct *button_async;

static atomic_t canopen = ATOMIC_INIT(1);     //定义原子变量canopen并初始化为1

static struct timer_list buttons_timer;    // 定义定时器

static DECLARE_MUTEX(button_lock);         //定义互斥锁

static struct pin_desc *irq_pd;
struct pin_desc{
	unsigned int pin;
	unsigned int key_val;   
};
// 键值按下时:0x01,0x02,0x03,0x04
// 键值松开是:0x81,0x81,0x83,0x84
struct pin_desc pins_desc[4] = 
{
    { S3C2410_GPF0,0X01},
	{ S3C2410_GPF2,0X02},
	{ S3C2410_GPG3,0X03},
	{ S3C2410_GPG11,0X04},


};
int major;
/*确定按键值*/
static irqreturn_t buttons_irq(int irq, void *dev_id)
{
    irq_pd = (struct pin_desc *)dev_id;

	mod_timer(&buttons_timer,jiffies+HZ/100);
	
	return IRQ_RETVAL(IRQ_HANDLED);
}


static int seventh_dev_open(struct inode *inode, struct file *file)
{
 // *gpfcon &= ~((0x3 << (0*2)) | (0x3 << (2*2))); 
 // *gpgcon &= ~((0x3 << (3*2)) | (0x3 << (11*2)));输入引脚
 /*if(!atomic_dec_and_test(&canopen))
	 {
	   atomic_inc(&canopen);
	   return -EBUSY;
	 }                   // 原子操作
 */

 if(file->f_flags & O_NONBLOCK)
 	{
 	  if (down_trylock(&button_lock))
	  	return -EBUSY;
 	}
 else                      //阻塞
 down(&button_lock) ;   // 信号量  
 request_irq(IRQ_EINT0, buttons_irq,IRQT_BOTHEDGE,"s2",&pins_desc[0]);
 request_irq(IRQ_EINT2, buttons_irq,IRQT_BOTHEDGE,"s3",&pins_desc[1]);
 request_irq(IRQ_EINT11,buttons_irq,IRQT_BOTHEDGE,"s4",&pins_desc[2]);
 request_irq(IRQ_EINT19,buttons_irq,IRQT_BOTHEDGE,"s5",&pins_desc[3]);
  return 0;
}

static int seventh_dev_read(struct file *file, char __user *buf, size_t size, loff_t *offp)
{

    if(size != 1)
    return -EINVAL;
   if(file->f_flags & O_NONBLOCK)
   	{
      if(!ev_press)
	  	return -EAGAIN;
   	}
  else
  	{
    wait_event_interruptible(button_waitq, ev_press);
  	}
  copy_to_user(buf,&key_val,1);    //如有按键按下，将键值传到用户程序

  ev_press = 0;
  return 1;
}

static int seventh_drv_close (struct inode *inode, struct file *file)
{
      //atomic_inc(&canopen);  // 原子操作
		up(&button_lock);    //信号量
		free_irq(IRQ_EINT0, &pins_desc[0]);
		free_irq(IRQ_EINT2, &pins_desc[1]);
		free_irq(IRQ_EINT11,&pins_desc[2]);
		free_irq(IRQ_EINT19,&pins_desc[3]);
 		return 0;
}
static ssize_t seventh_dev_write(struct file *file, const char __user *buf, size_t size, loff_t * ppos)
{
  return 0;
}

static	 unsigned seventh_drv_poll(struct file *file, poll_table *wait)
{
				unsigned int mask = 0;
				
				poll_wait(file,&button_waitq,wait);
	
				if(ev_press)
					mask |= POLLIN|POLLRDNORM;
				 
				return mask;
}  
static int seventh_drv_fasync (int fd, struct file *filp, int on)
	
{
        printk("driver:seventh_drv_fasync\n");
		return fasync_helper (fd, filp, on, &button_async);
}


static struct file_operations seventh_dev_fops = {
			.owner	=	THIS_MODULE,	
			.open	=	seventh_dev_open,		   
			.write	=	seventh_dev_write,	 
			.read	=	seventh_dev_read,
			.release=   seventh_drv_close,
			.poll   =   seventh_drv_poll,
			.fasync =   seventh_drv_fasync, 
};
			
static void buttons_timer_function(unsigned long data)
{
    struct pin_desc * pindesc = irq_pd;
	unsigned int pinval;

	if(!pindesc)
		return;
	pinval = s3c2410_gpio_getpin(pindesc->pin);
  //  printk("pinval:%d",pinval);
	if(pinval)
		{
       key_val =0x80 |pindesc-> key_val;
	}
	else 
		{
       key_val =   pindesc-> key_val;
	}

	
    ev_press = 1;                               // 中断发生
	wake_up_interruptible(&button_waitq);       // 唤醒休眠进程

	kill_fasync (&button_async, SIGIO, POLL_IN);
}


static int seventh_drv_init(void)
{
  
  init_timer(&buttons_timer);
//	buttons_timer.data	 = (unsigned long) SCpnt;                      //传入参数 给处理函数用
//	buttons_timer.expires  = jiffies + 100*HZ;                         // 10s 超时时间
	buttons_timer.function = buttons_timer_function;  //处理函数
  add_timer(&buttons_timer);   
  //将定时器告诉内核，超时后调用处理函数
  major = register_chrdev(0,"seventh_drv", &seventh_dev_fops);
  
  seventhdrv_class = class_create(THIS_MODULE, "seventh_drv");

  seventhdrv_class_dev = class_device_create(seventhdrv_class, NULL, MKDEV(major, 0), NULL, "buttons");   // /dev/buttons
  
  gpfcon = (volatile unsigned long*)ioremap(0x56000050,16);

  gpfdat = gpfcon+1;

  gpgcon = (volatile unsigned long*)ioremap(0x56000060,16);

  gpgdat = gpgcon+1;
  
  return 0;
}

static int seventh_drv_exit(void)
{
  major = unregister_chrdev(major,"seventh_drv");
  class_device_unregister(seventhdrv_class_dev);
  class_destroy(seventhdrv_class);

  iounmap(gpfcon);
  iounmap(gpgcon);
  return 0;
}



module_init(seventh_drv_init);
module_exit(seventh_drv_exit);

MODULE_LICENSE("GPL");




