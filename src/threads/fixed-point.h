#ifndef THREADS_fixed_point_H
#define THREADS_fixed_point_H

typedef int fixed_point;

#define FIXED_POINT 16384

fixed_point fp_int_to_fixed_point (int);
int fp_fixed_point_to_int (fixed_point);
int fp_fixed_point_to_int_rounded (fixed_point);
fixed_point fp_add (fixed_point, fixed_point);
fixed_point fp_subtract (fixed_point, fixed_point);
fixed_point fp_add_int (fixed_point, int);
fixed_point fp_subtract_int (fixed_point, int);
fixed_point fp_multiply (fixed_point, fixed_point);
fixed_point fp_multiply_int (fixed_point, int);
fixed_point fp_divide (fixed_point, fixed_point);
fixed_point fp_divide_int (fixed_point, int);


fixed_point fp_int_to_fixed_point (int n) { return n * FIXED_POINT; }
int fp_fixed_point_to_int (fixed_point n) { return n / FIXED_POINT; }
int fp_fixed_point_to_int_rounded (fixed_point n) {
  return (n > 0) ? (n + FIXED_POINT / 2) / FIXED_POINT 
                 : (n - FIXED_POINT / 2) / FIXED_POINT;
}

fixed_point fp_add (fixed_point n, fixed_point m) { return n + m; }
fixed_point fp_subtract (fixed_point n, fixed_point m) { return n - m; }
fixed_point fp_add_int (fixed_point n, int m) { 
  return n + m * FIXED_POINT;
}
fixed_point fp_subtract_int (fixed_point n, int m) {
  return n - m * FIXED_POINT;
}

fixed_point fp_multiply (fixed_point n, fixed_point m) {
  return ((int64_t) n) * m / FIXED_POINT;
}
fixed_point fp_multiply_int (fixed_point n, int m) { return n * m; }

fixed_point fp_divide (fixed_point n, fixed_point m) {
  return ((int64_t) n) * FIXED_POINT / m;
}
fixed_point fp_divide_int (fixed_point n, int m) {
  return ((int64_t) n) / m;
}


#endif
