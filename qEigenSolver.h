#pragma once

#include "qMatrix.h"
#include "qLinearSolver.h"
#include "qHelper.h"

template<typename T, typename S1> CUDA_FUNC_IN void eig2x2(const qMatrix<T, 2, 2, S1>& A, T& l1, T& l2)
{
	int N = 2;
	T a = A(N - 2, N - 2), b = A(N - 2, N - 1), c = A(N - 1, N - 2), d = A(N - 1, N - 1), p = -a - d, q = a * d - b * c;
	T l0 = std::sqrt(p * p / T(4) - q);
	l1 = -p / T(2) + l0;
	l2 = -p / T(2) - l0;
	if (l1 > l2)
	{
		T tmp = l1;
		l1 = l2;
		l2 = tmp;
	}
}

template<typename T, int N, typename S1, typename S2, typename S3> CUDA_FUNC_IN void hessenbergReduction(const qMatrix<T, N, N, S1>& A, qMatrix<T, N, N, S2>& H, qMatrix<T, N, N, S3>& Q)
{
	H = A;
	Q.id();
	
	for_i<0, N - 2>([&H, &Q](auto kT)
	{
		const int k = decltype(kT)::VAL;

		auto u = Clone(H.template submat<k+1,k,N-1,k>());
		u(0) = u(0) + norm(u) * (u(0) >= (T)0 ? (T)1 : (T)-1);
		auto v = u / norm(u);
		auto a1 = H.template submat<k + 1, 0, N - 1, N - 1>();
		a1 = a1 - (T)2 * v * (v.transpose() * a1);
		auto a2 = H.template submat<0, k + 1, N - 1, N - 1>();
		a2 = a2 - (a2 * ((T)2 * v)) * v.transpose();
		auto a3 = Q.template submat<0, k + 1, N - 1, N - 1>();
		a3 = a3 - (a3 * ((T)2 * v)) * v.transpose();
	});
}

namespace __eigenvalue_reoder__
{
	template<typename T, int N, typename S2, typename S3> CUDA_FUNC_IN void reorderEigenValues(int n_eig_counter, qMatrix<T, N, 1, S2>& D, qMatrix<T, N, N, S3>& V)
	{
		for (int i = 0; i < n_eig_counter - 1; i++)
		{
			int minIdx = i;
			for (int j = i + 1; j < n_eig_counter; j++)
				if (D(j) > D(minIdx))
					minIdx = j;
			if (minIdx != i)
			{
				V.swap_cols(i, minIdx);
				D.swap_rows(i, minIdx);
			}
		}
	}
}

//X has to be symmetric and of full rank, all symmetric matrices are NON defective i.e. have a full set of eigenvalues
template<typename T, int N, typename S1, typename S2, typename S3> CUDA_FUNC_IN void qrAlgorithmSymmetric(const qMatrix<T, N, N, S1>& X, qMatrix<T, N, 1, S2>& D, qMatrix<T, N, N, S3>& V, int n = 50)
{
	assert(X.is_symmetric());
	//using Wilkinson shifts
	V.id();
	qMatrix<T, N, N> X_i = X, I = qMatrix<T, N, N>::Id();
	for (int i = 0; i < n; i++)
	{
		T kappa = 0;
		if (N > 2)
		{
			T l1, l2, d = X_i(N - 1, N - 1);
			eig2x2(X_i.template submat<N - 2, N - 2, N - 1, N - 1>(), l1, l2);
			kappa = std::abs(l1 - d) < std::abs(l2 - d) ? l1 : l2;
		}

		qMatrix<T, N, N> Q_i, R_i;
		qrHousholder(X_i - kappa * I, Q_i, R_i);
		X_i = R_i * Q_i + kappa * I;
		V = V * Q_i;
	}
	D = diag<qMatrix<T, N, 1>>(X_i);

	__eigenvalue_reoder__::reorderEigenValues(N, D, V);
}

namespace __qrAlgorithm__
{
	template<typename T, int N, typename S1> CUDA_FUNC_IN qMatrix<T, N, 1> inversePowerMethod(const qMatrix<T, N, N, S1>& A, const T& lambda)
	{
		qMatrix<T, N, 1> w = solve(A - lambda * qMatrix<T, N, N>::Id(), ::e<qMatrix<T, N, 1>>(0));
		return w / w.p_norm(T(2));
	}
}

template<typename T, int N, typename S1, typename S2, typename S3> CUDA_FUNC_IN int qrAlgorithm(const qMatrix<T, N, N, S1>& X, qMatrix<T, N, 1, S2>& D, qMatrix<T, N, N, S3>& V, int n = 50)
{
	V.id();
	qMatrix<T, N, N> X_i = X;
	for (int i = 0; i < n; i++)
	{
		qMatrix<T, N, N> Q_i, R_i, P;
		qrHousholder(X_i, Q_i, R_i);
		X_i = R_i * Q_i;
		V = V * Q_i;
	}
	D = diag<qMatrix<T, N, 1>>(X_i);
	V.zero();
	int n_eig_counter = 0, j = 0;
	while (j < N && std::abs(D(j)) > T(1e-5))
	{
		auto eigVal = D(j++);
		auto eigVec = __qrAlgorithm__::inversePowerMethod(X, eigVal);
		auto diff = X * eigVec - eigVal  * eigVec;
		if (norm(diff) > T(1e-4))
		{
			D(j-1) = 0;
			V.col(j-1).zero();
		}
		else
		{
			V.col(n_eig_counter) = eigVec.num_negative_elements() > N / 2 ? -eigVec : eigVec;
			n_eig_counter++;
		}
	}

	__eigenvalue_reoder__::reorderEigenValues(n_eig_counter, D, V);

	return n_eig_counter;
}