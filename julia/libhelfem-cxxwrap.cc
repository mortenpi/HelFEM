#include <string>
#include <memory>
#include <jlcxx/jlcxx.hpp>
#include <armadillo>
#include <helfem.h>

auto helfem_basis(int nnodes, int nelem, int primbas, double rmax, int igrid, double zexp, int nquad) {
    helfem::polynomial_basis::PolynomialBasis * poly = helfem::polynomial_basis::get_basis(primbas, nnodes);
    if(nquad <= 0) nquad = 5 * poly->get_nbf();
    arma::vec grid = helfem::utils::get_grid(rmax, nelem, igrid, zexp);
    return helfem::atomic::basis::RadialBasis(poly, nquad, grid);
}

namespace jlcxx
{
  template<> struct SuperType<helfem::modelpotential::PointNucleus> { typedef helfem::modelpotential::ModelPotential type; };
  template<> struct SuperType<helfem::modelpotential::GaussianNucleus> { typedef helfem::modelpotential::ModelPotential type; };
  template<> struct SuperType<helfem::modelpotential::SphericalNucleus> { typedef helfem::modelpotential::ModelPotential type; };
  template<> struct SuperType<helfem::modelpotential::HollowNucleus> { typedef helfem::modelpotential::ModelPotential type; };
}

JLCXX_MODULE define_julia_module(jlcxx::Module& mod)
{
    mod.add_type<arma::vec>("ArmaVector")
        .constructor<arma::uword>()
        .method("at", [] (const arma::vec& m, arma::uword i) { return m(i); })
        .method("at!", [] (arma::vec& m, arma::uword i, double x) { m(i) = x; })
        .method("nrows", [] (const arma::vec& m) { return m.n_rows; });

    mod.add_type<arma::mat>("ArmaMatrix")
        .constructor<arma::uword, arma::uword>()
        .method("at", [] (const arma::mat& m, arma::uword i, arma::uword j) { return m(i, j); })
        .method("at!", [] (arma::mat& m, arma::uword i, arma::uword j, double x) { m(i, j) = x; })
        .method("nrows", [] (const arma::mat& m) { return m.n_rows; })
        .method("ncols", [] (const arma::mat& m) { return m.n_cols; });

    mod.method("version", &helfem::version);
    mod.method("verbose", &helfem::set_verbosity);

    mod.method("get_grid", &helfem::utils::get_grid);
    mod.method("invh", &helfem::utils::invh);
    mod.method("chebyshev", &helfem::chebyshev::chebyshev);
    mod.method("lobatto", &lobatto_compute);

    mod.add_type<helfem::modelpotential::ModelPotential>("ModelPotential")
        .method("V", static_cast<double (helfem::modelpotential::ModelPotential::*)(double) const>(&helfem::modelpotential::ModelPotential::V));
    mod.add_type<helfem::modelpotential::PointNucleus>("PointNucleus", jlcxx::julia_base_type<helfem::modelpotential::ModelPotential>())
        .constructor<int>();
    mod.add_type<helfem::modelpotential::GaussianNucleus>("GaussianNucleus", jlcxx::julia_base_type<helfem::modelpotential::ModelPotential>())
        .constructor<int, double>();
    mod.add_type<helfem::modelpotential::SphericalNucleus>("SphericalNucleus", jlcxx::julia_base_type<helfem::modelpotential::ModelPotential>())
        .constructor<int, double>();
    mod.add_type<helfem::modelpotential::HollowNucleus>("HollowNucleus", jlcxx::julia_base_type<helfem::modelpotential::ModelPotential>())
        .constructor<int, double>();

    mod.add_type<helfem::polynomial_basis::PolynomialBasis>("PolynomialBasis")
        .method("get_nbf", &helfem::polynomial_basis::PolynomialBasis::get_nbf)
        .method("get_noverlap", &helfem::polynomial_basis::PolynomialBasis::get_noverlap)
        .method("pb_order", &helfem::polynomial_basis::PolynomialBasis::get_order)
        .method("pb_eval", static_cast<arma::mat (helfem::polynomial_basis::PolynomialBasis::*)(const arma::vec &) const>(&helfem::polynomial_basis::PolynomialBasis::eval))
        .method("pb_eval", static_cast<void (helfem::polynomial_basis::PolynomialBasis::*)(const arma::vec &, arma::mat &, arma::mat &) const>(&helfem::polynomial_basis::PolynomialBasis::eval))
        .method("pb_eval_lapl", &helfem::polynomial_basis::PolynomialBasis::eval_lapl);
    mod.add_type<helfem::atomic::basis::RadialBasis>("RadialBasis")
        .constructor<helfem::polynomial_basis::PolynomialBasis*,int,arma::vec>()
        .method("get_nquad", &helfem::atomic::basis::RadialBasis::get_nquad)
        .method("get_poly", &helfem::atomic::basis::RadialBasis::get_poly)
        .method("get_poly_id", &helfem::atomic::basis::RadialBasis::get_poly_id)
        .method("get_poly_order", &helfem::atomic::basis::RadialBasis::get_poly_order)
        .method("nbf", &helfem::atomic::basis::RadialBasis::Nbf)
        .method("nel", &helfem::atomic::basis::RadialBasis::Nel)
        .method("radial_integral", static_cast<arma::mat (helfem::atomic::basis::RadialBasis::*)(const helfem::atomic::basis::RadialBasis &, int, bool, bool) const>(&helfem::atomic::basis::RadialBasis::radial_integral))
        .method("model_potential", static_cast<arma::mat (helfem::atomic::basis::RadialBasis::*)(const helfem::atomic::basis::RadialBasis &, const helfem::modelpotential::ModelPotential *, bool, bool) const>(&helfem::atomic::basis::RadialBasis::model_potential))
        .method("overlap", static_cast<arma::mat (helfem::atomic::basis::RadialBasis::*)(const helfem::atomic::basis::RadialBasis &) const>(&helfem::atomic::basis::RadialBasis::overlap))
        .method("get_bval", &helfem::atomic::basis::RadialBasis::get_bval)
        .method("add_boundary", &helfem::atomic::basis::RadialBasis::add_boundary)
        // Basis function introspection (at quadrature points)
        .method("get_bf", &helfem::atomic::basis::RadialBasis::get_bf)
        .method("get_df", &helfem::atomic::basis::RadialBasis::get_df)
        .method("get_lf", &helfem::atomic::basis::RadialBasis::get_lf)
        .method("get_wrad", &helfem::atomic::basis::RadialBasis::get_wrad)
        .method("get_r", &helfem::atomic::basis::RadialBasis::get_r);

    mod.method("polynomial_basis", [] (const int primbas, const int nnodes) {
        //return std::unique_ptr<helfem::polynomial_basis::PolynomialBasis>(helfem::polynomial_basis::get_basis(primbas, nnodes));
        return jlcxx::julia_owned(helfem::polynomial_basis::get_basis(primbas, nnodes));
    });
    mod.method("basis", &helfem_basis);
}