#include "stm32f4xx_hal.h"
#include "bsp_adc.h"
#include <board.h>


ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;


static void DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}



void adc_init(void)
{
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCKPRESCALER_PCLK_DIV4;        /* 4分频，ADCCLK = PCLK2/4 = 84/4 = 21Mhz */
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;                      /* 12位模式 */
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;                      /* 右对齐 */
    hadc1.Init.ScanConvMode = DISABLE;                               /* 非扫描模式 */
    hadc1.Init.ContinuousConvMode = DISABLE;                         /* 关闭连续转换 */
    hadc1.Init.NbrOfConversion = 1;                                  /* 1个转换在规则序列中 也就是只转换规则序列1 */
    hadc1.Init.DiscontinuousConvMode = DISABLE;                      /* 禁止不连续采样模式 */
    hadc1.Init.NbrOfDiscConversion = 0;                              /* 不连续采样通道数为0 */
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;                /* 软件触发 */
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE; /* 使用软件触发 */
    hadc1.Init.DMAContinuousRequests = DISABLE;                      /* 关闭DMA请求 */
    HAL_ADC_Init(&hadc1);    
	
}