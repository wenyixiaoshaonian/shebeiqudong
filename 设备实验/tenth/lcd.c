#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

#include <asm/mach/map.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/fb.h>


static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green, unsigned int blue, unsigned int transp, struct fb_info *info);
			     

struct lcd_regs {
	unsigned long	lcdcon1;
	unsigned long	lcdcon2;
	unsigned long	lcdcon3;
	unsigned long	lcdcon4;
	unsigned long	lcdcon5;
    unsigned long	lcdsaddr1;
    unsigned long	lcdsaddr2;
    unsigned long	lcdsaddr3;
    unsigned long	redlut;
    unsigned long	greenlut;
    unsigned long	bluelut;
    unsigned long	reserved[9];
    unsigned long	dithmode;
    unsigned long	tpal;
    unsigned long	lcdintpnd;
    unsigned long	lcdsrcpnd;
    unsigned long	lcdintmsk;
    unsigned long	lpcsel;
};


static struct fb_ops s3c_lcdfb_ops =
{
	.owner			= THIS_MODULE,
	.fb_setcolreg		= s3c_lcdfb_setcolreg,
	.fb_fillrect		= cfb_fillrect,   // 填充一个矩形
	.fb_copyarea		= cfb_copyarea,   // 拷贝一个区域
	.fb_imageblit		= cfb_imageblit,
};
	

static struct fb_info *s3c_lcd;
static volatile unsigned long *gpbcon;
static volatile unsigned long *gpbdat;
static volatile unsigned long *gpccon;
static volatile unsigned long *gpdcon;
static volatile unsigned long *gpgcon;

static volatile struct lcd_regs* lcd_regs;

static u32 pseudo_paltte[16];


/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info)
{
   unsigned int val;

	if(regno > 16)
		  return 1;

	/*  用red green blue 三原色构造出val        */
	val  = chan_to_field(red,   &info->var.red);
	val |= chan_to_field(green, &info->var.green);
	val |= chan_to_field(blue,  &info->var.blue);

//	((u32 *)(info->pseudo_paltte))[regno] = val ;
	pseudo_paltte[regno] = val;
	return 0;
}

static int lcd_init(void)
{ 
 /*  1.分配一个fb_into结构体       */
  s3c_lcd = framebuffer_alloc(0,NULL);
 /*  2.设置    */
 /*  2.1 设置固定的参数  */
  strcpy(s3c_lcd->fix.id,"mylcd");
  s3c_lcd->fix.smem_len = 480*272*16/8;
  s3c_lcd->fix.type   = FB_TYPE_PACKED_PIXELS;
  s3c_lcd->fix.visual = FB_VISUAL_TRUECOLOR;
  s3c_lcd->fix.line_length = 480*2;
  
 /*  2.2 设置可变的参数  */
  s3c_lcd->var.xres           = 480;
  s3c_lcd->var.yres           = 272;
  s3c_lcd->var.xres_virtual   = 480; 
  s3c_lcd->var.yres_virtual   = 272;
  s3c_lcd->var.bits_per_pixel = 16;

  s3c_lcd->var.red.offset     = 11;
  s3c_lcd->var.red.length     = 5;

  s3c_lcd->var.green.offset   = 5;
  s3c_lcd->var.green.length   = 6;

  s3c_lcd->var.blue.offset    = 0;
  s3c_lcd->var.blue.length    = 5;

  s3c_lcd->var.activate       = FB_ACTIVATE_NOW;
  
 /*  2.3 设置操作的函数  */

  s3c_lcd->fbops              = &s3c_lcdfb_ops;
 /*  2.4 其他的设置*/
  
  s3c_lcd->pseudo_palette = pseudo_paltte;   // 调色板
 // s3c_lcd->screen_base      // 显存的虚拟地址
    s3c_lcd->screen_size      = 480*272*16/8;   
 
  /*  3.硬件相关的设置         */
  /*  3.1配置GPIO用于LCD         */
  gpbcon = ioremap(0x56000010,8);
  gpbdat = gpbcon + 1;
  gpccon = ioremap(0x56000020,4);
  gpdcon = ioremap(0x56000030,4);
  gpgcon = ioremap(0x56000060,4);

  *gpccon  = 0xaaaaaaaa;   // GPIO管脚用于VD[7:0],LCDVF[2:0],VM,VFRAME,VLINE,VCLK,LEND 
  *gpdcon  = 0xaaaaaaaa;   // GPIO管脚用于VD[23:8]

  *gpbcon &= ~3;
  *gpbcon |= 1;
  *gpbdat &= ~1;

  *gpgcon |= (3<<8);  // GPG4用作LCD电源使能
  
  /*  3.2根据LCD手册设置LCD控制器。如VCLK频率等         */
  lcd_regs = ioremap(0X4D000000,sizeof(struct lcd_regs));
  /* bit[17:8] VCLK = HCLK / [(CLKVAL+1) x 2]
       HCLK  :内核 dmesg 查看 VCLK: 芯片手册 
     100ns(10MHZ) = 100MHZ / [(CLKVAL+1) x 2] 
     CLKVAL = 4;
   * bit[6:5]: 0b11,TFT LCD
   * bit[4:1]: 0b1100,16 bpp for TFT
   * bit[0]  : 0 :Disable the video output and the LCD control signal
   */ 
   lcd_regs->lcdcon1 = (4<<8) | (3<<5) | (0x0c <<1);
  /*  垂直方向的时间参数
   * bit[31:24]: VBPD,VSYNC之后过多久发出第一行数据,LCD 手册 T0-T2-T1 = 4,VBPD = 3
   * bit[23:14]: 多少行，320 320-1 = 319
   * bit[13:6] : VFPD,发出最后一行数据后，再过多久发出VSYNC T2-T5 = 322-320 = 2,vfpd= 2-1 = 1；
   * bit[5:0]  : VSPW,VSYNC的脉冲宽度 T1 = 1,VSPW = 1-1 = 0;
  */
   lcd_regs->lcdcon2 = (1<<24) |(271<<14) | (1<<6) | (9<<0);
  /*  水平方向的时间参数
   * bit[25：19]: HBPD,HSYNC之后过多久发出第一个数据,LCD 手册 T6-T7-T8 = 17,VBPD = 16
   * bit[18:8]: 多少列，240 240-1 = 219
   * bit[7:0]  : HFPD,发出最后一个数据后，再过多久发出HSYNC T8-T11 = 251-240 = 11,vfpd= 11-1 = 10
   */
   lcd_regs->lcdcon3 = (1<<19) |(479<<8) | (1<<0) ;
  /* 水平方向的同步信号
   *bit[7:0] :HSPW,HSYNC的脉冲宽度 芯片手册 T7 = 5 HSPW = 4;
  */
   lcd_regs->lcdcon4 = 40;
  /* 信号的级性
   *bit[11] :1：5:6:5 Format
   *bit[10] :0：不需要反转
   *bit[9]  :1：HSYNC信号需反转 低电平有效
   *bit[8]  :1: VSYNC信号需反转
   *bit[7]  :0：VD信号不需要反转
   *bit[6]  :0：VDEN不需要反转
   *bit[5]  :0: 电源使能信号不需要反转
   *bit[3]  :0: PWREN输出0
   *bit[1]  :0: BSWP 字节交换 数据从最低位开始
   *bit[0]  :1: HWSWP 同上
  */
   lcd_regs->lcdcon5 = (1<<11) | (0<<10) | (1<<9) | (1<<8) | (1<<0);
   
  /*  3.3分配显存(framebuffer),并将地址等告诉LCD控制器         */
   
  s3c_lcd->screen_base = dma_alloc_writecombine(NULL,s3c_lcd->fix.smem_len,&s3c_lcd-> fix.smem_start,GFP_KERNEL);
  lcd_regs->lcdsaddr1 = (s3c_lcd->fix.smem_start >>1) & ~ (3<<30);   //存放显存的起始地址
  lcd_regs->lcdsaddr2 = ((s3c_lcd->fix.smem_start + s3c_lcd->fix.smem_len) >>1) & 0x1fffff;  // 结束地址的1-21位
  lcd_regs->lcdsaddr3 = (480*16/16);  //一行的长度（单位：两字节）
  //  s3c_lcd-> fix.smem_start = xxx;   // 显存的物理地址
  /*  启动    */
  lcd_regs->lcdcon1 |=(1<<0);  //使能LCD本身控制器
  lcd_regs->lcdcon5 |=(1<<3);  //使能LCD本身
  *gpbdat |= 1;     //输出高电平 使能背光
  /*  4.注册    */
  
  register_framebuffer(s3c_lcd);
  return 0;
}

static void lcd_exit(void)
{
	unregister_framebuffer(s3c_lcd);
	lcd_regs->lcdcon1 &= ~(1<<0);  //关闭LCD本身
    *gpbdat &= ~1;     //关背光
    dma_free_writecombine(NULL,s3c_lcd->fix.smem_len,s3c_lcd->screen_base,s3c_lcd-> fix.smem_start);
	iounmap(lcd_regs);
	iounmap(gpbcon);
    iounmap(gpccon);
    iounmap(gpdcon);
    iounmap(gpgcon);
	framebuffer_release(s3c_lcd);
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_LICENSE("GPL");

