#pragma once

#include <functional>
#include <vector>

std::vector<std::function<std::function<void()>()>> get_slide_loaders();

void add_cpp_slides(std::vector<std::function<std::function<void()>()>> &);
void add_cuda_slides(std::vector<std::function<std::function<void()>()>> &);
