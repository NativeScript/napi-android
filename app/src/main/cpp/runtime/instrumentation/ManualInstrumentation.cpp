//
// Created by Panayot Cankov on 26/05/2017.
//

#include "ManualInstrumentation.h"

bool ns::instrumentation::Frame::disabled = true;
const std::chrono::system_clock::time_point ns::instrumentation::Frame::disabled_time = std::chrono::system_clock::time_point();
