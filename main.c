/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the AXI-DMA Memory Copy
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_retarget_io.h"
#include "mtb_hal.h"
#include <inttypes.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define AXIDMAC_SW_TRIG (TRIG_OUT_MUX_13_AXIDMA_TR_IN0)
#define AXIDMA_INTERRUPT_PRIORITY (7u)
#define GPIO_INTERRUPT_PRIORITY (7u)
#define DELAY_MS (1)

/* Set number of data elements to transfer 128 (32*4) */
#define BUFFER_SIZE  (32ul)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* A flag when DMA transfer is completed, then it will change to true */
static bool g_isComplete;
/* A flag when button interrupt is occurred, then it will change to true */
static bool g_isInterrupt;

/*******************************************************************************
* Private Variables/Constants
*******************************************************************************/
/* DMAC Interrupt configuration structure */
const cy_stc_sysint_t IRQ_CFG =
{
    .intrSrc = ((NvicMux4_IRQn << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) | AXIDMA_IRQ),
    .intrPriority = AXIDMA_INTERRUPT_PRIORITY
};

const cy_stc_sysint_t BTN_IRQ_CFG =
{
    .intrSrc = ((NvicMux3_IRQn << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) | CYBSP_USER_BTN_IRQ),
    .intrPriority = GPIO_INTERRUPT_PRIORITY
};

/* For the Retarget -IO (Debug UART) usage */
static cy_stc_scb_uart_context_t    UART_context;           /** UART context */
static mtb_hal_uart_t               UART_hal_obj;           /** Debug UART HAL object  */

/* Data to be transferred */
const static  uint32_t    srcBuffer[BUFFER_SIZE] =
{0x10000000,0x10000001,0x10000002,0x10000003,
 0x10000004,0x10000005,0x10000006,0x10000007,
 0x10000008,0x10000009,0x1000000A,0x1000000B,
 0x1000000C,0x1000000D,0x1000000E,0x1000000F,
 0x20000000,0x20000001,0x20000002,0x20000003,
 0x20000004,0x20000005,0x20000006,0x20000007,
 0x20000008,0x20000009,0x2000000A,0x2000000B,
 0x2000000C,0x2000000D,0x2000000E,0x2000000F,
 };
/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/* AXI-DMA Handler */
void HandleAXIDMACIntr(void);

/* GPIO Handler */
void HandleGPIOIntr(void);

/*******************************************************************************
* Function Implementations
*******************************************************************************/

/**********************************************************************************************************************
 * Function Name: HandleAXIDMACIntr*
 * Summary:
 *  AXIDMA interrupt handler
 * Parameters:
 *  None
 * Return:
 *  None
 **********************************************************************************************************************
 */
void HandleAXIDMACIntr(void)
{
    uint32_t masked;

    masked = Cy_AXIDMAC_Channel_GetInterruptStatusMasked(AXIDMA_HW, AXIDMA_CHANNEL);
    if ((CY_AXIDMAC_INTR_COMPLETION == masked))
    {
        /* Clear Complete DMA interrupt flag */
        Cy_AXIDMAC_Channel_ClearInterrupt(AXIDMA_HW, AXIDMA_CHANNEL,CY_AXIDMAC_INTR_COMPLETION);

        /* Mark the transmission as complete */
        g_isComplete = true;
    }
    else
    {
        CY_ASSERT(false);
    }
}

/**********************************************************************************************************************
 * Function Name: HandleGPIOIntr
 * Summary:
 *   GPIO interrupt handler.
 * Parameters: 
 *   none  
 **********************************************************************************************************************
 */
void HandleGPIOIntr(void)
{
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_NUM);
   
    g_isInterrupt = true;
}

/**********************************************************************************************************************
 * Function Name: main
 * Summary:
 *  This is the main function for CPU. It...
 *    1.Transmit data from memory array to memory array via M-DMA
 *    2.Using button interrupt to trigger the M-DMA transfer
 *    3.The result will be like below
 *       dstBuffer: 0x10000000,0x10000001,0x10000002,......
 *       srcBuffer: 0x10000000,0x10000001,0x10000002,......
 * Parameters:
 *  none
 * Return:
 *  int
 **********************************************************************************************************************
 */
int main(void)
{
    cy_rslt_t result;
    uint32_t dstBuffer[BUFFER_SIZE];

    /* Initialize the device and board peripherals */
    if (cybsp_init() != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    SCB_DisableICache();
    SCB_DisableDCache();

    /* Debug UART init */
    result = (cy_rslt_t)Cy_SCB_UART_Init(UART_HW, &UART_config, &UART_context);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    Cy_SCB_UART_Enable(UART_HW);

    /* Setup the HAL UART */
    result = mtb_hal_uart_setup(&UART_hal_obj, &UART_hal_config, &UART_context, NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    result = cy_retarget_io_init(&UART_hal_obj);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    printf("\x1b[2J\x1b[;H");
    printf("**************** AXI-DMA memory copy Transfer ****************\r\n");
    printf("\r\n");
    printf("- AXI-DMA- memory copy Transfer Initialize & Enable  \r\n");

    /* Configure GPIO interrupt */
    Cy_SysInt_Init(&BTN_IRQ_CFG, &HandleGPIOIntr);
    NVIC_ClearPendingIRQ((IRQn_Type)BTN_IRQ_CFG.intrSrc);
    NVIC_EnableIRQ((IRQn_Type) NvicMux3_IRQn);

    /* Disable AXI-DMA */
    Cy_AXIDMAC_Disable(AXIDMA_HW);
    Cy_AXIDMAC_Channel_DeInit(AXIDMA_HW, AXIDMA_CHANNEL);
 
    /* Set the Source and Destination address */
    AXIDMA_Descriptor_0_config.srcAddress = (void *)srcBuffer;
    AXIDMA_Descriptor_0_config.dstAddress = (void *)dstBuffer;

    /* Initialize the AXI-DMA descriptor */
    result = Cy_AXIDMAC_Descriptor_Init(&AXIDMA_Descriptor_0, &AXIDMA_Descriptor_0_config);
    if (result != CY_AXIDMAC_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize the AXI-DMA channel */
    result = CY_AXIDMAC_SUCCESS != Cy_AXIDMAC_Channel_Init(AXIDMA_HW, AXIDMA_CHANNEL, &AXIDMA_channelConfig);
    if (result != CY_AXIDMAC_SUCCESS)
    {
        CY_ASSERT(0);
    }
    Cy_AXIDMAC_Channel_SetInterruptMask(AXIDMA_HW, AXIDMA_CHANNEL, CY_AXIDMAC_INTR_COMPLETION);

    /* Enable the AXI-DMA */
    Cy_AXIDMAC_Enable(AXIDMA_HW);

    /* Interrupt Initialization */
    result = Cy_SysInt_Init(&IRQ_CFG, HandleAXIDMACIntr);
    if (result != CY_SYSINT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    /* Enable the Interrupt */
    NVIC_EnableIRQ(NvicMux4_IRQn);

    printf("- AXI-DMA- memory copy Transfer setup is completed. \r\n");
    printf("\r\n");
    printf("**************** Please Press USER_BTN1. ****************\r\n");
    printf("\r\n");

    for (;;)
    {
        /* Clear the BTN interrupt flag */
        g_isInterrupt = false;

        /* Wait for BTN interrupt */
        while (g_isInterrupt == false)
        {
            Cy_SysLib_Delay(DELAY_MS);
        }

        /* Clear destination memory */
        memset(dstBuffer, 0, sizeof(dstBuffer));

        /* Set up the channel */
        Cy_AXIDMAC_Channel_SetDescriptor(AXIDMA_HW, AXIDMA_CHANNEL, &AXIDMA_Descriptor_0);
        Cy_AXIDMAC_Channel_Enable(AXIDMA_HW, AXIDMA_CHANNEL);

        /* Clear the transfer completion flag */
        g_isComplete = false;

        /* SW Trigger start */
        result = Cy_TrigMux_SwTrigger(AXIDMAC_SW_TRIG, CY_TRIGGER_TWO_CYCLES);
        if (result != CY_AXIDMAC_SUCCESS)
        {
            CY_ASSERT(0);
        }

        /* wait for DMA completion */
        while (g_isComplete == false)
        {
            Cy_SysLib_Delay(DELAY_MS);
        }

        printf("- AXI-DMA transfer is completed. \r\n");
        printf("\r\n");

        /* Compare source and destination buffers */
        result = memcmp(srcBuffer, dstBuffer, BUFFER_SIZE);
        if (result != 0)
        {
            CY_ASSERT(0);
        }
        
        /* Print out the result of memory copy*/
        printf("**************** Source(CODE FLASH): ****************\r\n");
        for (int idx = 0UL; idx < BUFFER_SIZE; idx++)
        {
            printf("0x%" PRIX32 " ", srcBuffer[idx]);
            if (idx % 4 == 3)
            {
                printf("\r\n");
            }
        }
        printf("\r\n");

        printf("**************** Destination(SRAM): ****************\r\n");
        for (int idx = 0UL; idx < BUFFER_SIZE; idx++)
        {
            printf("0x%" PRIX32 " ", dstBuffer[idx]);
            if (idx % 4 == 3)
            {
                printf("\r\n");
            }
        }
        printf("\r\n");
        printf("Wait for next memory transfer. \r\n");
        printf("\r\n");
    }
}

/* [] END OF FILE */
