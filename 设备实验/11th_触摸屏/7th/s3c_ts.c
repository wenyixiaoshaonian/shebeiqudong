
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/plat-s3c24xx/ts.h>

#include <asm/arch/regs-adc.h>
#include <asm/arch/regs-gpio.h>

struct s3c_ts_regs{
   unsigned long adccon;
   unsigned long adctsc;
   unsigned long adcdly;
   unsigned long adcdat0;
   unsigned long adcdat1;
   unsigned long adcupdn;
};

static struct input_dev *s3c_ts_dev;
static volatile struct s3c_ts_regs *s3c_ts_regs;

static struct timer_list ts_timer;
static void enter_wait_pen_down_mode(void)
{
  s3c_ts_regs->adctsc = 0xd3;
}

static void enter_wait_pen_up_mode(void)
{
  s3c_ts_regs->adctsc = 0x1d3;
}

static void enter_measure_xy_mode(void)
{
	s3c_ts_regs->adctsc = (1<<3) | (1<<2);

}

static void start_adc (void)
{
  s3c_ts_regs->adccon |= (1<<0);
}

static int s3c_filter_ts(int x[],int y[])
{
#define ERR_LIMIT 10

  int avr_x,avr_y;
  int det_x,det_y;
  avr_x = (x[0] + x[1])/2;
  avr_y = (y[0] + y[1])/2;

  det_x = (x[2] > avr_x) ? (x[2] - avr_x) : (avr_x - x[2]);
  det_y = (y[2] > avr_y) ? (y[2] - avr_y) : (avr_y - y[2]);

  if((det_x > ERR_LIMIT) || (det_y > ERR_LIMIT))
  	return 0;
  
  avr_x = (x[1] + x[2])/2;
  avr_y = (y[1] + y[2])/2;

  det_x = (x[3] > avr_x) ? (x[3] - avr_x) : (avr_x - x[3]);
  det_y = (y[3] > avr_y) ? (y[3] - avr_y) : (avr_y - y[3]);
  
  if((det_x > ERR_LIMIT) || (det_y > ERR_LIMIT))
  	return 0;

  return 1;
}

void static s3c_ts_timer_functions(unsigned long data)
{
  if(s3c_ts_regs->adcdat0 & (1<<15))
  	{
  	  enter_wait_pen_down_mode();
  	}
  else
  	{
  	  enter_measure_xy_mode();
      start_adc();
  	}
}
static irqreturn_t pen_down_up_irq(int irq, void *dev_id)
{
  if(s3c_ts_regs->adcdat0 & (1<<15))
  	{
    printk("pen up \n");
    enter_wait_pen_down_mode();
  	}
  else 
  	{
// 	printk("pen down \n");
//	enter_wait_pen_up_mode();
    enter_measure_xy_mode();
    start_adc();
  	}
  return IRQ_HANDLED;
}

static irqreturn_t adc_irq(int irq, void *dev_id)
{
  /*  优化措施2.
   *  如果ADC完成时，发现触摸笔已经松开，则丢弃 
  */
  static int cnt = 0;
  static int x[4],y[4];
  int adcdat0,adcdat1;
  adcdat0 = s3c_ts_regs->adcdat0;
  adcdat1 = s3c_ts_regs->adcdat1;
  if(adcdat0 & (1<<15))  //已经松开
  	{
      cnt = 0;
	  enter_wait_pen_down_mode();
  	}
  else
  	{
	  //printk("adc_irq = %d,x = %d, y = %d\n",++cnt,s3c_ts_regs->adcdat0 & 0x3ff,s3c_ts_regs->adcdat1 & 0x3ff);
	  /*  优化措施3. 多次测量，去平均值 */
	  
	  x[cnt] = s3c_ts_regs->adcdat0 & 0x3ff;
	  y[cnt] = s3c_ts_regs->adcdat1 & 0x3ff;
	  ++cnt;
	  if(cnt == 4)
	  	{
          /*  优化措施4.软件过滤 */
		  if(s3c_filter_ts(x,y))
		  	{
		      printk("x = %d, y = %d\n",(x[1]+x[2]+x[3]+x[4])/4,(y[1]+y[2]+y[3]+y[4])/4);
		  	}
		  cnt = 0;
		  enter_wait_pen_up_mode();

		  /* 启动定时器处理长按/滑动的情况 */
		  mod_timer(&ts_timer,jiffies + HZ/100);
	  	}
	  else
	  	{
	  	enter_measure_xy_mode();
        start_adc();
	  	}
	 
  	}
  
  return IRQ_HANDLED;
}
static int s3c_ts_init(void)
{
  struct clk* clk;
  /*  1.分配结构体       */
  s3c_ts_dev = input_allocate_device();
  /*  2.设置        */
  /*  2.1 能产生哪类事件         */
  set_bit(EV_KEY,s3c_ts_dev->evbit);
  set_bit(EV_ABS,s3c_ts_dev->evbit);

  /*  2.2 能产生这类事件的哪些事件 */
  set_bit(BTN_TOUCH,s3c_ts_dev->keybit);
  input_set_abs_params(s3c_ts_dev, ABS_X, 0, 0x3FF, 0, 0);
  input_set_abs_params(s3c_ts_dev, ABS_Y, 0, 0x3FF, 0, 0);
  input_set_abs_params(s3c_ts_dev, ABS_PRESSURE, 0, 1, 0, 0);

  /*  3.注册          */
  input_register_device(s3c_ts_dev);

  /*  4.硬件相关的设置         */
  /*  4.1使能时钟CLKCON[15]         */
  clk = clk_get(NULL, "adc");
  clk_enable(clk);
  
  /*  4.2设置S3C2440的ADC/TS寄存器         */
  s3c_ts_regs = ioremap(0x58000000,sizeof(struct s3c_ts_regs));

  /*  
   *bit[14]  : A/D converter prescaler enable
   *bit[13:6]: A/D converter prescaler value
   *bit[0]   : A/D conversion starts by enable.先设为0
  */
  s3c_ts_regs->adccon  = (1<<14) | (49<<6) | (0<0);

  request_irq (IRQ_TC,pen_down_up_irq,IRQF_SAMPLE_RANDOM,"touch_pen",NULL);
  
  request_irq (IRQ_ADC,adc_irq,IRQF_SAMPLE_RANDOM,"adc",NULL);

  /*  优化措施1.
   *  设置ADCDLY为最大值。使得电压稳定后再发出IRQ_TC中断
  */
  s3c_ts_regs->adcdly  = 0xffff;

  /* 
   * 优化措施5.使用定时器处理长按，滑动的情况
  */
  init_timer(&ts_timer);
  ts_timer.function = s3c_ts_timer_functions;
  add_timer(&ts_timer);
  
  enter_wait_pen_down_mode();
  return 0;
}

static void s3c_ts_exit(void)
{
  free_irq(IRQ_TC,NULL);  
  free_irq(IRQ_ADC,NULL);
  iounmap(s3c_ts_regs);
  input_unregister_device(s3c_ts_dev);
  input_free_device(s3c_ts_dev);
  del_timer(&ts_timer);
}

module_init(s3c_ts_init);
module_exit(s3c_ts_exit);

MODULE_LICENSE("GPL");

