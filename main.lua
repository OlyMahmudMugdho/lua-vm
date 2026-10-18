
-- PicoLuaVM MVP test program

print("Hello from PicoLuaVM!")
print("Lua version:", _VERSION)

local total = 0

for i = 1, 10 do
    total = total + i
end

print("Sum of 1 to 10:", total)

local function fibonacci(n)
    if n <= 1 then
        return n
    end

    return fibonacci(n - 1) + fibonacci(n - 2)
end

print("Fibonacci(10):", fibonacci(10))
print("Program completed!")
