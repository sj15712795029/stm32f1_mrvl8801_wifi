/******************************************************************************
  * @file    hw_sdio.h
  * @author  Yu-ZhongJun
  * @version V0.0.1
  * @date    2018-7-31
  * @brief   sdio的源文件
******************************************************************************/
#include "hw_sdio.h"
#include "stm32f4xx_ll_dma.h"

sdio_func_t hw_sdio_func[SDIO_FUNC_MAX];
sdio_core_t hw_sdio_core;
psdio_core_t phw_sdio_core = &hw_sdio_core;

/* 函数声明区 */
static uint8_t hw_sdio_cmd3(uint32_t para,uint32_t *resp);
static uint8_t hw_sdio_cmd5(uint32_t para,uint32_t *resp,uint32_t retry_max);
static uint8_t hw_sdio_cmd7(uint32_t para,uint32_t *resp);
static uint8_t hw_sdio_cmd53_read(uint8_t func_num,uint32_t address, uint8_t incr_addr, uint8_t *buf,uint32_t size,uint16_t cur_blk_size);
static uint8_t hw_sdio_cmd53_write(uint8_t func_num,uint32_t address, uint8_t incr_addr, uint8_t *buf,uint32_t size,uint16_t cur_blk_size);
static uint8_t hw_sdio_core_init(void);
static uint8_t hw_sdio_parse_r4(uint32_t r4);
static uint8_t hw_sdio_parse_r6(uint32_t r6,uint32_t *rca);
static uint8_t hw_sdio_cis_read_parse(uint8_t func_num,uint32_t cis_ptr);
static uint8_t hw_sdio_set_dblocksize(uint32_t *struct_dblocksize,uint32_t block_size);
static uint8_t hw_sdio_check_err(void);
uint8_t hw_sdio_enable_func(uint8_t func_num);
uint8_t hw_sdio_enable_mgr_int(void);
uint8_t hw_sdio_enable_func_int(uint8_t func_num);
uint8_t hw_sdio_get_cis_ptr(uint8_t func_num,uint32_t *ptr_address);
uint8_t hw_sdio_set_bus_width(uint8_t bus_width);
uint8_t hw_sdio_get_cccr_version(uint8_t *cccr_version);
uint8_t hw_sdio_get_sdio_version(uint8_t *sdio_version);
uint8_t hw_sdio_cmd52(uint8_t write,uint8_t func_num,uint32_t address,uint8_t para,uint8_t *resp);

/******************************************************************************
 *	函数名:	hw_sdio_init
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		SDIO init
 				pin脚分配
 				PC8->SDIO D0 PC9->SDIO D1 PC10->SDIO D2 PC11->SDIO D3
 				PC12->SDIO CLK
 				PD2->SDIO CMD
******************************************************************************/
uint8_t hw_sdio_init()
{
    uint32_t rca;
    uint8_t func_index;
    uint32_t cmd3_para;
    uint32_t cmd3_resp;
    uint32_t cmd5_para;
    uint32_t cmd5_resp;
    uint32_t cmd7_para;
    uint32_t cmd7_resp;
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	SDIO_InitTypeDef SDIO_InitStruct = {0};

    HW_ENTER();
    hw_chip_reset();
    hw_sdio_core_init();

	__HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**SDIO GPIO Configuration
    PC12     ------> SDIO_CK
    PC8     ------> SDIO_D0
    PC9     ------> SDIO_D1
    PC10     ------> SDIO_D2
    PC11     ------> SDIO_D3
    PD2     ------> SDIO_CMD
    */

    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11 |GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    SDIO_InitStruct.BusWide =SDIO_BUS_WIDE_1B;
    SDIO_InitStruct.ClockDiv = SDIO_CLK_400KHZ;
    SDIO_Init(SDIO,SDIO_InitStruct);

	SDIO_PowerState_ON(SDIO);
    __SDIO_ENABLE(SDIO);
    __SDIO_OPERATION_ENABLE(SDIO);
 
#if 0
	/* 设置SDIO中断，抢占优先级为3，相应优先级为4 */
    NVIC_InitStructure.NVIC_IRQChannel = SDIO_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = WIFI_PREE_PRIO;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = WIFI_SUB_PRIO;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    SDIO_ITConfig(SDIO_IT_CCRCFAIL |SDIO_IT_DCRCFAIL | SDIO_IT_CTIMEOUT |\
                  SDIO_IT_DTIMEOUT | SDIO_IT_TXUNDERR |SDIO_IT_RXOVERR | \
                  SDIO_IT_STBITERR|SDIO_IT_SDIOIT, ENABLE);
#endif

    /* 开启SDIO的DMA，此位置尤其重要，如果在CMD53中每次开启，那么会DMA lock住 */
    __SDIO_DMA_ENABLE(SDIO);

    /* 发送cmd5 */
    cmd5_para = 0;
    if(hw_sdio_cmd5(cmd5_para,&cmd5_resp,SDIO_RETRY_MAX))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_CMD5_FAIL;
    }

    /* ocr 3.2V~3.4V*/
    cmd5_para = 0x300000;
    if(hw_sdio_cmd5(cmd5_para,&cmd5_resp,SDIO_RETRY_MAX))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_CMD5_FAIL;
    }

    /* 解析R4 */
    hw_sdio_parse_r4(cmd5_resp);

    /* 发送cmd3获取地址 */
    cmd3_para = 0;
    if(hw_sdio_cmd3(cmd3_para,&cmd3_resp))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_CMD3_FAIL;
    }

    hw_sdio_parse_r6(cmd3_resp,&rca);

    /* 发送cmd7选地址 */
    cmd7_para = rca << 16;
    if(hw_sdio_cmd7(cmd7_para,&cmd7_resp))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_CMD7_FAIL;
    }

    /* 获取CCCR版本和SDIO版本 */
    hw_sdio_get_cccr_version(&phw_sdio_core->cccr_version);
    hw_sdio_get_sdio_version(&phw_sdio_core->sdio_version);

    /* 切换到4 bus width,切换24M clk */
    hw_sdio_set_bus_width(SDIO_BUS_WIDTH_4);
	SDIO_InitStruct.BusWide =SDIO_BUS_WIDE_4B;
    SDIO_InitStruct.ClockDiv = SDIO_CLK_24MHZ;
    SDIO_Init(SDIO,SDIO_InitStruct);

    /* 读取每个func的CIS指针并且解析 */
    for(func_index = 0; func_index < phw_sdio_core-> func_total_num; func_index++)
    {
        uint32_t cis_ptr;
        hw_sdio_get_cis_ptr(func_index,&cis_ptr);
        hw_sdio_cis_read_parse(func_index,cis_ptr);
    }

    /* enable Func */
    for(func_index = SDIO_FUNC_1; func_index < phw_sdio_core-> func_total_num; func_index++)
    {
        hw_sdio_enable_func(func_index);
    }

    /* 使能中断 */
    hw_sdio_enable_mgr_int();
    for(func_index = SDIO_FUNC_1; func_index < phw_sdio_core-> func_total_num; func_index++)
    {
        hw_sdio_enable_func_int(func_index);
    }

    /* 设置block size */
    for(func_index = SDIO_FUNC_1; func_index < phw_sdio_core-> func_total_num; func_index++)
    {
        hw_sdio_set_blk_size(func_index,SDIO_DEFAULT_BLK_SIZE);
    }
    HW_LEAVE();

    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_get_cccr_version
 * 参数:  		cccr_version(OUT)		-->CCCR版本
 * 返回值: 	返回执行结果
 * 描述:		通过读取CCCR寄存器，或许到CCCR寄存器版本
 				CCCR版本的寄存器地址为0x0
 				格式为:
 				|-7-|-6-|-5-|-4-|-3-|-2-|-1-|-0-|
 				|--SDIO version-| CCCR version  |
 				Value CCCR/FBR Format Version
				00h CCCR/FBR defined in SDIO Version 1.00
				01h CCCR/FBR defined in SDIO Version 1.10
				02h CCCR/FBR defined in SDIO Version 2.00
				03h CCCR/FBR defined in SDIO Version 3.00
				04h-0Fh Reserved for Future Use
******************************************************************************/
uint8_t hw_sdio_get_cccr_version(uint8_t *cccr_version)
{
    uint8_t version;
    HW_ENTER();
    if(!cccr_version)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CMD52读出CCCR0的值 */
    if(hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_SDIO_VERSION,0,&version))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_GET_VER_FAIL;
    }

    *cccr_version = phw_sdio_core->cccr_version = version & 0xf;

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_get_sdio_version
 * 参数:  		sdio_version(OUT)		-->SDIO版本
 * 返回值: 	返回执行结果
 * 描述:		通过读取CCCR寄存器，或许到SDIO版本
 				SDIO版本的寄存器地址为0x0
 				格式为:
 				|-7-|-6-|-5-|-4-|-3-|-2-|-1-|-0-|
 				|--SDIO version-| CCCR version  |
 				Value SDIO Specification
				00h SDIO Specification Version 1.00
				01h SDIO Specification Version 1.10
				02h SDIO Specification Version 1.20 (unreleased)
				03h SDIO Specification Version 2.00
				04h SDIO Specification Version 3.00
				05h-0Fh Reserved for Future Use
******************************************************************************/
uint8_t hw_sdio_get_sdio_version(uint8_t *sdio_version)
{
    uint8_t version;
    HW_ENTER();
    if(!sdio_version)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CMD52读出CCCR0的值 */
    if(hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_SDIO_VERSION,0,&version))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_GET_VER_FAIL;
    }

    *sdio_version = phw_sdio_core->sdio_version = (version>>4) & 0xf;
    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_enable_func
 * 参数:  		func_num(IN)		-->func num
 * 返回值: 	返回执行结果
 * 描述:		根据func num使能特定的func
******************************************************************************/
uint8_t hw_sdio_enable_func(uint8_t func_num)
{
    uint8_t enable;
    uint8_t ready = 0;
    HW_ENTER();
    if(func_num > SDIO_FUNC_MAX)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CMD52读出CCCR2的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_IO_ENABLE,0,&enable))
    {
        /* 或操作，不破坏原有的值 */
        enable |= (1<<func_num);
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_FUNC_FAIL;
    }

    /* 重新写会CCCR2 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_IO_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_FUNC_FAIL;
    }

    /* 等待特定的func ready */
    while(!(ready & (1<<func_num)))
    {
        hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_IO_READY,0,&ready);
    }

    /* 更新func的状态 */
    if((phw_sdio_core->func)[func_num])
    {
        (phw_sdio_core->func)[func_num]->func_status = FUNC_ENABLE;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_disable_func
 * 参数:  		func_num(IN)		-->func num
 * 返回值: 	返回执行结果
 * 描述:		根据func num失能特定的func
******************************************************************************/
uint8_t hw_sdio_disable_func(uint8_t func_num)
{
    uint8_t enable;
    HW_ENTER();
    if(func_num > SDIO_FUNC_MAX)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }
    /* CMD52读出CCCR2的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_IO_ENABLE,0,&enable))
    {
        /* 与反操作，不破坏原有的值 */
        enable &= ~(1<<func_num);

    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_FUNC_FAIL;
    }

    /* 重新写会CCCR2 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_IO_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_FUNC_FAIL;
    }

    /* 更新func的状态 */
    if((phw_sdio_core->func)[func_num])
    {
        (phw_sdio_core->func)[func_num]->func_status = FUNC_DISABLE;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}


/******************************************************************************
 *	函数名:	hw_sdio_enable_func_int
 * 参数:  		func_num(IN)		-->func num
 * 返回值: 	返回执行结果
 * 描述:		根据func num使能特定的func的中断
******************************************************************************/
uint8_t hw_sdio_enable_func_int(uint8_t func_num)
{
    uint8_t enable;
    HW_ENTER();
    if(func_num > SDIO_FUNC_MAX)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CMD52读出CCCR4的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,0,&enable))
    {
        /* 或操作，不破坏原有的值 */
        enable |= (1<<func_num);
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_FUNC_INT_FAIL;
    }

    /* 重新写会CCCR4 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_FUNC_INT_FAIL;
    }

    /* 更新func的中断状态 */
    if((phw_sdio_core->func)[func_num])
    {
        (phw_sdio_core->func)[func_num]->func_int_status = FUNC_INT_ENABLE;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_disable_func_int
 * 参数:  		func_num(IN)		-->func num
 * 返回值: 	返回执行结果
 * 描述:		根据func num失能特定的func的中断
******************************************************************************/
uint8_t hw_sdio_disable_func_int(uint8_t func_num)
{
    uint8_t enable;
    HW_ENTER();
    if(func_num > SDIO_FUNC_MAX)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CMD52读出CCCR4的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,0,&enable))
    {
        /* 与反操作，不破坏原有的值 */
        enable &= ~(1<<func_num);
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_FUNC_INT_FAIL;
    }

    /* 重新写会CCCR4 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_FUNC_INT_FAIL;
    }

    /* 更新func的中断状态 */
    if((phw_sdio_core->func)[func_num])
    {
        (phw_sdio_core->func)[func_num]->func_int_status = FUNC_INT_DISABLE;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_enable_mgr_int
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		根据func num使能CCCR int总开关
******************************************************************************/
uint8_t hw_sdio_enable_mgr_int(void)
{
    uint8_t enable;
    HW_ENTER();

    /* 读回CCCR4的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,0,&enable))
    {
        /* 中断总开关在CCCR4的bit 0,所以或上1 */
        enable |= 0x1;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_MGR_INT_FAIL;
    }

    /* 重新写回去 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_ENABLE_MGR_INT_FAIL;
    }

    /* 更新INT manager的状态 */
    phw_sdio_core->sdio_int_mgr = FUNC_INT_ENABLE;

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_disable_mgr_int
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		根据func num失能CCCR int总开关
******************************************************************************/
uint8_t hw_sdio_disable_mgr_int()
{
    uint8_t enable;
    HW_ENTER();

    /* 读回CCCR4的值 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,0,&enable))
    {
        /* 中断总开关在CCCR4的bit 0,所以与反上1 */
        enable &= ~0x1;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_MGR_INT_FAIL;
    }

    /* 重新写回去 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_INT_ENABLE,enable,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_DISABLE_MGR_INT_FAIL;
    }

    /* 更新INT manager的状态 */
    phw_sdio_core->sdio_int_mgr = FUNC_INT_DISABLE;

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_get_int_pending
 * 参数:  		int_pending(OUT)		-->中断pending的值
 * 返回值: 	返回执行结果
 * 描述:		获取中断pending的值
******************************************************************************/
uint8_t hw_sdio_get_int_pending(uint8_t *int_pending)
{
    HW_ENTER();
    if(!int_pending)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* 读回中断pending的值 */
    if(hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_INT_PENDING,0,int_pending))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_GET_INT_PEND_FAIL;
    }
    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_set_func_abort
 * 参数:  		func_num(IN)		-->func编号
 * 返回值: 	返回执行结果
 * 描述:		设置某一个func abort
******************************************************************************/
uint8_t hw_sdio_set_func_abort(uint8_t func_num)
{
    uint8_t abort;
    HW_ENTER();

    if(func_num > SDIO_FUNC_MAX)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* 读会abort的值，或上func num,为了不破坏数据 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_IO_ABORT,0,&abort))
    {
        abort |= func_num;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_SET_ABORT_FAIL;
    }

    /* 重新写回去 */
    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_IO_ABORT,abort,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_SET_ABORT_FAIL;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_reset
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		sdio reset
******************************************************************************/
uint8_t hw_sdio_reset(void)
{
    uint8_t abort;
    HW_ENTER();

    /* reset在在abort的寄存器的bit3 */
    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_IO_ABORT,0,&abort))
    {
        abort |= 0x8;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_RESET_FAIL;
    }

    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_IO_ABORT,abort,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_RESET_FAIL;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_set_bus_width
 * 参数:  		bus_width(IN)			-->数据宽度
 * 返回值: 	返回执行结果
 * 描述:		设置SDIO的数据宽度
 				00b: 1-bit
				01b: Reserved
				10b: 4-bit bus
				11b: 8-bit bus (only for embedded SDIO)
******************************************************************************/
uint8_t hw_sdio_set_bus_width(uint8_t bus_width)
{
    uint8_t width;
    HW_ENTER();

    if(!hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_BUS_CONTROL,0,&width))
    {
        /* 清除，并且设定数据宽度 */
        width &= ~0x3;
        width |= bus_width;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_SET_BUS_WIDTH_FAIL;
    }

    if(hw_sdio_cmd52(SDIO_EXCU_WRITE,SDIO_FUNC_0,SDIO_CCCR_BUS_CONTROL,width,NULL))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_SET_BUS_WIDTH_FAIL;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_get_bus_width
 * 参数:  		bus_width(OUT)			-->数据宽度
 * 返回值: 	返回执行结果
 * 描述:		获取DIO的数据宽度
 				00b: 1-bit
				01b: Reserved
				10b: 4-bit bus
				11b: 8-bit bus (only for embedded SDIO)
******************************************************************************/
uint8_t hw_sdio_get_bus_width(uint8_t *bus_width)
{
    HW_ENTER();
    if(!bus_width)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    if(hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0,SDIO_CCCR_BUS_CONTROL,0,bus_width))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_GET_BUS_WIDTH_FAIL;
    }
    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_get_cis_ptr
 * 参数:  		func_num(IN)				-->func编号
 				bus_width(OUT)			-->数据宽度
 * 返回值: 	返回执行结果
 * 描述:		根据func编号获取CIS指针
******************************************************************************/
uint8_t hw_sdio_get_cis_ptr(uint8_t func_num,uint32_t *ptr_address)
{
    uint8_t index;
    HW_ENTER();

    uint32_t prt_temp = 0;
    if(func_num > SDIO_FUNC_MAX || (!ptr_address))
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    /* CIS的指针分别为:0x9,0xa,0xb，获取到组合起来就是CIS指针 */
    for (index = 0; index < 3; index++)
    {
        uint8_t x;

        hw_sdio_cmd52(SDIO_EXCU_READ, SDIO_FUNC_0,SDIO_FBR_BASE(func_num) + SDIO_CCCR_CIS_PTR + index, 0, &x);

        prt_temp |= x << (index * 8);
    }

    *ptr_address = prt_temp;
    HW_LEAVE();
    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_set_blk_size
 * 参数:  		func_num(IN)				-->func编号
 				blk_size(IN)				-->设置block size
 * 返回值: 	返回执行结果
 * 描述:		设置特定的func的block size
******************************************************************************/
uint8_t hw_sdio_set_blk_size(uint8_t func_num,uint16_t blk_size)
{
    HW_ENTER();

    /* 设置block size */
    hw_sdio_cmd52(SDIO_EXCU_WRITE, SDIO_FUNC_0,SDIO_FBR_BASE(func_num) + SDIO_CCCR_BLK_SIZE, blk_size & 0xff, NULL);
    hw_sdio_cmd52(SDIO_EXCU_WRITE, SDIO_FUNC_0,SDIO_FBR_BASE(func_num) + SDIO_CCCR_BLK_SIZE+1, (blk_size >> 8)&0xff, NULL);

    /* 更新到特定的func num 结构体中 */
    if((phw_sdio_core->func)[func_num])
    {
        (phw_sdio_core->func)[func_num]->cur_blk_size = blk_size;
    }
    else
    {
        return HW_ERR_SDIO_INVALID_FUNC_NUM;
    }
    HW_LEAVE();
    return  HW_ERR_OK;

}

/******************************************************************************
 *	函数名:	hw_sdio_get_blk_size
 * 参数:  		func_num(IN)				-->func编号
 				blk_size(OUT)				-->block size
 * 返回值: 	返回执行结果
 * 描述:		获取block size
 				NOTED:不是直接读取寄存器，而是通过func的结构体获得
******************************************************************************/
uint8_t hw_sdio_get_blk_size(uint8_t func_num,uint16_t *blk_size)
{
    HW_ENTER();

    if(!blk_size)
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_PARA;
    }

    if((phw_sdio_core->func)[func_num])
    {
        *blk_size = (phw_sdio_core->func)[func_num]->cur_blk_size;
    }
    else
    {
        HW_LEAVE();
        return HW_ERR_SDIO_INVALID_FUNC_NUM;
    }

    HW_LEAVE();
    return  HW_ERR_OK;
}


/******************************************************************************
 *	函数名:	hw_sdio_cmd52
 * 参数:  		write(IN)			-->执行操作，read or write
 				func_num(IN)		-->func的编号
 				address(IN)		-->address地址
 				para(IN)			-->要写的参数
 				resp(OUT)			-->读要返回的数据
 * 返回值: 	返回执行结果
 * 描述:		执行CMD52的动作
******************************************************************************/
uint8_t hw_sdio_cmd52(uint8_t write,uint8_t func_num,uint32_t address,uint8_t para,uint8_t *resp)
{
	uint8_t error_status;
    uint8_t response;
    SDIO_CmdInitTypeDef SDIO_CmdInitStructure;

    SDIO_CmdInitStructure.Argument = write ? 0x80000000 : 0x00000000;
    SDIO_CmdInitStructure.Argument |= func_num << 28;
    SDIO_CmdInitStructure.Argument |= (write && resp) ? 0x08000000 : 0x00000000;
    SDIO_CmdInitStructure.Argument |= address << 9;
    SDIO_CmdInitStructure.Argument |= para;
    SDIO_CmdInitStructure.CmdIndex = 52;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_SendCommand(SDIO, &SDIO_CmdInitStructure);

    /* 等待发送完成 */
    while (__SDIO_GET_FLAG(SDIO, SDIO_FLAG_CMDACT) == SET);
    error_status = hw_sdio_check_err();

    if (HW_ERR_OK != error_status)
    {
        return  HW_ERR_SDIO_CMD52_FAIL;
    }

    response = SDIO_GetResponse(SDIO,SDIO_RESP1) & 0xff;
    if((!write) && resp)
    {
        *resp = response;
    }
    return  HW_ERR_OK;
}



/******************************************************************************
 *	函数名:	hw_sdio_cmd52
 * 参数:  		write(IN)			-->执行操作，read or write
 				func_num(IN)		-->func的编号
 				address(IN)		-->address地址
 				incr_addr(IN)		-->地址是否累加
 				buf(IN/OUT)		-->如果操作是写，那么此参数就是要write的buffer
 										如果操作是读，那么此参数就是read返回的buffer
 				size(IN)			-->读或者写的size
 * 返回值: 	返回执行结果
 * 描述:		执行CMD53的动作
******************************************************************************/
uint8_t hw_sdio_cmd53(uint8_t write, uint8_t func_num,uint32_t address, uint8_t incr_addr, uint8_t *buf,uint32_t size)
{
    uint16_t func_cur_blk_size;

    if((phw_sdio_core->func)[func_num])
    {
        func_cur_blk_size = (phw_sdio_core->func)[func_num]->cur_blk_size;
        if(func_cur_blk_size == 0)
        {
            return HW_ERR_SDIO_BLK_SIZE_ZERO;
        }
    }
    else
    {
        return HW_ERR_SDIO_INVALID_FUNC_NUM;
    }

    if(write)
    {
        /* CMD53 write */
        hw_sdio_cmd53_write(func_num,address,incr_addr,buf,size,func_cur_blk_size);
    }
    else
    {
        /* CMD53 read */
        hw_sdio_cmd53_read(func_num,address,incr_addr,buf,size,func_cur_blk_size);
    }

    return  HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_core_init
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		sdio core的初始化
******************************************************************************/
static uint8_t hw_sdio_core_init(void)
{
    hw_memset(&hw_sdio_func,0,sizeof(sdio_func_t)*SDIO_FUNC_MAX);
    hw_memset(&hw_sdio_core,0,sizeof(sdio_core_t));
    hw_sdio_func[SDIO_FUNC_0].func_status = FUNC_INT_ENABLE;
    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_parse_r6
 * 参数:  		r6(IN)			-->R6的入参
 				rca(OUT)		-->rca的返回值
 * 返回值: 	返回执行结果
 * 描述:		解析R6的response
******************************************************************************/
static uint8_t hw_sdio_parse_r6(uint32_t r6,uint32_t *rca)
{
    HW_ENTER();
    if(rca)
    {
        *rca = RCA_IN_R6(r6);
        HW_LEAVE();
        return HW_ERR_OK;
    }
    HW_LEAVE();
    return HW_ERR_SDIO_INVALID_PARA;
}

/******************************************************************************
 *	函数名:	hw_sdio_parse_r4
 * 参数:  		r4(IN)			-->R4的入参
 * 返回值: 	返回执行结果
 * 描述:		解析R4，主要关注func的数量
******************************************************************************/
static uint8_t hw_sdio_parse_r4(uint32_t r4)
{
    HW_ENTER();
    uint32_t index = 0;

    phw_sdio_core->func_total_num = FUNC_NUM_IN_R4(r4);
    for(index = 0; index < phw_sdio_core->func_total_num; index++)
    {
        (phw_sdio_core->func)[index] = &hw_sdio_func[index];
        hw_sdio_func[index].func_num = index;
    }

    HW_LEAVE();
    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_cis_read_parse
 * 参数:  		func_num(IN)			-->func编号
 				cis_ptr(IN)			-->CIS指针
 * 返回值: 	返回执行结果
 * 描述:		读取CIS数据并且做解析，重要的数据存储到core的结构体中
******************************************************************************/
static uint8_t hw_sdio_cis_read_parse(uint8_t func_num,uint32_t cis_ptr)
{
    /* SDIO协议上有这样一句话 :No SDIO card tuple can be longer than 257 bytes
     * 1 byte TPL_CODE + 1 byte TPL_LINK +
     *	FFh byte tuple body (and this 257 bytetuple ends the chain)
     * 所以我们定义的数据是是255
     */
    uint8_t data[255];
    uint8_t index,len;
    uint8_t tpl_code = CISTPL_NULL;
    uint8_t tpl_link;
    uint32_t cis_ptr_temp = cis_ptr;

    HW_ENTER();
    while (tpl_code != CISTPL_END)
    {
        hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0, cis_ptr_temp++,0,&tpl_code);
        if (tpl_code == CISTPL_NULL)
            continue;

        /* 本结点数据的大小 */
        hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0, cis_ptr_temp++,0,&tpl_link);

        for (index = 0; index < tpl_link; index++)
            hw_sdio_cmd52(SDIO_EXCU_READ,SDIO_FUNC_0, cis_ptr_temp + index,0,&data[index]);

        switch (tpl_code)
        {
        case CISTPL_VERS_1:
            HW_DEBUG("Product Information:");
            for (index = 2; data[index] != 0xff; index += len + 1)
            {
                // 遍历所有字符串
                len = hw_strlen((char *)data + index);
                if (len != 0)
                    HW_DEBUG(" %s", data + index);
            }
            HW_DEBUG("\n");
            break;
        case CISTPL_MANFID:
            // 16.6 CISTPL_MANFID: Manufacturer Identification String Tuple
            HW_DEBUG("Manufacturer Code: 0x%04x\n", *(uint16_t *)data); // TPLMID_MANF
            HW_DEBUG("Manufacturer Information: 0x%04x\n", *(uint16_t *)(data + 2)); // TPLMID_CARD
            phw_sdio_core->manf_code = *(uint16_t *)data;
            phw_sdio_core->manf_info = *(uint16_t *)(data + 2);
            break;
        case CISTPL_FUNCID:
            // 16.7.1 CISTPL_FUNCID: Function Identification Tuple
            HW_DEBUG("Card Function Code: 0x%02x\n", data[0]); // TPLFID_FUNCTION
            HW_DEBUG("System Initialization Bit Mask: 0x%02x\n", data[1]); // TPLFID_SYSINIT
            break;
        case CISTPL_FUNCE:
            // 16.7.2 CISTPL_FUNCE: Function Extension Tuple
            if (data[0] == 0)
            {
                // 16.7.3 CISTPL_FUNCE Tuple for Function 0 (Extended Data 00h)
                HW_DEBUG("Maximum Block Size case1: func: %d,size %d\n", func_num, *(uint16_t *)(data + 1));
                HW_DEBUG("Maximum Transfer Rate Code: 0x%02x\n", data[3]);
                if((phw_sdio_core->func)[func_num])
                {
                    (phw_sdio_core->func)[func_num]->max_blk_size = *(uint16_t *)(data + 1);
                }
            }
            else
            {
                // 16.7.4 CISTPL_FUNCE Tuple for Function 1-7 (Extended Data 01h)
                HW_DEBUG("Maximum Block Size case2 func: %d,size %d\n", func_num,*(uint16_t *)(data + 0x0c)); // TPLFE_MAX_BLK_SIZE
                if((phw_sdio_core->func)[func_num])
                {
                    (phw_sdio_core->func)[func_num]->max_blk_size = *(uint16_t *)(data + 0x0c);
                }

            }
            break;
        default:
            HW_DEBUG("[CIS Tuple 0x%02x] addr=0x%08x size=%d\n", tpl_code, cis_ptr_temp - 2, tpl_link);
#if HW_DEBUG_ENABLE > 0
            hw_hex_dump(data, tpl_link);
#endif
        }

        if (tpl_link == 0xff)
            break; // 当TPL_LINK为0xff时说明当前结点为尾节点
        cis_ptr_temp += tpl_link;
    }

    HW_LEAVE();
    return HW_ERR_OK;
}


static uint8_t hw_sdio_set_dblocksize(uint32_t *struct_dblocksize,uint32_t block_size)
{
    uint32_t dblock_size;
    switch (block_size)
    {
    case 1:
        dblock_size = SDIO_DATABLOCK_SIZE_1B;
        break;
    case 2:
        dblock_size = SDIO_DATABLOCK_SIZE_2B;
        break;
    case 4:
        dblock_size = SDIO_DATABLOCK_SIZE_4B;
        break;
    case 8:
        dblock_size = SDIO_DATABLOCK_SIZE_8B;
        break;
    case 16:
        dblock_size = SDIO_DATABLOCK_SIZE_16B;
        break;
    case 32:
        dblock_size = SDIO_DATABLOCK_SIZE_32B;
        break;
    case 64:
        dblock_size = SDIO_DATABLOCK_SIZE_64B;
        break;
    case 128:
        dblock_size = SDIO_DATABLOCK_SIZE_128B;
        break;
    case 256:
        dblock_size = SDIO_DATABLOCK_SIZE_256B;
        break;
    case 512:
        dblock_size = SDIO_DATABLOCK_SIZE_512B;
        break;
    case 1024:
        dblock_size = SDIO_DATABLOCK_SIZE_1024B;
        break;
    case 2048:
        dblock_size = SDIO_DATABLOCK_SIZE_2048B;
        break;
    case 4096:
        dblock_size = SDIO_DATABLOCK_SIZE_4096B;
        break;
    case 8192:
        dblock_size = SDIO_DATABLOCK_SIZE_8192B;
        break;
    case 16384:
        dblock_size = SDIO_DATABLOCK_SIZE_16384B;
        break;
    }
    *struct_dblocksize = dblock_size;
    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_check_err
 * 参数:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		验证有什么错误，并且清除相应的flag
******************************************************************************/
static uint8_t hw_sdio_check_err()
{
	uint8_t err = HW_ERR_OK;

    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CCRCFAIL) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_CCRCFAIL);
        err++;
        HW_DEBUG("%s: CMD%d CRC failed!\n", __func__, SDIO->CMD & SDIO_CMD_CMDINDEX);
    }
    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CTIMEOUT) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_CTIMEOUT);
        err++;
        HW_DEBUG("%s: CMD%d timeout!\n", __func__, SDIO->CMD & SDIO_CMD_CMDINDEX);
    }
    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_DCRCFAIL) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_DCRCFAIL);
        err++;
        HW_DEBUG("%s: data CRC failed!\n", __func__);
    }
    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_DTIMEOUT) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_DTIMEOUT);
        err++;
        HW_DEBUG("%s: data timeout!\n", __func__);
    }
    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_TXUNDERR) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_TXUNDERR);
        err++;
        HW_DEBUG("%s: data underrun!\n", __func__);
    }
    if (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_RXOVERR) == SET)
    {
        __SDIO_CLEAR_FLAG(SDIO,SDIO_FLAG_RXOVERR);
        err++;
        HW_DEBUG("%s: data overrun!\n", __func__);
    }

    return err;
}

/******************************************************************************
 *	函数名:	hw_sdio_cmd3
 * 参数:  		para(IN)		-->发送cmd3的参数
 				resp			-->cmd3的返回值
 * 返回值: 	返回执行结果
 * 描述:		发送cmd3
******************************************************************************/
static uint8_t hw_sdio_cmd3(uint32_t para,uint32_t *resp)
{
	uint8_t error_status;
    uint32_t response;
    SDIO_CmdInitTypeDef SDIO_CmdInitStructure;

    SDIO_CmdInitStructure.Argument = para;
    SDIO_CmdInitStructure.CmdIndex = SDIO_CMD3;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;
    SDIO_SendCommand(SDIO,&SDIO_CmdInitStructure);

    /* 等待发送完成 */
    while (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CMDACT) == SET);
    error_status = hw_sdio_check_err();

    if (HW_ERR_OK != error_status)
    {
        return error_status;
    }

    /* 获取到response的结果 */
    response = SDIO_GetResponse(SDIO,SDIO_RESP1);
    if (resp)
    {
        *resp = response;
    }

    return (error_status);
}

/******************************************************************************
 *	函数名:	hw_sdio_cmd5
 * 参数:  		para(IN)			-->入参
 				resp(OUT)			-->返回值
 				retry_max(IN)		-->最大尝试次数
 * 返回值: 	返回执行结果
 * 描述:		发送cmd5
******************************************************************************/
static uint8_t hw_sdio_cmd5(uint32_t para,uint32_t *resp,uint32_t retry_max)
{
	uint32_t index;
    uint32_t response;
    uint8_t error_status;
    SDIO_CmdInitTypeDef SDIO_CmdInitStructure;

    HW_ENTER();
    SDIO_CmdInitStructure.Argument = para;
    SDIO_CmdInitStructure.CmdIndex = SDIO_CMD5 ;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;
    for (index = 0; index < retry_max; index++)
    {
        SDIO_SendCommand(SDIO,&SDIO_CmdInitStructure);
        /* 等待发送完成 */
        while (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CMDACT) == SET);
        error_status = hw_sdio_check_err();

        if (HW_ERR_OK != error_status)
        {
            continue;
        }
        response = SDIO_GetResponse(SDIO,SDIO_RESP1);

        /* 判断是否OK */
        if(C_IN_R4(response))
        {
            if (resp)
            {
                *resp = response;
            }
            break;
        }
    }

    HW_LEAVE();
    return error_status;
}


/******************************************************************************
 *	函数名:	hw_sdio_cmd7
 * 参数:  		para(IN)			-->入参
 				resp(OUT)			-->返回值
 * 返回值: 	返回执行结果
 * 描述:		发送cmd7
******************************************************************************/
static uint8_t hw_sdio_cmd7(uint32_t para,uint32_t *resp)
{
	uint8_t error_status;
    uint32_t response;
    SDIO_CmdInitTypeDef SDIO_CmdInitStructure;

    HW_ENTER();
    /* Send CMD7 SDIO_SEL_DESEL_CARD */
    SDIO_CmdInitStructure.Argument = para;
    SDIO_CmdInitStructure.CmdIndex = SDIO_CMD7;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;
    SDIO_SendCommand(SDIO,&SDIO_CmdInitStructure);

    /* 等待发送完成 */
    while (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CMDACT) == SET);
    error_status = hw_sdio_check_err();

    if (HW_ERR_OK != error_status)
    {
        return error_status;
    }
    /* 获取返回结果 */
    response = SDIO_GetResponse(SDIO,SDIO_RESP1);
    if (resp)
    {
        *resp = response;
    }

    HW_ENTER();
    return (error_status);
}


/******************************************************************************
 *	函数名:	hw_sdio_cmd53_read
 * 参数:  		func_num(IN)			-->func编号
 				address(IN)			-->要读取的地址
 				incr_addr(IN)			-->地址是否累加
 				buf(OUT)				-->数据返回buffer
 				size(IN)				-->要读取的size
 				cur_blk_size(IN)		-->当前的func编号的block size
 * 返回值: 	返回执行结果
 * 描述:		执行CMD53的read，采用block mode
******************************************************************************/
static uint8_t hw_sdio_cmd53_read(uint8_t func_num,uint32_t address, uint8_t incr_addr, uint8_t *buf,uint32_t size,uint16_t cur_blk_size)
{
    uint8_t error_status;
    uint32_t remain_size = size;
	SDIO_CmdInitTypeDef SDIO_CmdInitStructure = {0};
	SDIO_DataInitTypeDef SDIO_DataInitStructure = {0};
	LL_DMA_InitTypeDef DMA_DataInitStructure = {0};

    /* 2.启动DMA */
	LL_DMA_DeInit(DMA2,LL_DMA_STREAM_3);
	LL_DMA_DisableStream(DMA2,LL_DMA_STREAM_3);
	LL_DMA_ClearFlag_TC3(DMA2);
	
	DMA_DataInitStructure.Channel = LL_DMA_CHANNEL_4;
  DMA_DataInitStructure.Direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
  DMA_DataInitStructure.FIFOMode = LL_DMA_FIFOMODE_ENABLE;
  DMA_DataInitStructure.FIFOThreshold = LL_DMA_FIFOTHRESHOLD_FULL;
  DMA_DataInitStructure.MemBurst = LL_DMA_MBURST_INC4;
  DMA_DataInitStructure.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_WORD;
  DMA_DataInitStructure.Mode = LL_DMA_MODE_PFCTRL;
  if(remain_size%cur_blk_size)
    {
        DMA_DataInitStructure.NbData = (remain_size/cur_blk_size+1)*cur_blk_size/4;
    }
    else
    {
        DMA_DataInitStructure.NbData = (remain_size/cur_blk_size)*cur_blk_size/4;
    }
  DMA_DataInitStructure.PeriphBurst = LL_DMA_PBURST_INC4;
  DMA_DataInitStructure.PeriphOrM2MSrcAddress = (uint32_t)&SDIO->FIFO;
  DMA_DataInitStructure.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_WORD;
  DMA_DataInitStructure.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
  DMA_DataInitStructure.Priority = LL_DMA_PRIORITY_VERYHIGH;
    DMA_DataInitStructure.MemoryOrM2MDstAddress = (uint32_t)buf;
    DMA_DataInitStructure.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;

  LL_DMA_Init(DMA2, LL_DMA_STREAM_3, &DMA_DataInitStructure);
  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_3);


    /* 3.配置SDIO data结构体 */
    SDIO_DataInitStructure.DataTimeOut = SDIO_24M_DATATIMEOUT;
    if(remain_size%cur_blk_size)
    {
        SDIO_DataInitStructure.DataLength = (remain_size/cur_blk_size+1)*cur_blk_size;
    }
    else
    {
        SDIO_DataInitStructure.DataLength = (remain_size/cur_blk_size)*cur_blk_size;
    }

    hw_sdio_set_dblocksize(&SDIO_DataInitStructure.DataBlockSize,cur_blk_size);
    SDIO_DataInitStructure.TransferDir = SDIO_TRANSFER_DIR_TO_SDIO;
    SDIO_DataInitStructure.TransferMode = SDIO_TRANSFER_MODE_BLOCK;

    SDIO_DataInitStructure.DPSM = SDIO_DPSM_ENABLE;
    SDIO_ConfigData(SDIO,&SDIO_DataInitStructure);


	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_CMDREND);

/* 1.发送CMD53 */
    /* CMD53的命令参数格式为 */
    /* |--RW FLAG--|--FUNC NUM--|--BLK MODE--|--OP MODE--|--REG ADDR--|--BYTE/BLK CNT--| */
    /* |--1  BYTE--|--3   BYTE--|--1   BYTE--|--1  BYTE--|--17  BYTE--|--9      BYTE --| */
    SDIO_CmdInitStructure.Argument = 0x0;					/* CMD53的R/W read的flag */
    SDIO_CmdInitStructure.Argument |= func_num << 28;	/* FUNC */
    SDIO_CmdInitStructure.Argument |= 0x08000000;			/* Block mode */
    SDIO_CmdInitStructure.Argument |= incr_addr ? 0x04000000 : 0x0;	/* OP MODE :1.递增 0,固定地址 */
    if(incr_addr)
        SDIO_CmdInitStructure.Argument |= (address) << 9;/* REG ADDR,要写入的地址 */
    else
        SDIO_CmdInitStructure.Argument |= address << 9;		/* REG ADDR,要写入的地址 */

    if(remain_size%cur_blk_size)
    {
        SDIO_CmdInitStructure.Argument |= (remain_size/cur_blk_size+1);
    }
    else
    {
        SDIO_CmdInitStructure.Argument |= remain_size/cur_blk_size;
    }
    SDIO_CmdInitStructure.CmdIndex = SDIO_CMD53;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;
	
    SDIO_SendCommand(SDIO,&SDIO_CmdInitStructure);

    /* 等待发送完成 */
	while (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CMDREND) == RESET);
    error_status = hw_sdio_check_err();

    if (HW_ERR_OK != error_status)
    {
        return  HW_ERR_SDIO_CMD53_FAIL;
    }

	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_CMDREND);
	
    while (LL_DMA_IsActiveFlag_TC3(DMA2) == RESET); /* 等待DMA发送成功 */
	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_DATAEND);

    LL_DMA_DeInit(DMA2,LL_DMA_STREAM_3);
	LL_DMA_DisableStream(DMA2,LL_DMA_STREAM_3);
	LL_DMA_ClearFlag_TC3(DMA2);  /* 清除DMA传输完成标志位 */

    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_cmd53_write
 * 参数:  		func_num(IN)			-->func编号
 				address(IN)			-->要读取的地址
 				incr_addr(IN)			-->地址是否累加
 				buf(IN)				-->数据返回buffer
 				size(IN)				-->要读取的size
 				cur_blk_size(IN)		-->当前的func编号的block size
 * 返回值: 	返回执行结果
 * 描述:		执行CMD53的write，采用block mode写下去
******************************************************************************/
static uint8_t hw_sdio_cmd53_write(uint8_t func_num,uint32_t address, uint8_t incr_addr, uint8_t *buf,uint32_t size,uint16_t cur_blk_size)
{
    uint8_t error_status = 0;
    uint32_t remain_size = size;
	SDIO_CmdInitTypeDef SDIO_CmdInitStructure = {0};
	SDIO_DataInitTypeDef SDIO_DataInitStructure = {0};
	LL_DMA_InitTypeDef DMA_DataInitStructure = {0};

	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_CMDREND);

/* 1.发送CMD53 */
    /* CMD53的命令参数格式为 */
    /* |--RW FLAG--|--FUNC NUM--|--BLK MODE--|--OP MODE--|--REG ADDR--|--BYTE/BLK CNT--| */
    /* |--1  BYTE--|--3   BYTE--|--1   BYTE--|--1  BYTE--|--17  BYTE--|--9      BYTE --| */
    SDIO_CmdInitStructure.Argument = 0x80000000;			/* CMD53的R/W write的flag */
    SDIO_CmdInitStructure.Argument |= func_num << 28;	/* FUNC */
    SDIO_CmdInitStructure.Argument |= 0x08000000;			/* Block mode */
    SDIO_CmdInitStructure.Argument |= incr_addr ? 0x04000000 : 0x0;	/* OP MODE :1.递增 0,固定地址 */

    SDIO_CmdInitStructure.Argument |= address << 9;		/* REG ADDR,要写入的地址 */

    if(remain_size%cur_blk_size)
    {
        SDIO_CmdInitStructure.Argument |= (remain_size/cur_blk_size+1);
    }
    else
    {
        SDIO_CmdInitStructure.Argument |= remain_size/cur_blk_size;
    }

    SDIO_CmdInitStructure.CmdIndex = SDIO_CMD53;
    SDIO_CmdInitStructure.Response = SDIO_RESPONSE_SHORT;
    SDIO_CmdInitStructure.WaitForInterrupt = SDIO_WAIT_NO;
    SDIO_CmdInitStructure.CPSM = SDIO_CPSM_ENABLE;

    SDIO_SendCommand(SDIO,&SDIO_CmdInitStructure);

    /* 等待发送完成 */
    while (__SDIO_GET_FLAG(SDIO,SDIO_FLAG_CMDREND) == RESET);
    error_status = hw_sdio_check_err();

    if (HW_ERR_OK != error_status)
    {
        return  HW_ERR_SDIO_CMD53_FAIL;
    }

	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_CMDREND);

    /* 2.启动DMA */
	DMA_DataInitStructure.Channel = LL_DMA_CHANNEL_4;
  DMA_DataInitStructure.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
  DMA_DataInitStructure.FIFOMode = LL_DMA_FIFOMODE_ENABLE; 
  DMA_DataInitStructure.FIFOThreshold = LL_DMA_FIFOTHRESHOLD_FULL;
  DMA_DataInitStructure.MemBurst = LL_DMA_MBURST_INC4;
  DMA_DataInitStructure.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_WORD;
  DMA_DataInitStructure.Mode = LL_DMA_MODE_PFCTRL;
  if(remain_size%cur_blk_size)
    {
        DMA_DataInitStructure.NbData = (remain_size/cur_blk_size+1)*cur_blk_size/4;
    }
    else
    {
        DMA_DataInitStructure.NbData = (remain_size/cur_blk_size)*cur_blk_size/4;
    }
  DMA_DataInitStructure.PeriphBurst = LL_DMA_PBURST_INC4;
  DMA_DataInitStructure.PeriphOrM2MSrcAddress = (uint32_t)&SDIO->FIFO;
  DMA_DataInitStructure.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_WORD;
  DMA_DataInitStructure.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
  DMA_DataInitStructure.Priority = LL_DMA_PRIORITY_VERYHIGH;
    DMA_DataInitStructure.MemoryOrM2MDstAddress = (uint32_t)buf;
    DMA_DataInitStructure.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;

  LL_DMA_Init(DMA2, LL_DMA_STREAM_3, &DMA_DataInitStructure);
  LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_3);

    /* 3.配置SDIO data结构体 */
SDIO_DataInitStructure.DataTimeOut = SDIO_24M_DATATIMEOUT;

    if(remain_size%cur_blk_size)
    {
        SDIO_DataInitStructure.DataLength = (remain_size/cur_blk_size+1)*cur_blk_size;
    }
    else
    {
        SDIO_DataInitStructure.DataLength = (remain_size/cur_blk_size)*cur_blk_size;
    }


    hw_sdio_set_dblocksize(&SDIO_DataInitStructure.DataBlockSize,cur_blk_size);
    SDIO_DataInitStructure.TransferDir = SDIO_TRANSFER_DIR_TO_CARD;
    SDIO_DataInitStructure.TransferMode = SDIO_TRANSFER_MODE_BLOCK;

    SDIO_DataInitStructure.DPSM = SDIO_DPSM_ENABLE;

    SDIO_ConfigData(SDIO,&SDIO_DataInitStructure);


    while (LL_DMA_IsActiveFlag_TC3(DMA2) == RESET); /* 等待DMA发送成功 */
	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_DATAEND);/* 清除发送完成标志 */
	__SDIO_CLEAR_FLAG(SDIO, SDIO_FLAG_DBCKEND);/* 清除发送/接收数据块 */

    LL_DMA_DeInit(DMA2,LL_DMA_STREAM_3);
	LL_DMA_DisableStream(DMA2,LL_DMA_STREAM_3);
	LL_DMA_ClearFlag_TC3(DMA2);  /* 清除DMA传输完成标志位 */

    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	hw_sdio_cmd53_read
 * 参数:  		func_num(IN)			-->func编号
:  		NULL
 * 返回值: 	返回执行结果
 * 描述:		chip reset,此部分用WIFI模组的PDN引脚 PD5
******************************************************************************/
uint8_t hw_chip_reset()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    CHIP_RESET_LOW;
    hw_delay_ms(10);
    CHIP_RESET_HIGH;
    hw_delay_ms(10);

    return HW_ERR_OK;
}

/******************************************************************************
 *	函数名:	SDIO_IRQHandler
 * 参数:  		NULL
 * 返回值: 	NULL
 * 描述:		SDIO中断处理函数
******************************************************************************/
void SDIO_IRQHandler(void)
{
    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_CCRCFAIL) == SET)
    {
        /* 发生命令CRC错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_CCRCFAIL);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_CCRCFAIL OCCUR\n");
    }
    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_DCRCFAIL) == SET)
    {
        /* 发生数据CRC错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_DCRCFAIL);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_DCRCFAIL OCCUR\n");
    }
    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_CTIMEOUT) == SET)
    {
        /* 发生命令超时错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_CTIMEOUT);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_CTIMEOUT OCCUR\n");
    }
    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_DTIMEOUT) == SET)
    {
        /* 发生数据超时错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_DTIMEOUT);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_DTIMEOUT OCCUR\n");
    }
    if(__SDIO_GET_FLAG(SDIO, SDIO_IT_TXUNDERR) == SET)
    {
        /* 发生发送FIFO下溢错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_TXUNDERR);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_TXUNDERR OCCUR\n");
    }
    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_RXOVERR) == SET)
    {
        /* 发生接收FIFO上溢错误 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_RXOVERR);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_RXOVERR OCCUR\n");
    }

    if(__SDIO_GET_FLAG(SDIO,SDIO_IT_SDIOIT) == SET)
    {
        /* 收到SDIO中断 */
        __SDIO_CLEAR_FLAG(SDIO,SDIO_IT_SDIOIT);
        HW_DEBUG("SDIO_IRQHandler:SDIO_IT_SDIOIT OCCUR\n");
        //wifi_process_packet();
    }
}

  
