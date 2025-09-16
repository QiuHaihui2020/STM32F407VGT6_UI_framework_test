#include "lcd.h"
//#include "stdint.h"
#include "fsmc.h"
#include "gpio.h"


				 
//LCD的画笔颜色和背景色	   
u16 POINT_COLOR=0x0000;	//画笔颜色
u16 BACK_COLOR=0xFFFF;  //背景色 

void Delay(uint32_t nCount)
{
	for(; nCount != 0; nCount--);
}

	 
//写寄存器函数
//regval:寄存器值
void LCD_WR_REG(u16 regval)
{ 
	LCD->LCD_REG=regval;//写入要写的寄存器序号	 
}
//写LCD数据
//data:要写入的值
void LCD_WR_DATA(u16 data)
{										    	   
	LCD->LCD_RAM=data;		 
}
//读LCD数据
//返回值:读到的值
u16 LCD_RD_DATA(void)
{										    	   
	return LCD->LCD_RAM;		 
}					   
//写寄存器
//LCD_Reg:寄存器地址
//LCD_RegValue:要写入的数据
void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
{	
	LCD->LCD_REG = LCD_Reg;		//写入要写的寄存器序号	 
	LCD->LCD_RAM = LCD_RegValue;//写入数据	    		 
}	   
//读寄存器
//LCD_Reg:寄存器地址
//返回值:读到的数据
u16 LCD_ReadReg(u16 LCD_Reg)
{										   
	LCD_WR_REG(LCD_Reg);		//写入要读的寄存器序号
//	delay_us(5);		  
	return LCD_RD_DATA();		//返回读到的值
}   
//开始写GRAM
void LCD_WriteRAM_Prepare(void)
{
 	LCD->LCD_REG=0x22;	  
}	 
//LCD写GRAM
//RGB_Code:颜色值
void LCD_WriteRAM(u16 RGB_Code)
{							    
	LCD->LCD_RAM = RGB_Code;//写十六位GRAM
}
		 
//LCD开启显示
/*****************************************************************************
** 函数名称:LCD_DisplayOn
** 功能描述: 开启LCD显示
** 功能描述: 关闭LCD显示
*****************************************************************************/  

void LCD_Display(u8 off_on)
{
if(off_on==1)	
LCD_WriteReg(0x07,0x0173); 			//开启显示
else 
LCD_WriteReg(0x07, 0x0);//关闭显示 	
}  
//设置光标位置
//Xpos:横坐标
//Ypos:纵坐标
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{	 
 
	
if(Horizontal_or_Vertical)
{
	//横屏显示
		LCD_WriteReg(0x20,Ypos);
		LCD_WriteReg(0x21,319-Xpos);
}
		//竖屏显示					   
else
{
	  LCD_WriteReg(0x20, Xpos);
		LCD_WriteReg(0x21, Ypos);	
}	
	


} 		 
//设置LCD的自动扫描方向
	   
void LCD_Scan_Dir(void)
{
	u16 regval=0;
	regval|=L2R_D2U; //从左到右,从上到下
	regval|=1<<12;  
	LCD_WriteReg(0X03,regval);
	}  


//画点
//x,y:坐标
//POINT_COLOR:此点的颜色
void LCD_DrawPoint(u16 x,u16 y,u16 Color)
{
	LCD_SetCursor(x,y);		//设置光标位置 
	LCD_WriteRAM_Prepare();	//开始写入GRAM
	LCD->LCD_RAM=Color; 
}
//清屏函数
//color:要清屏的填充色
void LCD_Clear(u16 color)
{
	u32 index=0;      
 LCD_SetCursor(0x00,0x0000);	//设置光标位置 
	LCD_WriteRAM_Prepare();     		//开始写入GRAM	 	  
	for(index=0;index<76800;index++)
	{
		LCD->LCD_RAM=color;	   
	}
} 


void LCD_FSMC_Init(void)
{
	MX_FSMC_Init();
}
//初始化lcd

void LCD_Init(void)
{ 										  
 	  LCD_FSMC_Init();
  
     Delay(0xfffff);	 

	
   LCD_WriteReg(0x00E5,0x78F0); 
		LCD_WriteReg(0x0001,0x0100); 
		LCD_WriteReg(0x0002,0x0700); 
		LCD_WriteReg(0x0003,0x1030); 
		LCD_WriteReg(0x0004,0x0000); 
		LCD_WriteReg(0x0008,0x0202);  
		LCD_WriteReg(0x0009,0x0000);
		LCD_WriteReg(0x000A,0x0000); 
		LCD_WriteReg(0x000C,0x0000); 
		LCD_WriteReg(0x000D,0x0000);
		LCD_WriteReg(0x000F,0x0000);
		//power on sequence VGHVGL
		LCD_WriteReg(0x0010,0x0000);   
		LCD_WriteReg(0x0011,0x0007);  
		LCD_WriteReg(0x0012,0x0000);  
		LCD_WriteReg(0x0013,0x0000); 
		LCD_WriteReg(0x0007,0x0000); 
		//vgh 
		LCD_WriteReg(0x0010,0x1690);   
		LCD_WriteReg(0x0011,0x0227);
		//delayms(100);
		//vregiout 
		LCD_WriteReg(0x0012,0x009D); //0x001b
		//delayms(100); 
		//vom amplitude
		LCD_WriteReg(0x0013,0x1900);
		//delayms(100); 
		//vom H
		LCD_WriteReg(0x0029,0x0025); 
		LCD_WriteReg(0x002B,0x000D); 
		//gamma
		LCD_WriteReg(0x0030,0x0007);
		LCD_WriteReg(0x0031,0x0303);
		LCD_WriteReg(0x0032,0x0003);// 0006
		LCD_WriteReg(0x0035,0x0206);
		LCD_WriteReg(0x0036,0x0008);
		LCD_WriteReg(0x0037,0x0406); 
		LCD_WriteReg(0x0038,0x0304);//0200
		LCD_WriteReg(0x0039,0x0007); 
		LCD_WriteReg(0x003C,0x0602);// 0504
		LCD_WriteReg(0x003D,0x0008); 
		//ram
		LCD_WriteReg(0x0050,0x0000); 
		LCD_WriteReg(0x0051,0x00EF);
		LCD_WriteReg(0x0052,0x0000); 
		LCD_WriteReg(0x0053,0x013F);  
		LCD_WriteReg(0x0060,0xA700); 
		LCD_WriteReg(0x0061,0x0001); 
		LCD_WriteReg(0x006A,0x0000); 
		//
		LCD_WriteReg(0x0080,0x0000); 
		LCD_WriteReg(0x0081,0x0000); 
		LCD_WriteReg(0x0082,0x0000); 
		LCD_WriteReg(0x0083,0x0000); 
		LCD_WriteReg(0x0084,0x0000); 
		LCD_WriteReg(0x0085,0x0000); 
		//
		LCD_WriteReg(0x0090,0x0010); 
		LCD_WriteReg(0x0092,0x0600); 
		
		LCD_WriteReg(0x0007,0x0133);
		LCD_WriteReg(0x00,0x0022);//
    LCD_Scan_Dir();	//默认扫描方向 
    LCD_LED;					//点亮背光

}  
 
 

	/****************************两点一线****************************/
//函数功能：画两点一线
//入口参数: x1,y1     直线的起点;
//			    x2,y2     直线的终点
//          color     直线的颜色
//出口参数: 无
//说明：    该函数是在LCD上画一条直线
/****************************************************************/
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2,u16 Color)
{
	int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0, 
	yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0, 
	curpixel = 0;
  
	deltax = abs(x2 - x1);        /* The difference between the x's */
	deltay = abs(y2 - y1);        /* The difference between the y's */
	x = x1;                       /* Start x off at the first pixel */
	y = y1;                       /* Start y off at the first pixel */
  
	if (x2 >= x1)                 /* The x-values are increasing */
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else                          /* The x-values are decreasing */
	{
		xinc1 = -1;
		xinc2 = -1;
	}
	if (y2 >= y1)                 /* The y-values are increasing */
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else                          /* The y-values are decreasing */
	{
		yinc1 = -1;
		yinc2 = -1;
	}
  
	if (deltax >= deltay)         /* There is at least one x-value for every y-value */
	{
		xinc1 = 0;                  /* Don't change the x when numerator >= denominator */
		yinc2 = 0;                  /* Don't change the y for every iteration */
		den = deltax;
		num = deltax / 2;
		numadd = deltay;
		numpixels = deltax;         /* There are more x-values than y-values */
	}
	else                          /* There is at least one y-value for every x-value */
	{
		xinc2 = 0;                  /* Don't change the x for every iteration */
		yinc1 = 0;                  /* Don't change the y when numerator >= denominator */
		den = deltay;
		num = deltay / 2;
		numadd = deltax;
		numpixels = deltay;         /* There are more y-values than x-values */
	}
  	
	for (curpixel = 0; curpixel <= numpixels; curpixel++)
  	{
				LCD_DrawPoint(x,y,Color);//画点 
		num += numadd;              /* Increase the numerator by the top of the fraction */
		if (num >= den)             /* Check if numerator >= denominator */
		{
			num -= den;               /* Calculate the new numerator value */
			x += xinc1;               /* Change the x as appropriate */
			y += yinc1;               /* Change the y as appropriate */
		}
		x += xinc2;                 /* Change the x as appropriate */
		y += yinc2;                 /* Change the y as appropriate */
	}
}


/*****************************************************************************
** 函数名称: LCD_DrawLine
** 功能描述: 在指定位置画一个指定大小的圆
				(x,y):中心点 	 r    :半径
*****************************************************************************/
void Draw_Circle(u16 x0,u16 y0,u8 r,u16 Color)
{
	int a=0,b=0;
	int di=0;
	a=0;b=r;	  
	di=1-r;             //判断下个点位置的标志
	while(a<=b)
	{
		LCD_DrawPoint(x0-b,y0-a,Color);             //3           
		LCD_DrawPoint(x0+b,y0-a,Color);             //0           
		LCD_DrawPoint(x0-a,y0+b,Color);             //1       
		LCD_DrawPoint(x0-b,y0-a,Color);             //7           
		LCD_DrawPoint(x0-a,y0-b,Color);             //2             
		LCD_DrawPoint(x0+b,y0+a,Color);             //4               
		LCD_DrawPoint(x0+a,y0-b,Color);             //5
		LCD_DrawPoint(x0+a,y0+b,Color);             //6 
		LCD_DrawPoint(x0-b,y0+a,Color);             
		a++; 
		if(di<0)	di+=(a<<1)+3;
		else
		{
			di+=((a-b)<<1)+5;
			b--;
		}
	}
} 
/*****************************************************************************
** 函数名称: LCD_Fill
** 功能描述: 在指定位置画一个指定大小的矩形填充

*****************************************************************************/


void LCD_Fill(uint8_t xsta,uint16_t ysta,uint8_t xend,uint16_t yend,uint16_t color)
{                    
    uint32_t n;
	//设置窗口										
	LCD_WriteReg(0x50, xsta); //水平方向GRAM起始地址
	LCD_WriteReg(0x51, xend); //水平方向GRAM结束地址
	LCD_WriteReg(0x52, ysta); //垂直方向GRAM起始地址
	LCD_WriteReg(0x53, yend); //垂直方向GRAM结束地址	
	LCD_SetCursor(xsta,ysta);//设置光标位置  
	LCD_WriteRAM_Prepare();  //开始写入GRAM	 	   	   
	n=(u32)(yend-ysta+1)*(xend-xsta+1);    
	while(n--){LCD_WR_DATA(color);}//显示所填充的颜色. 
	//恢复设置
	LCD_WriteReg(0x50, 0x0000); //水平方向GRAM起始地址
	LCD_WriteReg(0x51, 0x00EF); //水平方向GRAM结束地址
	LCD_WriteReg(0x52, 0x0000); //垂直方向GRAM起始地址
	LCD_WriteReg(0x53, 0x013F); //垂直方向GRAM结束地址	    
}

		  




	  






