import time
import sys
print(sys.flags.optimize)
def factorial(x):
    if x == 1:
        return 1
    return x * factorial(x - 1)
    
# Number of repetitions to get a measurable time
num_repetitions = 10000

start_time = time.time_ns()

for _ in range(num_repetitions):
    x = factorial(5)
    x = factorial(5) + factorial(5) + factorial(5)

end_time = time.time_ns()
print(start_time)
print(end_time)

execution_time = (end_time - start_time) / num_repetitions
print("Result:", x)
print("Execution time:", execution_time, "nanoseconds")
