#pragma once

void OutputLog(const char* format, ...);

#ifndef ELOG
#define ELOG(x, ...) OutputLog("[File : %s, Line : %d]" x "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

