#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
runtime=$1
gpu=$2
scale_dir=$3

if  [ -z "$gpu" ] || [ -z "$scale_dir" ]; then
    echo "usage: build-scale.sh <gpu arch> <scale-dir>"
    exit 1
fi

mkdir -p build

case $runtime in
    hip)
        echo "Building for HIP"
        cmake \
            -S $script_dir \
            -B build/hip \
            -GNinja \
            -DCMAKE_CXX_COMPILER=/opt/rocm/bin/hipcc \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_BENCHMARK=ON \
            -DAMDGPU_TARGETS=$gpu
        ninja -C build/hip benchmark_device_radix_sort benchmark_device_scan benchmark_device_reduce benchmark_device_merge_sort

        echo "Running benchmark - radix sort"
        build/hip/benchmark/benchmark_device_radix_sort --benchmark_out=hip_radix_sort_$gpu.json

        echo "Running benchmark - merge sort"
        build/hip/benchmark/benchmark_device_merge_sort --benchmark_out=hip_merge_sort_$gpu.json

        echo "Running benchmark - reduce"
        build/hip/benchmark/benchmark_device_reduce --benchmark_out=hip_reduce_$gpu.json

        echo "Running benchmark - scane"
        build/hip/benchmark/benchmark_device_scan --benchmark_out=hip_scan_$gpu.json
        ;;
    scale)
        echo "Building for SCALE"
        export LD_LIBRARY_PATH="$scale_dir/lib:${LD_LIBRARY_PATH:=}"
        export PATH="$scale_dir/bin:$scale_dir/targets/$gpu/bin:$PATH"
        export HIP_COMPILER=nvcc
        export HIP_PLATFORM=nvidia
        export HIP_RUNTIME=cuda
        cmake \
            -S $script_dir \
            -B build/scale \
            -GNinja \
            -DCMAKE_CXX_COMPILER=g++ \
            -DCMAKE_CUDA_COMPILER="$scale_dir/targets/$gpu/bin/nvcc" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_BENCHMARK=ON \
            -DNVGPU_TARGETS=86 \
            -DCMAKE_CUDA_ARCHITECTURES=86
        ninja -C build/scale benchmark_device_radix_sort benchmark_device_scan benchmark_device_reduce benchmark_device_merge_sort

        echo "Running benchmark - radix sort"
        build/scale/benchmark/benchmark_device_radix_sort --benchmark_out=scale_radix_sort_$gpu.json

        echo "Running benchmark - merge sort"
        build/scale/benchmark/benchmark_device_merge_sort --benchmark_out=scale_merge_sort_$gpu.json

        echo "Running benchmark - reduce"
        build/scale/benchmark/benchmark_device_reduce --benchmark_out=scale_reduce_$gpu.json

        echo "Running benchmark - scane"
        build/scale/benchmark/benchmark_device_scan --benchmark_out=scale_scan_$gpu.json
        ;;
    *)
        echo "invalid runtime"
        ;;
esac
