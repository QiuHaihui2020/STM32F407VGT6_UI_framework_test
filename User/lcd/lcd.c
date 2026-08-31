#include "lcd.h"
//#include "stdint.h"
#include "main.h"
#if FSMC_ENABLE
#include "fsmc.h"
#endif
#include "gpio.h"

#if LCD_ENABLE


				 
//LCD�Ļ�����ɫ�ͱ���ɫ	   
u16 POINT_COLOR=0x0000;	//������ɫ
u16 BACK_COLOR=0xFFFF;  //����ɫ 

void Delay(uint32_t nCount)
{
	for(; nCount != 0; nCount--);
}

	 
//д�Ĵ�������
//regval:�Ĵ���ֵ
void LCD_WR_REG(u16 regval)
{ 
	LCD->LCD_REG=regval;//д��Ҫд�ļĴ������	 
}
//дLCD����
//data:Ҫд���ֵ
void LCD_WR_DATA(u16 data)
{										    	   
	LCD->LCD_RAM=data;		 
}
//��LCD����
//����ֵ:������ֵ
u16 LCD_RD_DATA(void)
{										    	   
	return LCD->LCD_RAM;		 
}					   
//д�Ĵ���
//LCD_Reg:�Ĵ�����ַ
//LCD_RegValue:Ҫд�������
void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
{	
	LCD->LCD_REG = LCD_Reg;		//д��Ҫд�ļĴ������	 
	LCD->LCD_RAM = LCD_RegValue;//д������	    		 
}	   
//���Ĵ���
//LCD_Reg:�Ĵ�����ַ
//����ֵ:����������
u16 LCD_ReadReg(u16 LCD_Reg)
{										   
	LCD_WR_REG(LCD_Reg);		//д��Ҫ���ļĴ������
//	delay_us(5);		  
	return LCD_RD_DATA();		//���ض�����ֵ
}   
//��ʼдGRAM
void LCD_WriteRAM_Prepare(void)
{
 	LCD->LCD_REG=0x22;	  
}	 
//LCDдGRAM
//RGB_Code:��ɫֵ
void LCD_WriteRAM(u16 RGB_Code)
{							    
	LCD->LCD_RAM = RGB_Code;//дʮ��λGRAM
}
		 
//LCD������ʾ
/*****************************************************************************
** ��������:LCD_DisplayOn
** ��������: ����LCD��ʾ
** ��������: �ر�LCD��ʾ
*****************************************************************************/  

void LCD_Display(u8 off_on)
{
if(off_on==1)	
LCD_WriteReg(0x07,0x0173); 			//������ʾ
else 
LCD_WriteReg(0x07, 0x0);//�ر���ʾ 	
}  
//���ù��λ��
//Xpos:������
//Ypos:������
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{	 
 
	
if(Horizontal_or_Vertical)
{
	//������ʾ
		LCD_WriteReg(0x20,Ypos);
		LCD_WriteReg(0x21,319-Xpos);
}
		//������ʾ					   
else
{
	  LCD_WriteReg(0x20, Xpos);
		LCD_WriteReg(0x21, Ypos);	
}	
	


} 		 
//����LCD���Զ�ɨ�跽��
	   
void LCD_Scan_Dir(void)
{
	u16 regval=0;
	regval|=L2R_D2U; //������,���ϵ���
	regval|=1<<12;  
	LCD_WriteReg(0X03,regval);
	}  


//����
//x,y:����
//POINT_COLOR:�˵����ɫ
void LCD_DrawPoint(u16 x,u16 y,u16 Color)
{
	LCD_SetCursor(x,y);		//���ù��λ�� 
	LCD_WriteRAM_Prepare();	//��ʼд��GRAM
	LCD->LCD_RAM=Color; 
}
//��������
//color:Ҫ���������ɫ
void LCD_Clear(u16 color)
{
	u32 index=0;      
 LCD_SetCursor(0x00,0x0000);	//���ù��λ�� 
	LCD_WriteRAM_Prepare();     		//��ʼд��GRAM	 	  
	for(index=0;index<76800;index++)
	{
		LCD->LCD_RAM=color;	   
	}
} 


void LCD_FSMC_Init(void)
{
#if FSMC_ENABLE
	MX_FSMC_Init();
#endif
}
//��ʼ��lcd

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
    LCD_Scan_Dir();	//Ĭ��ɨ�跽�� 
    LCD_LED;					//��������

}  
 
 

	/****************************����һ��****************************/
//�������ܣ�������һ��
//��ڲ���: x1,y1     ֱ�ߵ����;
//			    x2,y2     ֱ�ߵ��յ�
//          color     ֱ�ߵ���ɫ
//���ڲ���: ��
//˵����    �ú�������LCD�ϻ�һ��ֱ��
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
				LCD_DrawPoint(x,y,Color);//���� 
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
** ��������: LCD_DrawLine
** ��������: ��ָ��λ�û�һ��ָ����С��Բ
				(x,y):���ĵ� 	 r    :�뾶
*****************************************************************************/
void Draw_Circle(u16 x0,u16 y0,u8 r,u16 Color)
{
	int a=0,b=0;
	int di=0;
	a=0;b=r;	  
	di=1-r;             //�ж��¸���λ�õı�־
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
** ��������: LCD_Fill
** ��������: ��ָ��λ�û�һ��ָ����С�ľ������

*****************************************************************************/


void LCD_Fill(uint8_t xsta,uint16_t ysta,uint8_t xend,uint16_t yend,uint16_t color)
{                    
    uint32_t n;
	//���ô���										
	LCD_WriteReg(0x50, xsta); //ˮƽ����GRAM��ʼ��ַ
	LCD_WriteReg(0x51, xend); //ˮƽ����GRAM������ַ
	LCD_WriteReg(0x52, ysta); //��ֱ����GRAM��ʼ��ַ
	LCD_WriteReg(0x53, yend); //��ֱ����GRAM������ַ	
	LCD_SetCursor(xsta,ysta);//���ù��λ��  
	LCD_WriteRAM_Prepare();  //��ʼд��GRAM	 	   	   
	n=(u32)(yend-ysta+1)*(xend-xsta+1);    
	while(n--){LCD_WR_DATA(color);}//��ʾ��������ɫ. 
	//�ָ�����
	LCD_WriteReg(0x50, 0x0000); //ˮƽ����GRAM��ʼ��ַ
	LCD_WriteReg(0x51, 0x00EF); //ˮƽ����GRAM������ַ
	LCD_WriteReg(0x52, 0x0000); //��ֱ����GRAM��ʼ��ַ
	LCD_WriteReg(0x53, 0x013F); //��ֱ����GRAM������ַ	    
}

		  




	

#endif /* LCD_ENABLE */
