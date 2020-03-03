#include <iostream>
using namespace std;
#include "../general/polynomial.h"
#include "basis.h"

extern "C" {
    void helfem_test() {
        cout << "HelFEM test routine" << endl;
    }

    void helfem_runstuff() {
        cout << "HelFEM running routine:" << endl;
        // Default values from atomic/main.cc:
        int primbas = 4;
        int nnodes = 15;
        int nquad = 0;
        double rmax = 40.0;
        // parser.add<int>("grid", 0, "type of grid: 1 for linear, 2 for quadratic, 3 for polynomial, 4 for logarithmic", false, 4);
        int igrid = 4;
        double zexp = 2.0;
        // Arguments without default values:
        //int Z = 1;
        int nelem = 100;
        //int lmax = 0;
        //int mmax = 0;
        // Local variables:
        helfem::polynomial_basis::PolynomialBasis * poly;
        poly = helfem::polynomial_basis::get_basis(primbas, nnodes);

        // helfem::atomic::basis::TwoDBasis basis;
        // basis = helfem::atomic::basis::TwoDBasis(Z, poly, nquad, nelem, rmax, lmax, mmax, igrid, zexp);
        helfem::atomic::basis::RadialBasis basis(poly, nquad, nelem, rmax, igrid, zexp);

        //arma::mat rinvmat = basis.radial_integral(-1, 10);
        //rinvmat.print("...");

        //arma::mat overlap = basis.overlap(10);
        arma::mat overlap = basis.radial_integral(0, 0);
        overlap.print("Overlap:");
    }
}
