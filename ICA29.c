#include <math.h>

void square (double* x){
    *x = (*x * *x);
}

void add_ten (double* x){
    *x = *x + 10.0;
}

void square_root (double* x){
    *x = sqrt(*x);
}