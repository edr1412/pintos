#ifndef __LIB_FIX_H
#define __LIB_FIX_H
/*为了实现一定范围内的实数运算，用32 bit 的 int 类型模拟实数，将前 16 记录一个实数的整数部分，后 16 bit 记录一个实数的小数部分。称此类型为 fix。 */
typedef int fix;

#define SHIFT_BIT 16

/* int to fix */
#define I_TO_F(A) ((fix)((A) << SHIFT_BIT))


/* fix + int = fix */
#define F_ADD_I(A, B) ((A) + I_TO_F(B))


/* fix * int = fix */
#define F_MULT_I(A, B) ((A) * (B))

/* fix / int = fix */
#define F_DIV_I(A, B) ((A) / (B))

/* fix * fix = fix */
#define F_MULT_F(A, B) ((fix)((((int64_t) (A)) * (int64_t) (B)) >> SHIFT_BIT))

/* fix / fix = fix */
#define F_DIV_F(A, B) ((fix)((((int64_t) (A)) << SHIFT_BIT) / (B)))

/* fix to int 忽略小数 */
#define F_TO_I_CUT(A) ((A) >> SHIFT_BIT)

/* fix to int 四舍五入 */
#define F_TO_I(A) ((A) >= 0 ? F_TO_I_CUT((A) + (1 << (SHIFT_BIT - 1))) \
                           : F_TO_I_CUT((A) - (1 << (SHIFT_BIT - 1))))

#endif /* lib/fix.h */