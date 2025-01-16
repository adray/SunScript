Simple, embeddable and lightweight scripting virtual machine.

Simply, copy the source files into your project.

* Sun.cpp/h - Compiler front-end
* SunScript.cpp/h - Virtual machine
* SunJIT.cpp/h - JIT compiler

Within SunScriptDemo.cpp find the Demo methods for example usage.

<h2>Introduction</h2>
SunScript is a dynamically typed scripting language designed to be embedded within applications. It is designed to be interpreted with an optional tracing JIT compilier.

<h2>Loops</h2>

```
var y = 0;
for (var i = 0; i < 5; i++)
{
    y += i;
}

while (y > 0)
{
    y--;
}
```

<h2>Functions</h2>

```
function Factorial(x)
{
    if (x == 1) { return 1; }
    return x * Factorial(x - 1);
}
```

<h2>Arrays</h2>

```
var arr = [];
for (var i = 0; i < 10; i++)
{
    arr[i] = i;
}
```

<h2>Classes</h2>

```
class MyClass
{
    MyClass()
    {
        self.x = 0.0;
    }

    function Increment()
    {
        self.x += 1.0;
    }
}

var c = new MyClass;
c.Increment();
```

<h2>Coroutines</h2>

```
yield Foo();
```
