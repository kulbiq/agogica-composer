#pragma once

// CPU time this process has used so far, all threads (seconds). Its rate of change is how many
// cores the tracker keeps busy (1.0 = one whole core) - the ONNX Runtime pools included.
double processCpuSeconds();
