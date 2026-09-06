#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// $[CMU]
// [CMU]$

// $[LFXO]
// [LFXO]$

// $[KEYSCAN]
// [KEYSCAN]$

// $[PRS.ASYNCH0]
// [PRS.ASYNCH0]$

// $[PRS.ASYNCH1]
// [PRS.ASYNCH1]$

// $[PRS.ASYNCH2]
// [PRS.ASYNCH2]$

// $[PRS.ASYNCH3]
// [PRS.ASYNCH3]$

// $[PRS.ASYNCH4]
// [PRS.ASYNCH4]$

// $[PRS.ASYNCH5]
// [PRS.ASYNCH5]$

// $[PRS.ASYNCH6]
// [PRS.ASYNCH6]$

// $[PRS.ASYNCH7]
// [PRS.ASYNCH7]$

// $[PRS.ASYNCH8]
// [PRS.ASYNCH8]$

// $[PRS.ASYNCH9]
// [PRS.ASYNCH9]$

// $[PRS.ASYNCH10]
// [PRS.ASYNCH10]$

// $[PRS.ASYNCH11]
// [PRS.ASYNCH11]$

// $[PRS.ASYNCH12]
// [PRS.ASYNCH12]$

// $[PRS.ASYNCH13]
// [PRS.ASYNCH13]$

// $[PRS.ASYNCH14]
// [PRS.ASYNCH14]$

// $[PRS.ASYNCH15]
// [PRS.ASYNCH15]$

// $[PRS.SYNCH0]
// [PRS.SYNCH0]$

// $[PRS.SYNCH1]
// [PRS.SYNCH1]$

// $[PRS.SYNCH2]
// [PRS.SYNCH2]$

// $[PRS.SYNCH3]
// [PRS.SYNCH3]$

// $[GPIO]
// GPIO SWV on PA03
#ifndef GPIO_SWV_PORT                           
#define GPIO_SWV_PORT                            SL_GPIO_PORT_A
#endif
#ifndef GPIO_SWV_PIN                            
#define GPIO_SWV_PIN                             3
#endif

// [GPIO]$

// $[TIMER0]
// [TIMER0]$

// $[TIMER1]
// [TIMER1]$

// $[TIMER2]
// [TIMER2]$

// $[TIMER3]
// [TIMER3]$

// $[TIMER4]
// [TIMER4]$

// $[TIMER5]
// [TIMER5]$

// $[TIMER6]
// [TIMER6]$

// $[TIMER7]
// [TIMER7]$

// $[TIMER8]
// [TIMER8]$

// $[TIMER9]
// [TIMER9]$

// $[EUSART1]
// EUSART1 RX on PA06
#ifndef EUSART1_RX_PORT                         
#define EUSART1_RX_PORT                          SL_GPIO_PORT_A
#endif
#ifndef EUSART1_RX_PIN                          
#define EUSART1_RX_PIN                           6
#endif

// EUSART1 TX on PA05
#ifndef EUSART1_TX_PORT                         
#define EUSART1_TX_PORT                          SL_GPIO_PORT_A
#endif
#ifndef EUSART1_TX_PIN                          
#define EUSART1_TX_PIN                           5
#endif

// [EUSART1]$

// $[EUSART2]
// [EUSART2]$

// $[EUSART3]
// [EUSART3]$

// $[USART0]
// USART0 CLK on PC00
#ifndef USART0_CLK_PORT                         
#define USART0_CLK_PORT                          SL_GPIO_PORT_C
#endif
#ifndef USART0_CLK_PIN                          
#define USART0_CLK_PIN                           0
#endif

// USART0 CS on PD03
#ifndef USART0_CS_PORT                          
#define USART0_CS_PORT                           SL_GPIO_PORT_D
#endif
#ifndef USART0_CS_PIN                           
#define USART0_CS_PIN                            3
#endif

// USART0 RX on PC03
#ifndef USART0_RX_PORT                          
#define USART0_RX_PORT                           SL_GPIO_PORT_C
#endif
#ifndef USART0_RX_PIN                           
#define USART0_RX_PIN                            3
#endif

// USART0 TX on PD00
#ifndef USART0_TX_PORT                          
#define USART0_TX_PORT                           SL_GPIO_PORT_D
#endif
#ifndef USART0_TX_PIN                           
#define USART0_TX_PIN                            0
#endif

// [USART0]$

// $[USART1]
// [USART1]$

// $[USART2]
// [USART2]$

// $[I2C1]
// [I2C1]$

// $[I2C2]
// [I2C2]$

// $[I2C3]
// [I2C3]$

// $[LCD]
// [LCD]$

// $[LETIMER0]
// [LETIMER0]$

// $[IADC0]
// [IADC0]$

// $[ACMP0]
// [ACMP0]$

// $[ACMP1]
// [ACMP1]$

// $[VDAC0]
// [VDAC0]$

// $[VDAC1]
// [VDAC1]$

// $[PCNT0]
// [PCNT0]$

// $[I2C0]
// I2C0 SCL on PC07
#ifndef I2C0_SCL_PORT                           
#define I2C0_SCL_PORT                            SL_GPIO_PORT_C
#endif
#ifndef I2C0_SCL_PIN                            
#define I2C0_SCL_PIN                             7
#endif

// I2C0 SDA on PC06
#ifndef I2C0_SDA_PORT                           
#define I2C0_SDA_PORT                            SL_GPIO_PORT_C
#endif
#ifndef I2C0_SDA_PIN                            
#define I2C0_SDA_PIN                             6
#endif

// [I2C0]$

// $[EUSART0]
// [EUSART0]$

// $[PTI]
// PTI DFRAME on PC05
#ifndef PTI_DFRAME_PORT                         
#define PTI_DFRAME_PORT                          SL_GPIO_PORT_C
#endif
#ifndef PTI_DFRAME_PIN                          
#define PTI_DFRAME_PIN                           5
#endif

// PTI DOUT on PC04
#ifndef PTI_DOUT_PORT                           
#define PTI_DOUT_PORT                            SL_GPIO_PORT_C
#endif
#ifndef PTI_DOUT_PIN                            
#define PTI_DOUT_PIN                             4
#endif

// [PTI]$

// $[MODEM]
// [MODEM]$

// $[CUSTOM_PIN_NAME]
#ifndef _PORT                                   
#define _PORT                                    SL_GPIO_PORT_A
#endif
#ifndef _PIN                                    
#define _PIN                                     0
#endif
































// [CUSTOM_PIN_NAME]$


#endif // PIN_CONFIG_H


