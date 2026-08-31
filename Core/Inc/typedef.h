#ifndef _TYPEDEF_H__
#define _TYPEDEF_H__
#include <stdint.h>

typedef unsigned char   		u8, BOOL;
typedef char            		s8;
typedef unsigned short  		u16;
typedef signed short    		s16;
typedef unsigned int    		u32;
typedef signed int      		s32;
typedef unsigned long long 		u64;
typedef u32						FOURCC;
typedef long long               s64;

/* 取第 n 位的掩码。用 1UL 而非 1，避免 n >= 31 时的有符号移位溢出 */
#define BIT(n)              (1UL << (n))

/* 数组元素个数。仅可用于真正的数组，传指针会得到错误结果 */
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))




#endif // !_TYPEDEF_H__
