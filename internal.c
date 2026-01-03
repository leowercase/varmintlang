#include "val.h"

Value equals(Value a, Value b)
{
  return a == b;
}

Value implies(Value a, Value b)
{
  return !a || b;
}

Value factorial(Value v)
{
  float64_t n = v;
  float64_t f = 1;
  for (int i = 0; i < n; i++) f *= i;
  return f;
}

