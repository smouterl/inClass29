#include <math.h>

void square (int* x){
    *x = (*x * *x);
}

void add_ten (int* x){
    *x = *x + 10;
}

void square_root (int* x){
    *x = (int)sqrt((double)*x);
}