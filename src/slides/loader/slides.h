#pragma once

#include <functional>
#include <vector>

std::vector<std::function<std::function<void()>()>> get_slide_loaders();
