import time
import sys
print(sys.flags.optimize)

# Number of repetitions to get a measurable time
num_repetitions = 10000

start_time = time.time_ns()

for _ in range(num_repetitions):
    x = 10
    hits = 0

    if x == 10:
        hits += 1

    if x == 9:
        assert False

    if x == 9:
        assert False
    else:
        hits += 1

    if x == 10 and 10 == 10:
        hits += 1

    if x == 9 and 10 == 10:
        assert False

    if x == 10 and (x == 10 or x == 9):
        hits += 1

    if x == 8 or (x == 10 and x == 10):
        hits += 1

    if x == 10 or x == 8 or x == 8:
        hits += 1

    if x == 10 and x == 10 and x == 10:
        hits += 1

    if x == 9 and (x == 10 or x == 9):
        assert False
    elif x == 10:
        hits += 1
    else:
        assert False

    if x != 10:
        assert False

    if x < 10:
        assert False

    if x > 10:
        assert False

    if x >= 10 and x <= 10:
        hits += 1

    if x > 5:
        hits += 1

    y = 8
    if y > x:
        assert False

    assert hits == 10, f"Expected hits to be 10, but got {hits}"


end_time = time.time_ns()
print(start_time)
print(end_time)

execution_time = (end_time - start_time) / num_repetitions
print("Execution time:", execution_time, "nanoseconds")
