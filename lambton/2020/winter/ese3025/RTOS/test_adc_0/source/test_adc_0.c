/*
 *
 * Takis's ADC tester project
 *
 * "thanks for the starter code, NXP!"
 *
 *
 */
#include "board.h"

typedef unsigned short int bool_t;

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define _ADC_CHANNLE		ADC_CH0
#define _LPC_ADC_ID 		LPC_ADC
#define _GPDMA_CONN_ADC 	GPDMA_CONN_ADC
#define _LPC_ADC_ID 		LPC_ADC
#define _LPC_ADC_IRQ 		ADC_IRQn

static ADC_CLOCK_SETUP_T 	ADCSetup;
static volatile uint8_t 	Burst_Mode_Flag = 1, Interrupt_Continue_Flag;
static volatile uint8_t 	ADC_Interrupt_Done_Flag, channelTC, dmaChannelNum;

volatile bool 				this_shit_is_for_real=true;
static const unsigned int 	dmaTransferSize=128; /* note that dmaTransferSize/_bitRate =  number of seconds for a burst */

uint32_t DMAbuffer;
uint32_t *dmaBuffer=(uint32_t *) malloc(dmaTransferSize*sizeof(uint32_t));

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* DMA routine for ADC */
static void retrieveADCSamples(void)
{
	uint16_t dataADC;

	/* Get  adc value until get 'x' character */
	while (this_shit_is_for_real)
	{
		channelTC = 0;
		Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNum,
						  _GPDMA_CONN_ADC,
						  (uint32_t) dmaBuffer,
						  GPDMA_TRANSFERTYPE_P2M_CONTROLLER_DMA,
						  dmaTransferSize);

		/* Waiting for ADC buffer to fill  */
		while (channelTC == 0) {}

		/* Get the ADC value from Data register*/
		dataADC = ADC_DR_RESULT(DMAbuffer);
		delay(1000);
	}
	/* Disable interrupts, release DMA channel */
	Chip_GPDMA_Stop(LPC_GPDMA, dmaChannelNum);
	NVIC_DisableIRQ(DMA_IRQn);
	/* Disable burst mode if any */
	if (Burst_Mode_Flag) {
		Chip_ADC_SetBurstCmd(_LPC_ADC_ID, DISABLE);
	}
}




/*****************************************************************************
 * Public functions
 ****************************************************************************/

/**
 * @brief	DMA interrupt handler sub-routine
 * @return	Nothing
 */
void DMA_IRQHandler(void)
{
	if (Chip_GPDMA_Interrupt(LPC_GPDMA, dmaChannelNum) == SUCCESS) {
		channelTC++;
	}
	else {
		/* Process error here */
	}
}

/**
 * @brief	Main routine for ADC example
 * @return	Nothing
 */
int main(void)
{
	bool end_Flag = false;
	uint32_t _bitRate = ADC_MAX_SAMPLE_RATE/4; /* should be around 50-kHz sampling */

	SystemCoreClockUpdate();
	Board_Init();

	/**** ADC INITIALIZATION ****/
		/* Basic ADC Init */
		Chip_ADC_Init(_LPC_ADC_ID, &ADCSetup); /* default is 200-kHz @ 12-bits */
		Chip_ADC_EnableChannel(_LPC_ADC_ID, _ADC_CHANNLE, ENABLE);
		ADCSetup.burstMode = true; /* we are processing 128-sample chunks */
		Chip_ADC_SetSampleRate(_LPC_ADC_ID, &ADCSetup, _bitRate);

		/* other ADC initializations */
		Chip_GPDMA_Init(LPC_GPDMA); /* Initialize GPDMA controller */
		/* Setting GPDMA interrupt */
		NVIC_DisableIRQ(DMA_IRQn);
		NVIC_SetPriority(DMA_IRQn, ((0x01 << 3) | 0x01));
		NVIC_EnableIRQ(DMA_IRQn);
		/* Setting ADC interrupt, ADC Interrupt must be disabled in DMA mode */
		NVIC_DisableIRQ(_LPC_ADC_IRQ);

		Chip_ADC_Int_SetChannelCmd(_LPC_ADC_ID, _ADC_CHANNLE, ENABLE);
		/* Get the free channel for DMA transfer */
		dmaChannelNum = Chip_GPDMA_GetFreeChannel(LPC_GPDMA, _GPDMA_CONN_ADC);

	/**** DAC INITIALIZATION ****/
		Chip_DAC_Init(LPC_DAC); /* Setup DAC pins for board and common CHIP code */
		/* Setup DAC timeout for polled and DMA modes to 0x3FF */
		Chip_Clock_SetPCLKDiv(SYSCTL_PCLK_DAC, SYSCTL_CLKDIV_1);
		Chip_DAC_SetDMATimeOut(LPC_DAC, DAC_TIMEOUT);
		/* Compute and show estimated DAC request time */
		dacClk = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_DAC);


		DEBUGOUT("DAC base clock rate = %dHz, DAC request rate = %dHz\r\n",
			dacClk, (dacClk / DAC_TIMEOUT));

		/* Enable count and DMA support */
		Chip_DAC_ConfigDAConverterControl(LPC_DAC, DAC_CNT_ENA | DAC_DMA_ENA);

	/* future coding: */
	retrieveADCSamples();
	dspSamples();
	sendDACSamples();

	int x;
	while (1)
	{
		++x;
	}
	return 0;
}

/**
 * @}
 */
/*
 * @brief ADC example
 * This example show how to  the ADC in 3 mode : Polling, Interrupt and DMA
 *
 * @note
 * Copyright(C) NXP Semiconductors, 2014
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

