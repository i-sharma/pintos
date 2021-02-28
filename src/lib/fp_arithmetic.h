/* begin ASSGN4 */
#ifndef __LIB_FP_ARITHMETIC_H
#define __LIB_FP_ARITHMETIC_H

/*
  Define constants for fixed point arithmetic,
  using 31 bit numbers, as described in the documentation
*/

#define P 17       /* Integer Bits */
#define Q 14       /* Decimal Bits */
#define S 1        /* Sign Bit */
#define F 1 << (Q) /* Floating point */

#define FP(n) ((n) * (F))
#define ROUND_ZERO(x) ((x) / (F))
#define ROUND_CLOSEST(x) (((x) >= 0 ? ((x) + (F) / 2) / (F) : ((x) - (F) / 2) / (F)))
#define ADD(x, y) ((x) + (y))
#define SUB(x, y) ((x) - (y))

#define INT_ADD(x, n) ((x) + (n) * (F))
#define INT_SUB(n, x) ((n) * (F) - (x))
#define INT_MULTIPLY(x, n) ((x) * (n))
#define INT_DIVIDE(x, n) ((x) / (n))

#define MULTIPLY(x, y) (((int64_t)(x)) * (y) / (F))
#define DIVIDE(x, y) (((int64_t)(x)) * (F) / (y))

#endif // __LIB_FP_ARITHMETIC_H
/* end ASSGN4 */
