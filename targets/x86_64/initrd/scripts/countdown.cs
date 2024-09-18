// Counts down once a second, and can be stopped early with Escape.
var count = 10;
while (count > 0)
{
    print(count, "...");
    sleep(1000);
    count = count - 1;
}
print("Lift off!");
