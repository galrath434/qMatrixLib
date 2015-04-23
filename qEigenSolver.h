#pragma once

#include "qMatrix.h"
#include "qLinearSolver.h"

template<typename T> CUDA_FUNC_IN void eig2x2(const qMatrix<T, 2, 2>& A, T& l1, T& l2)
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

namespace __hessenbergReduction__
{
	template<typename T, int N, int i> struct loop
	{
		CUDA_FUNC_IN static void exec(qMatrix<T, N, N>& A, qMatrix<T, N, N>& Q)
		{
			if (i < N - 2)
			{
				/*auto u = __qrHousholder__::householder(A.submat<i + 1, i, N - 1, i>());
				auto P_i = qMatrix<T, N - i - 1, N - i - 1>::Id() - T(2) * u * u.transpose();
				A.submat<i + 1, i, N - 1, N - 1>(P_i * A.submat<i + 1, i, N - 1, N - 1>());
				A.submat<0, i + 1, N - 1, N - 1>(A.submat<0, i + 1, N - 1, N - 1>() * P_i);
				Q.submat<i + 1, i + 1, N - 1, N - 1>(Q.submat<i + 1, i + 1, N - 1, N - 1>() * P_i);*/
				//auto v = A.submat<i+1,i,N-1,i>();
				//auto alpha = -norm(v);
				//if (v(1) < 0)
				//	alpha = -alpha;
				//v(1) = v(1) - alpha;
				//v = v / norm(v);
				//A.submat<i + 1, i + 1, N - 1, N - 1>(A.submat<i + 1, i + 1, N - 1, N - 1>() - T(2) * v * (v.transpose() * A.submat<i + 1, i + 1, N - 1, N - 1>()));
				//A(i + 1, i) = alpha;
				//A.submat<i + 2, i, N - 1, i>() = qMatrix<T, N - i - 2, 1>::Zero();
				//A.submat<0, i + 1, N - 1, N - 1>(A.submat<0, i + 1, N - 1, N - 1>() - T(2) * (A.submat<0, i + 1, N - 1, N - 1>() * v) * v.transpose());
				
				loop<T, N, i + 1>::exec(A, Q);
			}
		}
	};
	template<typename T, int N> struct loop<T, N, N>
	{
		CUDA_FUNC_IN static void exec(qMatrix<T, N, N>& A, qMatrix<T, N, N>& Q)
		{

		}
	};
}

template<typename T, int N> CUDA_FUNC_IN void hessenbergReduction(const qMatrix<T, N, N>& A, qMatrix<T, N, N>& H, qMatrix<T, N, N>& Q)
{
	H = A;
	Q.id();
	__hessenbergReduction__::loop<T, N, 0>::exec(H, Q);
}

//X has to be symmetric and of full rank
template<typename T, int N> CUDA_FUNC_IN void qrAlgorithmSymmetric(const qMatrix<T, N, N>& X, qMatrix<T, N, N>& D, qMatrix<T, N, N>& V, int n = 50)
{
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
	D = X_i;
}

namespace __qrAlgorithm__
{
	template<typename T, int N> CUDA_FUNC_IN qMatrix<T, N, 1> inversePowerMethod(const qMatrix<T, N, N>& A, const T& lambda)
	{
		qMatrix<T, N, 1> w = solve(A - lambda * qMatrix<T, N, N>::Id(), ::e<qMatrix<T, N, 1>>(0));
		return w / w.p_norm(T(2));
	}
}

template<typename T, int N> CUDA_FUNC_IN int qrAlgorithm(const qMatrix<T, N, N>& X, qMatrix<T, N, N>& D, qMatrix<T, N, N>& V, int n = 50)
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
	D = X_i;
	V.zero();
	int n_eig = 0;
	while (n_eig < N && std::abs(D(n_eig, n_eig)) > T(1e-5))
	{
		V.col(n_eig, __qrAlgorithm__::inversePowerMethod(X, D(n_eig, n_eig)));
		n_eig++;
	}

	for (int j = 0; j < n_eig; j++)
	{
		auto eigVec = V.col(j);
		auto eigVal = D(j, j);
		auto diff = X * eigVec - eigVal  * eigVec;
		if (diff.accuabs() > T(1e-3))
		{
			D(j, j) = 0;
			V.col(j, qMatrix<T, N, 1>::Zero());
		}
	}

	for (int n = n_eig; n > 1; n--)
		for (int i = 0; i < n - 1; i++)
			if (D(i, i) > D(i + 1, i + 1))
			{
				qMatrix_swap(D(i, i), D(i + 1, i + 1));
				V.swap_cols(i, i + 1);
			}

	return n_eig;
}
/*
template<typename T, int N> CUDA_FUNC_IN void qrAlgorithmDoubleShift(const qMatrix<T, N, N>& X, qMatrix<T, N, N>& D, qMatrix<T, N, N>& V, int n = 50)
{
V.id();
qMatrix<T, N, N> X_i = X, I = qMatrix<T, N, N>::Id();
for(int i = 0; i < n; i++)
{
T kappa1 = X_i(N - 2, N - 2) + X_i(N - 1, N - 1), kappa2 = X_i(N - 2, N - 2) * X_i(N - 1, N - 1) - X_i(N - 2, N - 1) * X_i(N - 1, N - 2);
qMatrix<T, N, N> p_A = X_i * X_i - kappa1 * X_i + kappa2 * I;
qMatrix<T, N, N> Q_i, R_i;
qrHousholder(p_A, Q_i, R_i);
X_i = Q_i.Transpose() * X_i * Q_i;
V = V * Q_i;
}
D = X_i;
}
*/