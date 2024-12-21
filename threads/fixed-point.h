#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#define F (1 << 14)

int integer_to_fp(int n);
int fp_to_integer(int x);
int fp_to_integer_round(int x);
int add_x_to_y(int x, int y);
int add_x_to_n(int x, int n);
int sub_y_from_x(int x, int y);
int sub_n_from_x(int x, int n);
int mul_x_by_y(int x, int y);
int mul_x_by_n(int x, int n);
int div_x_by_y(int x, int y);
int div_x_by_n(int x, int n);


int integer_to_fp(int n){
    return n * F;
}

int fp_to_integer(int x){
    return x / F;
}

int fp_to_integer_round(int x){
    if (x >= 0) return (x + F / 2) / F;
    return (x - F / 2) / F;
}

int add_x_to_y(int x, int y){
    return x + y;
}

int add_x_to_n(int x, int n){
    return x + n * F;
}

int sub_y_from_x(int x, int y){
    return x - y;
}

int sub_n_from_x(int x, int n){
    return x - n * F;
}

int mul_x_by_y(int x, int y){
    return ((int64_t) x) * y / F;
}

int mul_x_by_n(int x, int n){
    return x * n;
}

int div_x_by_y(int x, int y){
    return ((int64_t) x) * F / y;
}

int div_x_by_n(int x, int n){
    return x / n;
}