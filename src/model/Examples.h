// model/Examples.h — programas de ejemplo incluidos
#pragma once
#include <vector>
struct ExampleDef { const char* name; const char* code; };
const std::vector<ExampleDef>& examples();
