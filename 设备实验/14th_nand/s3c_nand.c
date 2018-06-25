
//  可参考..\nand/s3c2410.c    at91_nand.c


#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <asm/arch/regs-nand.h>
#include <asm/arch/nand.h>

struct s3c_nand_regs {
  unsigned long nfconf  ;
  unsigned long nfcont  ;
  unsigned long nfcmd   ;
  unsigned long nfaddr  ;
  unsigned long nfeccd0 ;
  unsigned long nfeccd1 ;
  unsigned long nfdata  ;
  unsigned long nfeccd  ;
  unsigned long nfstat  ;
  unsigned long nfestat0;
  unsigned long nfestat1;
  unsigned long nfmecc0 ;
  unsigned long nfmecc1 ;
  unsigned long nfsecc  ;
  unsigned long nfsblk  ;
  unsigned long nfeblk  ;
};

static struct nand_chip       *s3c_nand;
static struct mtd_info        *s3c_mtd;
static struct s3c_nand_regs   *s3c_nand_regs;


static struct mtd_partition s3c_nand_parts[] = {       // 添加四个分区
	[0] = {
        .name   = "bootloader",
        .size   = 0x00040000,
		.offset	= 0,
	},
	[1] = {
        .name   = "params",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00020000,
	},
	[2] = {
        .name   = "kernel",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00200000,
	},
	[3] = {
        .name   = "root",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	}
};

static int s3c2440_dev_ready(struct mtd_info *mtd)
{
   return (s3c_nand_regs->nfstat & (1<<0));
}
static void s3c2440_nand_cmd_ctrl(struct mtd_info *mtd, int data, unsigned int ctrl)
{
  if(ctrl & NAND_CLE)
  	{
  	   /* 发命令：NFCMMD = dat */
	   s3c_nand_regs->nfcmd = data ;
  	}
  else
  	{
  	  /* 发地址：NFADDR = dat */
	  s3c_nand_regs->nfaddr = data;
  	}
}
static void s3c2440_select_chip(struct mtd_info *mtd, int chipnr)
{
    if(chipnr == -1)
    	{
    	  /* 取消选中：NFCONT[1]设为0 */
		  s3c_nand_regs->nfcont |= (1<<1);
    	}
	else
		{
		  /* 选中：NFCONT[1]设为1 */
		  s3c_nand_regs->nfcont &=  ~(1<<1);
		}
}
static int s3c_nand_init(void)
{
  struct clk *clk;
 /*  1. 分配一个nand_chip结构体        */
  s3c_nand = kzalloc(sizeof(struct nand_chip), GFP_KERNEL);

  s3c_nand_regs = ioremap(0x4E000000,sizeof(struct s3c_nand_regs));
  
  /*  2. 设置 nand_chip */    //nand_chip 是给nand_scan使用的 可以先看scan如何使用 再来设置
                            //选中，发命令，发地址，读数据，判断状态
                            
  
  s3c_nand->select_chip = s3c2440_select_chip;
  s3c_nand->cmd_ctrl    = s3c2440_nand_cmd_ctrl;
  s3c_nand->IO_ADDR_R   = &s3c_nand_regs->nfdata;
  s3c_nand->IO_ADDR_W   = &s3c_nand_regs->nfdata;
  s3c_nand->dev_ready   = s3c2440_dev_ready;
  s3c_nand->ecc.mode    = NAND_ECC_SOFT;                        // enable ECC
  /*  3. 硬件相关的设置 */
  /* 使能NAND FLASH 控制器的时钟 */
  clk = clk_get(NULL,"nand");
  clk_enable(clk);                       //CLKCON[4]
  
  /*
   * HCLK  = 100MHZ
   * TACLS :发出 CLE/ALE 都多长时间发出nWE信号，从NAND手册可知 CLE/ALE与nWE可同时发出，所以TACLS = 0
   * TWRPH0 :nWE的脉冲长度，HCLK*(TWRPH0+1),从NAND手册可知它要>=12ns,所以TWRPH0>=1;
   * TWRPH1 :nWE变为高电平后多长时间CLE/ALE才能变为低电平，从NAND手册 可知它要>=5ns,所以TWRPH1>=0;
  */
#define  TACLS   0
#define  TWRPH0  1
#define  TWRPH1  0
  s3c_nand_regs->nfconf = (TACLS <<12) |(TWRPH0 << 8) | (TWRPH1 << 4);
  
  /*
   * BIT1-设为1，取消片选
   * BIT0-设为1，使能NAND FLASH 控制器
  */
  s3c_nand_regs->nfcont = (1<<1) | (1<<0);
  
  /*  4. 使用 nand_scan */
  
  s3c_mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
  s3c_mtd->owner = THIS_MODULE;
  s3c_mtd->priv = s3c_nand;
  

  
  nand_scan(s3c_mtd,1);   //用来识别NAND FLASH，构造mtd_info(里面有读写，擦除函数)
  
  /*  5. add_mtd_partitions */            //添加分区
  add_mtd_partitions(s3c_mtd,s3c_nand_parts,4);

  //add_mtd_device(s3c_mtd);
  
  return 0;
}

static void s3c_nand_exit(void)
{
  del_mtd_partitions(s3c_mtd);  
  kfree(s3c_mtd);
  kfree(s3c_nand);
  iounmap(s3c_nand_regs);

}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);

MODULE_LICENSE("GPL");




