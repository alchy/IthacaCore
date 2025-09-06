cmake_minimum_required(VERSION 3.10)
project(HelloWorld C)

# Nastavení standardu jazyka C
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Přidání spustitelného souboru
add_executable(HelloWorld main.c)