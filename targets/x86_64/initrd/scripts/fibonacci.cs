// Prints the first Fibonacci numbers using a recursive function.
func fibonacci(n)
{
    if (n < 2) { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

var i = 0;
while (i < 15)
{
    print("fibonacci(", i, ") = ", fibonacci(i));
    i = i + 1;
}
