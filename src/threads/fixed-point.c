/* This function allows for 17.14 fixed point math by using the value f,
defined as 1 << 14. */

static int f = 1 << 14;


int fixedpoint_add_fixedpoint (int x, int y)
{
    return x + y;
}

int fixedpoint_minus_fixedpoint (int x, int y)
{
    return x - y;
}

int fixedpoint_add_int (int x, int n)
{
    return x + n * f;
}

int fixedpoint_minus_int (int x, int n)
{
    return x - n * f;
}

int fixedpoint_mul_fixedpoint (int x, int y)
{
    return ((int64_t) x) * y / f;
}

int fixedpoint_div_fixedpoint(int x, int y)
{
    return ((int64_t) x) * f / y;
}

int fixedpoint_mul_int(int x, int n)
{
    return x * n;
}
int fixedpoint_div_int(int x, int n)
{
    return x / n;
}

int int_to_fixedpoint (int n)
{
    return n * f;
}

int fixedpoint_to_int (int x)
{
    if (x >= 0)
    {
        return (x + f / 2) / f;
    }
    else
    {
        return (x - f / 2) / f;
    }
}
