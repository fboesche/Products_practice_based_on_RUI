#include "rui.h"
#include "board.h"
#include "sensor.h"

static RUI_RETURN_STATUS rui_return_status;
//join cnt
#define JOIN_MAX_CNT 6
static uint8_t JoinCnt=0;
RUI_LORA_STATUS_T app_lora_status; //record status 

/*******************************************************************************************
 * The BSP user functions.
 * 
 * *****************************************************************************************/ 
#define LED_1                                   8
#define LED_2                                   9
#define BAT_LEVEL_CHANNEL                       20


const uint8_t level[2]={0,1};
#define low     &level[0]
#define high    &level[1]
RUI_GPIO_ST Led_Blue;  //join LoRaWAN successed indicator light
RUI_GPIO_ST Led_Green;  //send data successed indicator light 
RUI_GPIO_ST Bat_level;
RUI_I2C_ST I2c_1;
TimerEvent_t Led_Green_Timer;
TimerEvent_t Led_Blue_Timer;  //LoRa send out indicator light
volatile static bool autosend_flag = false;    //auto send flag
static uint8_t a[80]={};    // Data buffer to be sent by lora
bool IsJoiningflag= false;  //flag whether joining or not status
bool sample_flag = false;  //sensor sample record




void rui_lora_autosend_callback(void)  //auto_send timeout event callback
{
    autosend_flag = true;  
    IsJoiningflag = false;    
    bsp_i2c_init();
    rui_delay_ms(50);
}

void OnLed_Green_TimerEvent(void)
{  
    rui_timer_stop(&Led_Green_Timer);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Green, high);

    rui_lora_get_status(false,&app_lora_status);;//The query gets the current device status 
    if(app_lora_status.autosend_status)
    {
        autosend_flag = true;  //set autosend_flag after join LoRaWAN succeeded 
    }
}

void OnLed_Blue_TimerEvent(void)
{
    rui_timer_stop(&Led_Blue_Timer);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Blue, high);

    rui_lora_get_status(false,&app_lora_status);  //The query gets the current status
    switch(app_lora_status.autosend_status)
    {
        case RUI_AUTO_ENABLE_SLEEP:rui_lora_set_send_interval(RUI_AUTO_ENABLE_SLEEP,app_lora_status.lorasend_interval);  //start autosend_timer after send success
            rui_delay_ms(5);  
            break;
        case RUI_AUTO_ENABLE_NORMAL:rui_lora_set_send_interval(RUI_AUTO_ENABLE_NORMAL,app_lora_status.lorasend_interval);  //start autosend_timer after send success
            break;
        default:break;
    }
}
void bsp_led_init(void)
{
    Led_Green.pin_num = LED_1;
    Led_Blue.pin_num = LED_2;
    Led_Green.dir = RUI_GPIO_PIN_DIR_OUTPUT;
    Led_Blue.dir = RUI_GPIO_PIN_DIR_OUTPUT;
    Led_Green.pull = RUI_GPIO_PIN_NOPULL;
    Led_Blue.pull = RUI_GPIO_PIN_NOPULL;

    rui_gpio_init(&Led_Green);
    rui_gpio_init(&Led_Blue);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Green,low);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Blue,low);
    rui_delay_ms(200);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Green,high);
    rui_gpio_rw(RUI_IF_WRITE,&Led_Blue,high);
    rui_timer_init(&Led_Green_Timer,OnLed_Green_TimerEvent);
    rui_timer_init(&Led_Blue_Timer,OnLed_Blue_TimerEvent);
    rui_timer_setvalue(&Led_Green_Timer,100);
    rui_timer_setvalue(&Led_Blue_Timer,100);
}
void bsp_adc_init(void)
{
    Bat_level.pin_num = BAT_LEVEL_CHANNEL;
    Bat_level.dir = RUI_GPIO_PIN_DIR_INPUT;
    Bat_level.pull = RUI_GPIO_PIN_NOPULL;
    rui_adc_init(&Bat_level);

}
void bsp_i2c_init(void)
{
    I2c_1.INSTANCE_ID = 1;
    I2c_1.PIN_SDA = I2C_SDA;
    I2c_1.PIN_SCL = I2C_SCL;
    I2c_1.FREQUENCY = RUI_I2C_FREQ_100K;

    rui_i2c_init(&I2c_1);

    rui_delay_ms(50);

}
void bsp_init(void)
{
    bsp_led_init();
    bsp_adc_init();
    bsp_i2c_init();
    BME680_Init();
}

extern bsp_sensor_data_t bsp_sensor;
void app_loop(void)
{
    int temp=0;  
    int x,y,z;
    static uint8_t sensor_data_cnt=0;  //send data counter by LoRa
    rui_lora_get_status(false,&app_lora_status);
    if(app_lora_status.IsJoined)
    {
        if (autosend_flag) 
        {
            autosend_flag=false;
            rui_delay_ms(5);               

            BoardBatteryMeasureVolage(&bsp_sensor.voltage);
            bsp_sensor.voltage=bsp_sensor.voltage/1000.0;   //convert mV to V
            RUI_LOG_PRINTF("Battery Voltage = %d.%d V \r\n",(uint32_t)(bsp_sensor.voltage), (uint32_t)((bsp_sensor.voltage)*1000-((int32_t)(bsp_sensor.voltage)) * 1000));
            temp=(uint16_t)round(bsp_sensor.voltage*100.0);
            a[sensor_data_cnt++]=0x08;
            a[sensor_data_cnt++]=0x02;
            a[sensor_data_cnt++]=(temp&0xffff) >> 8;
            a[sensor_data_cnt++]=temp&0xff;				

            if(BME680_get_data(&bsp_sensor.humidity,&bsp_sensor.temperature,&bsp_sensor.pressure,&bsp_sensor.resis)==0)
            {
                a[sensor_data_cnt++]=0x07;
                a[sensor_data_cnt++]=0x68;
                a[sensor_data_cnt++]=( bsp_sensor.humidity / 500 ) & 0xFF;
					
                a[sensor_data_cnt++]=0x06;
                a[sensor_data_cnt++]=0x73;
                a[sensor_data_cnt++]=(( bsp_sensor.pressure / 10 ) >> 8 ) & 0xFF;
                a[sensor_data_cnt++]=(bsp_sensor.pressure / 10 ) & 0xFF;
			
                a[sensor_data_cnt++]=0x02;
                a[sensor_data_cnt++]=0x67;
                a[sensor_data_cnt++]=(( bsp_sensor.temperature / 10 ) >> 8 ) & 0xFF;
                a[sensor_data_cnt++]=(bsp_sensor.temperature / 10 ) & 0xFF;

                a[sensor_data_cnt++] = 0x04;
				a[sensor_data_cnt++] = 0x02; //analog output
				a[sensor_data_cnt++] = (((int32_t)(bsp_sensor.resis / 10)) >> 8) & 0xFF;
				a[sensor_data_cnt++] = ((int32_t)(bsp_sensor.resis / 10 )) & 0xFF;
            }


	        if(sensor_data_cnt != 0)
            {                    
                sample_flag = true;
                RUI_LOG_PRINTF("\r\n");
                rui_return_status = rui_lora_send(8,a,sensor_data_cnt);
                switch(rui_return_status)
                {
                    case RUI_STATUS_OK:RUI_LOG_PRINTF("[LoRa]: send out\r\n");
                        break;
                    default: RUI_LOG_PRINTF("[LoRa]: send error %d\r\n",rui_return_status);
                        rui_lora_get_status(false,&app_lora_status); 
                        switch(app_lora_status.autosend_status)
                        {
                            case RUI_AUTO_ENABLE_SLEEP:rui_lora_set_send_interval(RUI_AUTO_ENABLE_SLEEP,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                                break;
                            case RUI_AUTO_ENABLE_NORMAL:rui_lora_set_send_interval(RUI_AUTO_ENABLE_NORMAL,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                                break;
                            default:break;
                        }
						break;
                }                 
                sensor_data_cnt=0;                       
            }	
            else 
            {                
                rui_lora_set_send_interval(RUI_AUTO_DISABLE,0);  //stop it auto send data if no sensor data.
            }
        }
    }else if(IsJoiningflag == false)
    {
        IsJoiningflag = true;
        if(app_lora_status.join_mode == RUI_OTAA)
        {
            rui_return_status = rui_lora_join();
            switch(rui_return_status)
            {
                case RUI_STATUS_OK:RUI_LOG_PRINTF("OTAA Join Start...\r\n");break;
                case RUI_LORA_STATUS_PARAMETER_INVALID:RUI_LOG_PRINTF("ERROR: RUI_AT_PARAMETER_INVALID %d\r\n",RUI_AT_PARAMETER_INVALID);
                    rui_lora_get_status(false,&app_lora_status);  //The query gets the current status 
                    switch(app_lora_status.autosend_status)
                    {
                        case RUI_AUTO_ENABLE_SLEEP:rui_lora_set_send_interval(RUI_AUTO_ENABLE_SLEEP,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                            break;
                        case RUI_AUTO_ENABLE_NORMAL:rui_lora_set_send_interval(RUI_AUTO_ENABLE_NORMAL,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                            break;
                        default:break;
                    } 
                    break;
                default: RUI_LOG_PRINTF("ERROR: LORA_STATUS_ERROR %d\r\n",rui_return_status);
                    rui_lora_get_status(false,&app_lora_status); 
                    switch(app_lora_status.autosend_status)
                    {
                        case RUI_AUTO_ENABLE_SLEEP:rui_lora_set_send_interval(RUI_AUTO_ENABLE_SLEEP,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                            break;
                        case RUI_AUTO_ENABLE_NORMAL:rui_lora_set_send_interval(RUI_AUTO_ENABLE_NORMAL,app_lora_status.lorasend_interval);  //start autosend_timer after send success
                            break;
                        default:break;
                    }
                    break;
            }            
        }
    }	
}

/*******************************************************************************************
 * LoRaMac callback functions
 * * void LoRaReceive_callback(RUI_RECEIVE_T* Receive_datapackage);//LoRaWAN callback if receive data 
 * * void LoRaP2PReceive_callback(RUI_LORAP2P_RECEIVE_T *Receive_P2Pdatapackage);//LoRaP2P callback if receive data 
 * * void LoRaWANJoined_callback(uint32_t status);//LoRaWAN callback after join server request
 * * void LoRaWANSendsucceed_callback(RUI_MCPS_T status);//LoRaWAN call back after send data complete
 * *****************************************************************************************/  
void LoRaReceive_callback(RUI_RECEIVE_T* Receive_datapackage)
{
    char hex_str[3] = {0}; 
    RUI_LOG_PRINTF("at+recv=%d,%d,%d,%d", Receive_datapackage->Port, Receive_datapackage->Rssi, Receive_datapackage->Snr, Receive_datapackage->BufferSize);   
    
    if ((Receive_datapackage->Buffer != NULL) && Receive_datapackage->BufferSize) {
        RUI_LOG_PRINTF(":");
        for (int i = 0; i < Receive_datapackage->BufferSize; i++) {
            sprintf(hex_str, "%02x", Receive_datapackage->Buffer[i]);
            RUI_LOG_PRINTF("%s", hex_str); 
        }
    }
    RUI_LOG_PRINTF("\r\n");
}
void LoRaP2PReceive_callback(RUI_LORAP2P_RECEIVE_T *Receive_P2Pdatapackage)
{
    char hex_str[3]={0};
    RUI_LOG_PRINTF("at+recv=%d,%d,%d:", Receive_P2Pdatapackage -> Rssi, Receive_P2Pdatapackage -> Snr, Receive_P2Pdatapackage -> BufferSize); 
    for(int i=0;i < Receive_P2Pdatapackage -> BufferSize; i++)
    {
        sprintf(hex_str, "%02X", Receive_P2Pdatapackage -> Buffer[i]);
        RUI_LOG_PRINTF("%s",hex_str);
    }
    RUI_LOG_PRINTF("\r\n");    
}
void LoRaWANJoined_callback(uint32_t status)
{
    static int8_t dr; 
    if(status)  //Join Success
    {
        JoinCnt = 0;
        IsJoiningflag = false;
        RUI_LOG_PRINTF("[LoRa]:Join Success\r\nOK\r\n");
        rui_gpio_rw(RUI_IF_WRITE,&Led_Green, low);
        rui_timer_start(&Led_Green_Timer);        
    }else 
    {        
        if(JoinCnt<JOIN_MAX_CNT) // Join was not successful. Try to join again
        {
            JoinCnt++;
            RUI_LOG_PRINTF("[LoRa]:Join retry Cnt:%d\n",JoinCnt);
            rui_lora_get_status(false,&app_lora_status);
            if(app_lora_status.lora_dr > 0)
            {
                app_lora_status.lora_dr -= 1;
            }else app_lora_status.lora_dr = 0;
            rui_lora_set_dr(app_lora_status.lora_dr);
            rui_lora_join();                    
        }
        else   //Join failed
        {
            RUI_LOG_PRINTF("ERROR: RUI_AT_LORA_INFO_STATUS_JOIN_FAIL %d\r\n",RUI_AT_LORA_INFO_STATUS_JOIN_FAIL); 
			rui_lora_get_status(false,&app_lora_status);  //The query gets the current status 
			rui_lora_set_send_interval(RUI_AUTO_ENABLE_SLEEP,app_lora_status.lorasend_interval);  //start autosend_timer after send success
            JoinCnt=0;   
        }          
    }    
}
void LoRaWANSendsucceed_callback(RUI_MCPS_T status)
{
    switch( status )
    {
        case RUI_MCPS_UNCONFIRMED:
        {
            RUI_LOG_PRINTF("[LoRa]: RUI_MCPS_UNCONFIRMED send success\r\nOK\r\n");
            break;
        }
        case RUI_MCPS_CONFIRMED:
        {
            RUI_LOG_PRINTF("[LoRa]: RUI_MCPS_CONFIRMED send success\r\nOK\r\n");
            break;
        }
        case RUI_MCPS_PROPRIETARY:
        {
            RUI_LOG_PRINTF("[LoRa]: RUI_MCPS_PROPRIETARY send success\r\nOK\r\n");
            break;
        }
        case RUI_MCPS_MULTICAST:
        {
            RUI_LOG_PRINTF("[LoRa]: RUI_MCPS_MULTICAST send success\r\nOK\r\n");
            break;           
        }
        default:             
            break;
    }       
	
    rui_gpio_rw(RUI_IF_WRITE,&Led_Blue, low);
    rui_timer_start(&Led_Blue_Timer); 

}

/*******************************************************************************************
 * The RUI is used to receive data from uart.
 * 
 * *****************************************************************************************/ 
void rui_uart_recv(RUI_UART_DEF uart_def, uint8_t *pdata, uint16_t len)
{
    switch(uart_def)
    {
        case RUI_UART1://process code if RUI_UART1 work at RUI_UART_UNVARNISHED
            rui_lora_get_status(false,&app_lora_status);
            if(app_lora_status.IsJoined)  //if LoRaWAN is joined
            {
                rui_lora_send(8,pdata,len);
            }else
            {
                RUI_LOG_PRINTF("ERROR: RUI_AT_LORA_NO_NETWORK_JOINED %d",RUI_AT_LORA_NO_NETWORK_JOINED);
            }
             
            break;
        case RUI_UART3:
            break;
        default:break;
    }
}

/*******************************************************************************************
 * the app_main function
 * *****************************************************************************************/ 
void main(void)
{
    rui_init();
    bsp_init();
    
    /*******************************************************************************************
     * Register LoRaMac callback function
     * 
     * *****************************************************************************************/
    rui_lora_register_recv_callback(LoRaReceive_callback);  
    rui_lorap2p_register_recv_callback(LoRaP2PReceive_callback);
    rui_lorajoin_register_callback(LoRaWANJoined_callback); 
    rui_lorasend_complete_register_callback(LoRaWANSendsucceed_callback); 

    /*******************************************************************************************    
     *The query gets the current status 
    * 
    * *****************************************************************************************/ 
    rui_lora_get_status(false,&app_lora_status);

	if(app_lora_status.autosend_status)RUI_LOG_PRINTF("autosend_interval: %us\r\n", app_lora_status.lorasend_interval);

    /*******************************************************************************************    
     *Init OK ,print board status and auto join LoRaWAN
    * 
    * *****************************************************************************************/  
    switch(app_lora_status.work_mode)
	{
		case RUI_LORAWAN:
            RUI_LOG_PRINTF("Initialization OK,Current work_mode:LoRaWAN,");
            if(app_lora_status.join_mode == RUI_OTAA)
            {
                RUI_LOG_PRINTF(" join_mode:OTAA,");  
                switch(app_lora_status.class_status)
                {
                    case RUI_CLASS_A:RUI_LOG_PRINTF(" Class: A\r\n");
                        break;
                    case RUI_CLASS_B:RUI_LOG_PRINTF(" Class: B\r\n");
                        break;
                    case RUI_CLASS_C:RUI_LOG_PRINTF(" Class: C\r\n");
                        break;
                    default:break;
                }              
            }else if(app_lora_status.join_mode == RUI_ABP)
            {
                RUI_LOG_PRINTF(" join_mode:ABP,\r\n");
                switch(app_lora_status.class_status)
                {
                    case RUI_CLASS_A:RUI_LOG_PRINTF(" Class: A\r\n");
                        break;
                    case RUI_CLASS_B:RUI_LOG_PRINTF(" Class: B\r\n");
                        break;
                    case RUI_CLASS_C:RUI_LOG_PRINTF(" Class: C\r\n");
                        break;
                    default:break;
                } 
                if(rui_lora_join() == RUI_STATUS_OK)//join LoRaWAN by ABP mode
                {
                    LoRaWANJoined_callback(1);  //ABP mode join success
                }
            }
			break;
		case RUI_P2P:RUI_LOG_PRINTF("Current work_mode:P2P\r\n");
			break;
		default: break;
	}   
    RUI_LOG_PRINTF("\r\n");
    while(1)
    {       
        rui_lora_get_status(false,&app_lora_status);//The query gets the current status 
        rui_running();
        switch(app_lora_status.work_mode)
        {
            case RUI_LORAWAN:
                app_loop();				
                break;
            case RUI_P2P:
                /*************************************************************************************
                 * user code at LoRa P2P mode
                *************************************************************************************/
                break;
            default :break;
        }
    }
}