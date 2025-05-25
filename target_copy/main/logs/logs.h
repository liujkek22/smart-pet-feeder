
#pragma once

#ifdef __cplusplus
extern "C" {  // 防止 C++ 进行名称修饰
#endif

void log_error_if_nonzero(const char *message, int error_code);

#ifdef __cplusplus
}
#endif
