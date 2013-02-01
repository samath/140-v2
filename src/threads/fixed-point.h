#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

typedef int fixed_point;

#define FIXED_POINT 16384

static fixed_point fp_int_to_fixed_point (int);
static int fp_fixed_point_to_int (fixed_point);
static int fp_fixed_point_to_int_rounded (fixed_point);
static fixed_point fp_add (fixed_point, fixed_point);
static fixed_point fp_subtract (fixed_point, fixed_point);
static fixed_point fp_add_int (fixed_point, int);
static fixed_point fp_subtract_int (fixed_point, int);
static fixed_point fp_multiply (fixed_point, fixed_point);
static fixed_point fp_multiply_int (fixed_point, int);
static fixed_point fp_divide (fixed_point, fixed_point);
static fixed_point fp_divide_int (fixed_point, int);

// Conversion
static inline fixed_point fp_int_to_fixed_point (int n) {
  return n * FIXED_POINT;
}
static inline int fp_fixed_point_to_int (fixed_point n) {
  return n / FIXED_POINT;
}
static inline int fp_fixed_point_to_int_rounded (fixed_point n) {
  return (n > 0) ? (n + FIXED_POINT / 2) / FIXED_POINT 
                 : (n - FIXED_POINT / 2) / FIXED_POINT;
}

// Addition
static inline fixed_point fp_add (fixed_point n, fixed_point m) { return n + m; }
static inline fixed_point fp_add_int (fixed_point n, int m) { 
  return n + m * FIXED_POINT;
}

// Subtraction
static inline fixed_point fp_subtract (fixed_point n, fixed_point m) { return n - m; }
static inline fixed_point fp_subtract_int (fixed_point n, int m) {
  return n - m * FIXED_POINT;
}

// Multiplication
static inline fixed_point fp_multiply (fixed_point n, fixed_point m) {
  return ((int64_t) n) * m / FIXED_POINT;
}
static inline fixed_point fp_multiply_int (fixed_point n, int m) { return n * m; }

// Division
static inline fixed_point fp_divide (fixed_point n, fixed_point m) {
  return ((int64_t) n) * FIXED_POINT / m;
}
static inline fixed_point fp_divide_int (fixed_point n, int m) {
  return ((int64_t) n) / m;
}

#endif
