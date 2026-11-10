#ifndef higpio_h
#define higpio_h

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */

#define GPIO_DIR_OUT 	"out"
#define GPIO_DIR_IN 	"in"

//gpio1_4 =1*8 + 4
#define GPIO_SPK       12 
//GPIO7_3 = 7*8+3      59
#define GPIO_RESET     59
//GPIO3_4 = 3*8 +4     28
#define GPIO_TF_POWER  28
//GPIO0_1=0*8+1=1
#define GPIO_IRCUT_CTL1  1
//GPIO1_2=1*8+0
#define GPIO_IRCUT_CTL2  8
void    gpio_pull_up(int gpionum);
void    gpio_pull_down(int gpionum);
int     gpio_get_value(int gpionum);
int     get_gpio_reset_value();
void    gpio_open_ircut();
void    gpio_close_ircut();
void    gpio_int();
void    gpio_AudioDev(int ob);
int     get_adc_value();
 void  adc_check_turn_colorgrey();
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif
