int fib(int n)
{
    if (n <= 1)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main()
{
    // The result will be set in the register x8.
    return fib(5);
}
