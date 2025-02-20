#include "lib.h"

using std::vector;
using std::abs;

void buildHermiteJacobi(int n, vector<double> *D, vector<double> *E) {
  //
  // Construct symmetric tridiagonal matrix similar to Jacobi matrix
  // for Hermite polynomials
  //
  // On exit, D contains diagonal elements of said matrix;
  // E contains subdiagonal elements.
  //
  // Need D of size n, E of size n-1
  //
  // Building matrix based on recursion relation for monic versions of Hermite
  // polynomials:
  //      p_n(x) = H_n(x) / 2^n
  //      p_n+1(x) + (B_n-x)*p_n(x) + A_n*p_n-1(x) = 0
  //      B_n = 0
  //      A_n = n/2
  //
  // Matrix similar to Jacobi (J) defined by:
  //      J_i,i = B_i-1, i = 1, ..., n
  //      J_i,i-1 = J_i-1,i = sqrt(A_i-1), i = 2, ..., n
  //

  // Build diagonal
  int i;
  for (i = 0; i < n; i++) {
    (*D)[i] = 0.;
  }

  // Build sub/super-diagonal
  for (i = 0; i < n - 1; i++) {
    (*E)[i] = sqrt((i + 1.) / 2.);
  }
}

void quadInfoGolubWelsch(int n, vector<double> &D, vector<double> &E,
                         double mu0, vector<double> *x, vector<double> *w) {
  //
  // Compute weights & nodes for Gaussian quadrature using Golub-Welsch
  // algorithm.
  //
  // First need to build symmetric tridiagonal matrix J similar to Jacobi for
  // desired orthogonal polynomial (based on recurrence relation).
  //
  // D contains the diagonal of this matrix J, and E contains the
  // sub/super-diagonal.
  //
  // This routine finds the eigenvectors & values of the given J matrix.
  //
  // The eigenvalues correspond to the nodes for the desired quadrature rule
  // (roots of the orthogonal polynomial).
  //
  // The eigenvectors can be used to compute the weights for the quadrature rule
  // via:
  //
  //      w_j = mu0 * (v_j,1)^2
  //
  // where mu0 = \int_a^b w(x) dx
  // (integral over range of integration of weight function)
  //
  // and
  //
  // v_j,1 is the first entry of the jth normalized (to unit length)
  // eigenvector.
  //
  // On exit, x (length n) contains nodes for quadrature rule, and w (length n)
  // contains weights for quadrature rule.
  //
  // Note that contents of D & E are destroyed on exit
  //

  // Setup for eigenvalue computations
  char JOBZ = 'V';  // Flag to compute both eigenvalues & vectors.
  char RANGE = 'V';
  double VL = -4.0;
  double VU = 4.0;
  int IL = 0;
  int IU = 1;
  double ABSTOL = std::sqrt(std::numeric_limits<double>::epsilon());
  int M;
  vector<double> W(n);
  
  // The number of eigenvalues between -4 and 4 follows very closely this 
  // relationship for moderately high values of n (> 2^3 = 8) and is an 
  // upper bound for smaller values. Hence It is a good candidate as upper 
  // bound to avoid allocating memory for a n*n matrix. The GH rule is now
  // computed using 30GB RAM for n = 2^20 = 1,048,576.
  unsigned int Mmax = std::ceil(std::pow(2.0, 1.8177530512018800 + 0.5022347758669726 * std::log2(n)));
  Rcpp::Rcout << "Predicted number of eigenvalues in the range [-4, +4]: " << Mmax << " (+ 100)" << std::endl;
  
  // Add 100 to be on the safe side
  Mmax += 100;
  
  vector<double> Z(Mmax * n);  // This holds the resulting eigenvectors.
  vector<double> WORK(5 * n);
  vector<int> IWORK(5 * n);
  vector<int> IFAIL(n);
  int INFO;

  // Run eigen decomposition
  F77_NAME(dstevx)(
      &JOBZ, &RANGE, &n, &D[0], &E[0], &VL, &VU, &IL, &IU, &ABSTOL, // Job flag & input matrix
      &M, &W[0], &Z[0], &n,       // Output array for eigenvectors & dim
      &WORK[0], &IWORK[0], &IFAIL[0], &INFO  // Workspace & info flag
  );
  
  Rcpp::Rcout << "   Actual number of eigenvalues in the range [-4, +4]: " << M << std::endl;

  // Setup x & w
  (*x).resize(M);
  (*w).resize(M);
  unsigned int numPoints = 0;
  for (unsigned int i = 0;i < M;++i) {
    double weightValue = mu0 * Z[i * n] * Z[i * n];
    if (weightValue < std::sqrt(std::numeric_limits<double>::epsilon()))
      continue;
    
    (*x)[numPoints] = W[i];
    (*w)[numPoints] = weightValue;
    ++numPoints;
  }
  
  Rcpp::Rcout << "          Number of GH points with significant weight: " << numPoints << std::endl;
  
  (*x).resize(numPoints);
  (*w).resize(numPoints);
}

void findPolyRoots(const vector<double> &c, int n, vector<double> *r) {
  //
  // Compute roots of polynomial with coefficients c using eigenvalue
  // decomposition of companion matrix
  //
  // Using R LAPACK interface
  //
  // Places result into r, which needs to be of dimension n-1
  // Need c of dimension n
  //
  using namespace std;

  int i;

  // Build companion matrix; column-major order for compatibility with LAPACK
  vector<double> C(n * n);
  for (i = 0; i < n * n; i++) {
    C[i] = 0.;
  }

  // Add diagonal components
  for (i = 1; i < n; i++) {
    C[i + n * (i - 1)] = 1.;
  }

  // Add coefficients
  for (i = 0; i < n; i++) {
    C[i + n * (n - 1)] = -c[i] / c[n];
  }

  // Setup for eigenvalue computation

  // Allocate vectors for real & imaginary parts of eigenvalues
  vector<double> valI(n);

  // Integers for status codes and LWORK
  int INFO, LWORK;

  // Workspace; starting as a single double
  double tmpwork;

  // Run eigenvalue computation
  char no = 'N';
  int one = 1;
  // First, get optimal workspace size
  LWORK = -1;
  F77_CALL(dgeev)(
      &no, &no,            // Don't compute eigenvectors
      &n, &C[0], &n,       // Companion matrix & dimensions; overwritten on exit
      &(*r)[0], &valI[0],  // Arrays for real & imaginary parts of eigenvalues
      NULL, &one,          // VL & LDVL; not used
      NULL, &one,          // VR & LDVR; not used
      &tmpwork,            // Workspace; will contain optimal size upon exit
      &LWORK,              // Workspace size; -1 -> get optimal size
      &INFO                // Status code
      );

  // Next, actually run eigendecomposition
  LWORK = (int)tmpwork;
  vector<double> work(LWORK);
  F77_CALL(dgeev)(
      &no, &no,            // Don't compute eigenvectors
      &n, &C[0], &n,       // Companion matrix & dimensions; overwritten on exit
      &(*r)[0], &valI[0],  // Arrays for real & imaginary parts of eigenvalues
      NULL, &one,          // VL & LDVL; not used
      NULL, &one,          // VR & LDVR; not used
      &work[0],            // Workspace; will contain optimal size upon exit
      &LWORK,              // Workspace size; -1 -> get optimal size
      &INFO                // Status code
      );
}

SEXP findPolyRoots(SEXP cR) {
  using namespace Rcpp;

  // Convert coef to Rcpp object
  NumericVector c(cR);
  int n = c.size();

  // Allocate vector for results
  NumericVector roots(n - 1);

  // Compute roots
  vector<double> r = as<vector<double> >(roots);
  findPolyRoots(as<vector<double> >(c), n - 1, &r);

  return roots;
}

void hermitePolyCoef(int n, vector<double> *c) {
  //
  // Compute coefficients of Hermite polynomial of order n
  // Need c of dimension n+1
  //
  // Uses recursion relation for efficiency

  // Allocate workspace for coefficient evaluations;
  // will use column-major ordering
  vector<long> work((n + 1) * (n + 1));
  int i, j;
  for (i = 0; i < (n + 1) * (n + 1); i++) {
    work[i] = 0.;
  }

  // Handle special cases (n<2)
  if (n == 0) {
    (*c)[0] = 1.;
    return;
  } else if (n == 1) {
    (*c)[0] = 0.;
    (*c)[1] = 2.;
    return;
  }

  // Initialize recursion
  work[0] = 1.;  // H_0(x) = 1
  work[1] = 0.;
  work[1 + 1 * (n + 1)] = 2.;  // H_1(x) = 2*x

  // Run recursion relation
  for (i = 2; i < n + 1; i++) {
    // Order 0 term updates
    work[i] = -2 * (i - 1) * work[i - 2];
    for (j = 1; j < i + 1; j++) {
      // Remainder of recursion relation
      work[i + j * (n + 1)] = 2. * work[(i - 1) + (j - 1) * (n + 1)] -
                              2. * (i - 1.) * work[(i - 2) + j * (n + 1)];
    }
  }

  // Extract double-formatted coefficients from last row
  for (j = 0; j < n + 1; j++) {
    (*c)[j] = (double)work[n + j * (n + 1)];
  }
}

SEXP hermitePolyCoef(SEXP nR) {
  using namespace Rcpp;

  // Convert coef to Rcpp object
  int n = IntegerVector(nR)[0];

  // Allocate vector for coefficients
  NumericVector coef(n + 1);

  // Compute roots
  vector<double> c = as<vector<double> >(coef);
  hermitePolyCoef(n, &c);

  return coef;
}

double hermitePoly(double x, int n) {
  //
  // Compute Hermite polynomial of order n evaluated at x efficiently via
  // recursion relation:
  //      H_n+1(x) = 2*x*H_n(x) - 2*n*H_n-1(x)
  //      H_0(x) = 1
  //      H_1(x) = 2x
  //
  int i = 0;

  // Special cases
  if (n == 0) {
    return 1.;
  } else if (n == 1) {
    return 2. * x;
  }

  // Standard recursion
  double hnm2 = 1.;
  double hnm1 = 2. * x;
  double hn = 0.;
  for (i = 2; i <= n; i++) {
    hn = 2. * x * hnm1 - 2. * (i - 1.) * hnm2;
    hnm2 = hnm1;
    hnm1 = hn;
  }

  return hn;
}

SEXP evalHermitePoly(SEXP xR, SEXP nR) {
  using namespace Rcpp;
  int i;

  // Convert to Rcpp objects
  NumericVector x(xR);
  IntegerVector n(nR);

  if (n.size() == x.size()) {
    // Iterate through x & n
    NumericVector h(x.size());
    for (i = 0; i < x.size(); i++) {
      h[i] = hermitePoly(x[i], n[i]);
    }
    return h;
  } else if (x.size() > n.size()) {
    // Iterate through x only
    NumericVector h(x.size());
    for (i = 0; i < x.size(); i++) {
      h[i] = hermitePoly(x[i], n[0]);
    }
    return h;
  } else {
    // Iterate through n only
    NumericVector h(n.size());
    for (i = 0; i < n.size(); i++) {
      h[i] = hermitePoly(x[0], n[i]);
    }
    return h;
  }
}

int gaussHermiteDataDirect(int n, vector<double> *x, vector<double> *w) {
  //
  // Calculates roots & weights of Hermite polynomials of order n for
  // Gauss-Hermite integration.
  //
  // Need x & w of size n
  //
  // Using standard formulation (no generalizations or polynomial adjustment)
  //
  // Direct evaluation and root-finding; clear, but numerically unstable
  // for n>20 or so
  //
  // Calculate coefficients of Hermite polynomial of given order
  vector<double> coef(n + 1);
  hermitePolyCoef(n, &coef);

  // Find roots of given Hermite polynomial; these are the points at
  // which the integrand will be evaluated (x)
  findPolyRoots(coef, n, x);

  // Calculate weights w
  int i;
  double log2 = log(2.0), logsqrtpi = 0.5 * log(M_PI);
  for (i = 0; i < n; i++) {
    // First, compute the log-weight
    (*w)[i] = (n - 1.) * log2 + lgamma(n + 1) + logsqrtpi -
              2. * log((double)n) - 2. * log(abs(hermitePoly((*x)[i], n - 1)));
    (*w)[i] = exp((*w)[i]);
  }

  return 0;
}

int gaussHermiteDataGolubWelsch(int n, vector<double> *x, vector<double> *w) {
  //
  // Calculates nodes & weights for Gauss-Hermite integration of order n
  //
  // Need x & w of size n
  //
  // Using standard formulation (no generalizations or polynomial adjustment)
  //
  // Evaluations use Golub-Welsch algorithm; numerically stable for n>=100
  //
  // Build Jacobi-similar symmetric tridiagonal matrix via diagonal &
  // sub-diagonal
  vector<double> D(n), E(n);
  buildHermiteJacobi(n, &D, &E);

  // Get nodes & weights
  double mu0 = sqrt(M_PI);
  quadInfoGolubWelsch(n, D, E, mu0, x, w);

  return 0;
}

SEXP gaussHermiteData(SEXP nR) {
  using namespace Rcpp;

  // Convert nR to int
  int n = IntegerVector(nR)[0];

  // Allocate vectors for x & w
  vector<double> x(n), w(n);

  // Build Gauss-Hermite integration rules
  gaussHermiteDataGolubWelsch(n, &x, &w);

  // Build list for values
  return List::create(Named("x") = x, Named("w") = w);
}
