#include "higpio.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
int sys_write(const char *path, const char *cmd)
{
     int fd = open(path, O_WRONLY);
     if (fd < 0) {
      printf("open %s error: %s\n", path, strerror(errno));
      return -1;
     }

     int ret = write(fd, cmd, strlen(cmd));
     if (!(ret == strlen(cmd))) {
      printf("write %s error: %s\n", path, strerror(errno));
      return -1;
     }

     close(fd);
     return 0;
}

static int IRcutReverser(void)
{
    return 0;
}
static void gpio_direction_config(const int gpionum,const char *direction)
{
    char gpioNum[16] = {0};
    char gpioDir[64] = {0};

    snprintf(gpioNum,sizeof(gpioNum),"%d",gpionum);
    sys_write("/sys/class/gpio/export", gpioNum);

    snprintf(gpioDir,sizeof(gpioDir),"/sys/class/gpio/gpio%d/direction",gpionum);
    sys_write(gpioDir, direction);

}

void gpio_pull_up(int gpionum)
{
    char gpioValue[64] = {0};
    snprintf(gpioValue,sizeof(gpioValue),"/sys/class/gpio/gpio%d/value",gpionum);
    sys_write(gpioValue, "1");
}

void gpio_pull_down(int gpionum)
{
    char gpioValue[64] = {0};
    snprintf(gpioValue,sizeof(gpioValue),"/sys/class/gpio/gpio%d/value",gpionum);
    sys_write(gpioValue, "0");
}

int gpio_get_value(int gpionum)
{
    char path[64];
    char value_str[3];
    int fd;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpionum);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open gpio value for reading!");
        return -1;
    }

    if (read(fd, value_str, 3) < 0) {
        perror("Failed to read value!");
        return -1;
    }

    close(fd);
    return (atoi(value_str));
}

void gpio_AudioDev(int ob)
{
        if(ob)
             gpio_pull_up(GPIO_SPK);
        else
             gpio_pull_down(GPIO_SPK);

}

int get_gpio_reset_value()
{
     return gpio_get_value(GPIO_RESET);
}

void gpio_ircut_ctr1(int is_enable)
{
    if(is_enable){
        gpio_pull_up(GPIO_IRCUT_CTL1);
    }else{
        gpio_pull_down(GPIO_IRCUT_CTL1);
    }
}

void gpio_ircut_ctr2(int is_enable)
{
    if(is_enable){
        gpio_pull_up(GPIO_IRCUT_CTL2);
    }else{
        gpio_pull_down(GPIO_IRCUT_CTL2);
    }
}


void   gpio_open_ircut()
{
     //int nReverse=1;
    if(IRcutReverser()){

        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(1);
        usleep(100000);
        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(0);
    }else{

       
        
        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(1);
        usleep(100000);
        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(0);
    }
}
void   gpio_close_ircut()
{
    if(IRcutReverser()){

        gpio_ircut_ctr1(1);
        gpio_ircut_ctr2(0);
        usleep(100000);
        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(0);
    }else{

        gpio_ircut_ctr1(1);
        gpio_ircut_ctr2(0);
        usleep(100000);
        gpio_ircut_ctr1(0);
        gpio_ircut_ctr2(0); 
    }
}





void    gpio_int()
{
    gpio_direction_config(GPIO_RESET,GPIO_DIR_IN);
    gpio_direction_config(GPIO_IRCUT_CTL1,GPIO_DIR_OUT);
    gpio_direction_config(GPIO_IRCUT_CTL2,GPIO_DIR_OUT);
    gpio_direction_config(GPIO_SPK,GPIO_DIR_OUT);

    gpio_pull_down(GPIO_SPK);
    gpio_pull_down(GPIO_IRCUT_CTL2);
    gpio_pull_down(GPIO_IRCUT_CTL1);
  
    //gpio_open_ircut();
    gpio_close_ircut();
}
