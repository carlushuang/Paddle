//   Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "paddle/fluid/operators/math/math_function.h"
#include "paddle/fluid/platform/dynload/hipblas.h"
//#include "paddle/fluid/platform/dynload/cublas.h"
#include "paddle/fluid/platform/gpu_info.h"

DECLARE_bool(enable_cublas_tensor_op_math);

namespace paddle {
namespace operators {
namespace math {

template <typename T>
struct CUBlas;

template <>
struct CUBlas<float> {
  template <typename... ARGS>
  static void GEMM(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasSgemm(args...));
  }

  template <typename... ARGS>
  static void AXPY(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasSaxpy(args...));
  }

  template <typename... ARGS>
  static void GEMV(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasSgemv(args...));
  }

  template <typename... ARGS>
  static void GEMM_STRIDED_BATCH(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasSgemmStridedBatched(args...));
  }

  // NOTES: GEMM_EX can use Tensor Core to accelerate matrix multiply.
  // https://docs.nvidia.com/cuda/cublas/index.html#cublassetmathmode
  template <typename... ARGS>
  static void GEMM_EX(platform::CUDADeviceContext *dev_ctx,
                      hipblasOperation_t transa, hipblasOperation_t transb, int m,
                      int n, int k, const float *alpha, const void *A,
                      miopenDataType_t Atype, int lda, const void *B,
                      miopenDataType_t Btype, int ldb, const float *beta, void *C,
                      miopenDataType_t Ctype, int ldc) {
// Because the gcc 4.8 doesn't expand template parameter pack that
// appears in a lambda-expression, I can not use template parameter pack
// here.
#if CUDA_VERSION >= 8000
    VLOG(5) << "use_tensor_op_math: "
            << (dev_ctx->tensor_core_available() ? "True" : "False");
    dev_ctx->TensorCoreCublasCallIfAvailable([&](cublasHandle_t handle) {
      PADDLE_ENFORCE(platform::dynload::cublasSgemmEx(
          handle, transa, transb, m, n, k, alpha, A, Atype, lda, B, Btype, ldb,
          beta, C, Ctype, ldc));
    });
#else
    PADDLE_THROW("cublasSgemmEx is supported on cuda >= 8.0");
#endif
  }
};

template <>
struct CUBlas<double> {
  template <typename... ARGS>
  static void GEMM(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasDgemm(args...));
  }

  template <typename... ARGS>
  static void AXPY(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasDaxpy(args...));
  }

  template <typename... ARGS>
  static void GEMV(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasDgemv(args...));
  }

  template <typename... ARGS>
  static void GEMM_STRIDED_BATCH(ARGS... args) {
    PADDLE_ENFORCE(platform::dynload::hipblasDgemmStridedBatched(args...));
  }

  template <typename... ARGS>
  static void GEMM_EX(ARGS... args) {
    PADDLE_THROW("Currently there are not cublasDgemmEx.");
  }
};

template <>
struct CUBlas<platform::float16> {
  using float16 = platform::float16;

  static void GEMM(hipblasHandle_t handle, hipblasOperation_t transa,
                   hipblasOperation_t transb, int m, int n, int k,
                   const float16 *alpha, const float16 *A, int lda,
                   const float16 *B, int ldb, const float16 *beta, float16 *C,
                   int ldc) {
#ifdef PADDLE_WITH_CUDA
    PADDLE_ENFORCE(
        platform::dynload::cublasHgemm(handle, transa, transb, m, n, k,
                                       reinterpret_cast<const __half *>(alpha),
                                       reinterpret_cast<const __half *>(A), lda,
                                       reinterpret_cast<const __half *>(B), ldb,
                                       reinterpret_cast<const __half *>(beta),
                                       reinterpret_cast<__half *>(C), ldc));
#else
    PADDLE_ENFORCE(
        platform::dynload::hipblasHgemm(handle, transa, transb, m, n, k,
                                       reinterpret_cast<const hipblasHalf *>(alpha),
                                       reinterpret_cast<const hipblasHalf *>(A), lda,
                                       reinterpret_cast<const hipblasHalf *>(B), ldb,
                                       reinterpret_cast<const hipblasHalf *>(beta),
                                       reinterpret_cast<hipblasHalf *>(C), ldc));
#endif
  }

  static void GEMM_STRIDED_BATCH(hipblasHandle_t handle,
                                 hipblasOperation_t transa,
                                 hipblasOperation_t transb, int m, int n, int k,
                                 const float16 *alpha, const float16 *A,
                                 int lda, long long int strideA,  // NOLINT
                                 const float16 *B,                // NOLINT
                                 int ldb, long long int strideB,  // NOLINT
                                 const float16 *beta, float16 *C, int ldc,
                                 long long int strideC,  // NOLINT
                                 int batchCount) {
#ifdef PADDLE_WITH_CUDA
#if CUDA_VERSION >= 8000
    PADDLE_ENFORCE(platform::dynload::cublasHgemmStridedBatched(
        handle, transa, transb, m, n, k,
        reinterpret_cast<const __half *>(alpha),
        reinterpret_cast<const __half *>(A), lda, strideA,
        reinterpret_cast<const __half *>(B), ldb, strideB,
        reinterpret_cast<const __half *>(beta), reinterpret_cast<__half *>(C),
        ldc, strideC, batchCount));
#else
    PADDLE_THROW("HgemmStridedBatched is not supported on cuda <= 7.5");
#endif
#else // PADDLE_WITH_CUDA
    PADDLE_ENFORCE(platform::dynload::hipblasHgemmStridedBatched(
        handle, transa, transb, m, n, k,
        reinterpret_cast<const hipblasHalf *>(alpha),
        reinterpret_cast<const hipblasHalf *>(A), lda, strideA,
        reinterpret_cast<const hipblasHalf *>(B), ldb, strideB,
        reinterpret_cast<const hipblasHalf *>(beta), reinterpret_cast<hipblasHalf *>(C),
        ldc, strideC, batchCount));
#endif
  }

  // NOTES: GEMM_EX can use Tensor Core to accelerate matrix multiply.
  // https://docs.nvidia.com/cuda/cublas/index.html#cublassetmathmode
  template <typename... ARGS>
  static void GEMM_EX(platform::CUDADeviceContext *dev_ctx,
                      hipblasOperation_t transa, hipblasOperation_t transb, int m,
                      int n, int k, const void *alpha, const void *A,
                      hipblasDatatype_t Atype, int lda, const void *B,
                      hipblasDatatype_t Btype, int ldb, const void *beta, void *C,
                      hipblasDatatype_t Ctype, int ldc,
                      hipblasDatatype_t computeType) {
#ifdef PADDLE_WITH_CUDA
#if CUDA_VERSION >= 8000
    cublasGemmAlgo_t algo = CUBLAS_GEMM_DFALT;
#if CUDA_VERSION >= 9000
    bool use_tensor_op_math = dev_ctx->tensor_core_available();
    if (use_tensor_op_math) {
      algo = CUBLAS_GEMM_DFALT_TENSOR_OP;
    }
    VLOG(5) << "use_tensor_op_math: "
            << (use_tensor_op_math ? "True" : "False");
#endif  // CUDA_VERSION >= 9000

    dev_ctx->TensorCoreCublasCallIfAvailable([&](cublasHandle_t handle) {
      PADDLE_ENFORCE(platform::dynload::cublasGemmEx(
          handle, transa, transb, m, n, k, alpha, A, Atype, lda, B, Btype, ldb,
          beta, C, Ctype, ldc, computeType, algo));
    });
#else
    PADDLE_THROW("cublasGemmEx is supported on cuda >= 8.0");
#endif
#else
    hipblasGemmAlgo_t algo = HIPBLAS_GEMM_DEFAULT;
    dev_ctx->HipblasCall([&](hipblasHandle_t handle) {
      PADDLE_ENFORCE(platform::dynload::hipblasGemmEx(
          handle, transa, transb, m, n, k, alpha, A, Atype, lda, B, Btype, ldb,
          beta, C, Ctype, ldc, computeType, algo));
    });
#endif
  }
};

template <>
template <typename T>
void Blas<platform::CUDADeviceContext>::GEMM(CBLAS_TRANSPOSE transA,
                                             CBLAS_TRANSPOSE transB, int M,
                                             int N, int K, T alpha, const T *A,
                                             const T *B, T beta, T *C) const {
  // Note that hipblas follows fortran order, so the order is different from
  // the cblas convention.
  int lda = (transA == CblasNoTrans) ? K : M;
  int ldb = (transB == CblasNoTrans) ? N : K;
  hipblasOperation_t cuTransA =
      (transA == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;
  hipblasOperation_t cuTransB =
      (transB == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;

#if CUDA_VERSION >= 8000
  if (FLAGS_enable_cublas_tensor_op_math && std::is_same<T, float>::value) {
    auto &cuda_ctx = const_cast<platform::CUDADeviceContext &>(context_);
    CUBlas<T>::GEMM_EX(&cuda_ctx, cuTransB, cuTransA, N, M, K, &alpha, B,
                       CUDA_R_32F, ldb, A, CUDA_R_32F, lda, &beta, C,
                       CUDA_R_32F, N);
  } else {
#endif  // CUDA_VERSION >= 8000
#ifdef PADDLE_WITH_HIP
    context_.HipblasCall([&](hipblasHandle_t handle) {
      CUBlas<T>::GEMM(handle, cuTransB, cuTransA, N, M, K, &alpha, B, ldb, A,
                      lda, &beta, C, N);
    });
#else
    context_.CublasCall([&](cublasHandle_t handle) {
      CUBlas<T>::GEMM(handle, cuTransB, cuTransA, N, M, K, &alpha, B, ldb, A,
                      lda, &beta, C, N);
    });
#endif

#if CUDA_VERSION >= 8000
  }
#endif  // CUDA_VERSION >= 8000
}

#if 0
template <>
template <>
inline void Blas<platform::CUDADeviceContext>::GEMM(
    CBLAS_TRANSPOSE transA, CBLAS_TRANSPOSE transB, int M, int N, int K,
    platform::float16 alpha, const platform::float16 *A,
    const platform::float16 *B, platform::float16 beta,
    platform::float16 *C) const {
  // Note that hipblas follows fortran order, so the order is different from
  // the cblas convention.
  int lda = (transA == CblasNoTrans) ? K : M;
  int ldb = (transB == CblasNoTrans) ? N : K;
  hipblasOperation_t cuTransA =
      (transA == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;
  hipblasOperation_t cuTransB =
      (transB == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;

  // TODO(kexinzhao): add processing code for compute capability < 53 case
  PADDLE_ENFORCE_GE(context_.GetComputeCapability(), 53,
                    "cublas fp16 gemm requires GPU compute capability >= 53");

  float h_alpha = static_cast<float>(alpha);
  float h_beta = static_cast<float>(beta);

#if CUDA_VERSION >= 8000
  // cublasHgemm does true FP16 computation which is slow for non-Volta
  // GPUs. So use cublasGemmEx instead which does pesudo FP16 computation:
  // input/output in fp16, computation in fp32, which can also be accelerated
  // using tensor cores in volta GPUs.
  auto &cuda_ctx = const_cast<platform::CUDADeviceContext &>(context_);
  CUBlas<platform::float16>::GEMM_EX(
      &cuda_ctx, cuTransB, cuTransA, N, M, K, &h_alpha, B, CUDA_R_16F, ldb, A,
      CUDA_R_16F, lda, &h_beta, C, CUDA_R_16F, N, CUDA_R_32F);
#else
  // CUDA 7.5 does not support cublasGemmEx, hence we fall back to use hgemm

  context_.CublasCall([&](cublasHandle_t handle) {
    CUBlas<platform::float16>::GEMM(handle, cuTransB, cuTransA, N, M, K,
                                    &h_alpha, h_B, ldb, h_A, lda, &h_beta, h_C,
                                    N);
  });
#endif  // CUDA_VERSION >= 8000
}
#endif

template <>
template <typename T>
void Blas<platform::CUDADeviceContext>::GEMM(bool transA, bool transB, int M,
                                             int N, int K, T alpha, const T *A,
                                             int lda, const T *B, int ldb,
                                             T beta, T *C, int ldc) const {
  // Note that hipblas follows fortran order, so the order is different from
  // the cblas convention.
  hipblasOperation_t cuTransA = transA ? HIPBLAS_OP_T : HIPBLAS_OP_N;
  hipblasOperation_t cuTransB = transB ? HIPBLAS_OP_T : HIPBLAS_OP_N;

#if CUDA_VERSION >= 8000
  if (FLAGS_enable_cublas_tensor_op_math && std::is_same<T, float>::value) {
    auto &cuda_ctx = const_cast<platform::CUDADeviceContext &>(context_);
    CUBlas<T>::GEMM_EX(&cuda_ctx, cuTransB, cuTransA, N, M, K, &alpha, B,
                       CUDA_R_32F, ldb, A, CUDA_R_32F, lda, &beta, C,
                       CUDA_R_32F, ldc);
  } else {
#endif  // CUDA_VERSION >= 8000
#ifdef PADDLE_WITH_HIP
    context_.HipblasCall([&](hipblasHandle_t handle) {
      CUBlas<T>::GEMM(handle, cuTransB, cuTransA, N, M, K, &alpha, B, ldb, A,
                      lda, &beta, C, ldc);
    });
#else
    context_.CublasCall([&](cublasHandle_t handle) {
      CUBlas<T>::GEMM(handle, cuTransB, cuTransA, N, M, K, &alpha, B, ldb, A,
                      lda, &beta, C, ldc);
    });
#endif

#if CUDA_VERSION >= 8000
  }
#endif  // CUDA_VERSION >= 8000
}

#if 0
template <>
template <>
inline void Blas<platform::CUDADeviceContext>::GEMM(
    bool transA, bool transB, int M, int N, int K, platform::float16 alpha,
    const platform::float16 *A, int lda, const platform::float16 *B, int ldb,
    platform::float16 beta, platform::float16 *C, int ldc) const {
  // Note that cublas follows fortran order, so the order is different from
  // the cblas convention.
  hipblasOperation_t cuTransA = transA ? HIPBLAS_OP_T : HIPBLAS_OP_N;
  hipblasOperation_t cuTransB = transB ? HIPBLAS_OP_T : HIPBLAS_OP_N;

  context_.CublasCall([&](cublasHandle_t handle) {
    CUBlas<platform::float16>::GEMM(handle, cuTransB, cuTransA, N, M, K, &alpha,
                                    B, ldb, A, lda, &beta, C, ldc);
  });
}
#endif

template <>
template <typename T>
void Blas<platform::CUDADeviceContext>::AXPY(int n, T alpha, const T *x,
                                             T *y) const {
  context_.HipblasCall([&](hipblasHandle_t handle) {
    CUBlas<T>::AXPY(handle, n, &alpha, x, 1, y, 1);
  });
}

template <>
template <typename T>
void Blas<platform::CUDADeviceContext>::GEMV(bool trans_a, int M, int N,
                                             T alpha, const T *A, const T *B,
                                             T beta, T *C) const {
  hipblasOperation_t cuTransA = !trans_a ? HIPBLAS_OP_T : HIPBLAS_OP_N;

  context_.HipblasCall([&](hipblasHandle_t handle) {
    CUBlas<T>::GEMV(handle, cuTransA, N, M, &alpha, A, N, B, 1, &beta, C, 1);
  });
}

template <>
template <typename T>
void Blas<platform::CUDADeviceContext>::BatchedGEMM(
    CBLAS_TRANSPOSE transA, CBLAS_TRANSPOSE transB, int M, int N, int K,
    T alpha, const T *A, const T *B, T beta, T *C, int batchCount,
    int64_t strideA, int64_t strideB) const {
  // Note that hipblas follows fortran order, so the order is different from
  // the cblas convention.
  int lda = (transA == CblasNoTrans) ? K : M;
  int ldb = (transB == CblasNoTrans) ? N : K;
  int ldc = N;
  hipblasOperation_t cuTransA =
      (transA == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;
  hipblasOperation_t cuTransB =
      (transB == CblasNoTrans) ? HIPBLAS_OP_N : HIPBLAS_OP_T;
  const int64_t strideC = M * N;

#if CUDA_VERSION >= 9010
  if (FLAGS_enable_cublas_tensor_op_math && std::is_same<T, float>::value) {
    cublasGemmAlgo_t algo = CUBLAS_GEMM_DFALT;
    bool use_tensor_op_math = context_.tensor_core_available();
    if (use_tensor_op_math) {
      algo = CUBLAS_GEMM_DFALT_TENSOR_OP;
    }
    VLOG(5) << "use_tensor_op_math: "
            << (use_tensor_op_math ? "True" : "False");

    context_.TensorCoreCublasCallIfAvailable([&](cublasHandle_t handle) {
      PADDLE_ENFORCE(platform::dynload::cublasGemmStridedBatchedEx(
          handle, cuTransB, cuTransA, N, M, K, &alpha, B, CUDA_R_32F, ldb,
          strideB, A, CUDA_R_32F, lda, strideA, &beta, C, CUDA_R_32F, ldc,
          strideC, batchCount, CUDA_R_32F, algo));
    });
  } else {
#endif  // CUDA_VERSION >= 9010

#ifdef PADDLE_WITH_HIP
    context_.HipblasCall([&](hipblasHandle_t handle) {
      CUBlas<T>::GEMM_STRIDED_BATCH(handle, cuTransB, cuTransA, N, M, K, &alpha,
                                    B, ldb, strideB, A, lda, strideA, &beta, C,
                                    ldc, strideC, batchCount);
    });
#else
    context_.CublasCall([&](cublasHandle_t handle) {
      CUBlas<T>::GEMM_STRIDED_BATCH(handle, cuTransB, cuTransA, N, M, K, &alpha,
                                    B, ldb, strideB, A, lda, strideA, &beta, C,
                                    ldc, strideC, batchCount);
    });
#endif

#if CUDA_VERSION >= 9010
  }
#endif  // CUDA_VERSION >= 9010
}

}  // namespace math
}  // namespace operators
}  // namespace paddle
