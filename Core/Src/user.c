#include "user.h"
#include "motor.h"
#include "servo.h"
#include "echo.h"

//自主避障和手动操作两种模式
#define AUTOMATIC  0x01
#define MANUAL     0x02

extern float distance;		//测量获得的距离
float left_distance;		
float right_distance;
float front_distance;
extern uint8_t distance_lock;	//距离读写锁

uint8_t mode = MANUAL;		//默认模式为手动操作

// 多次测距并返回中位数（count：测距次数，建议奇数如3/5）
float Get_Median_Distance(uint8_t count)
{
    float distances[count]; // 存储多次测距结果
    uint8_t valid_cnt = 0;  // 有效数据计数
    uint8_t i, j;
    float temp;

    // 1. 多次测距，收集有效数据
    for(i = 0; i < count; i++)
    {
        Trig();
        Wait_Echo_Measure();
        while(distance_lock){}; // 等待解锁
        
        // 过滤无效值（超出量程）
        if(distance > 2.0f && distance < 400.0f)
        {
            distances[valid_cnt++] = distance;
        }
        
        HAL_Delay(60); // 两次测距间隔，避免声波干扰
    }

    // 2. 若无有效数据，返回0
    if(valid_cnt == 0) return 0.0f;

    // 3. 冒泡排序（升序）
    for(i = 0; i < valid_cnt - 1; i++)
    {
        for(j = 0; j < valid_cnt - i - 1; j++)
        {
            if(distances[j] > distances[j+1])
            {
                temp = distances[j];
                distances[j] = distances[j+1];
                distances[j+1] = temp;
            }
        }
    }

    // 4. 返回中位数（奇数个取中间，偶数个取中间两个平均）
    if(valid_cnt % 2 == 1)
    {
        return distances[valid_cnt / 2];
    }
    else
    {
        return (distances[valid_cnt/2 - 1] + distances[valid_cnt/2]) / 2.0f;
    }
}

//获取左右障碍物的距离
void Get_Left_Right_Distance()
{
    Set_Angle(30);		//测量右边
	right_distance = Get_Median_Distance(3);

	HAL_Delay(60);		//在两次测量中间添加延时，避免上一次的声波影响测量
		
    Set_Angle(150);		//测量左边
    left_distance = Get_Median_Distance(3);

    Set_Angle(90);
}

void User_Code()
{
    //先检测正前方障碍物的距离
	Set_Angle(90);
    float front_distance = Get_Median_Distance(3);
    if(front_distance > 0 && front_distance < 60)   //前方障碍物距离较近
    {
        Set_Motor(0, 0);
        Get_Left_Right_Distance();
        if(left_distance > (right_distance + 10))   //左侧障碍物距离较远，向左转
        {
            //后退
            Set_Motor(-30, -30);
            HAL_Delay(200);
            //停下
            Set_Motor(0, 0);
            HAL_Delay(200);
            //左转
            Set_Motor(-30, 30);
            HAL_Delay(100);
        }
        else                                //右侧障碍物距离较远，向右转
        {
            //后退
            Set_Motor(-30, -30);
            HAL_Delay(200);
            //停下
            Set_Motor(0, 0);
            HAL_Delay(200);
            //右转
            Set_Motor(30, -30);
            HAL_Delay(100);
        }
    }else       //前方障碍物距离较远
    {
        //前进
        Set_Motor(30, 30);
    }
}
