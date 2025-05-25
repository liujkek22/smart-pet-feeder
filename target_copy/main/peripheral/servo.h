#pragma once

#ifdef __cplusplus
extern "C" {  // 防止 C++ 进行名称修饰
#endif
void servo_init(void);
void servo_angle_set(int angle);

#ifdef __cplusplus
}
#endif