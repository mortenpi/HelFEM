#define HARTREEINEV 27.211386
#include "../general/cmdline.h"
#include "../general/diis.h"
#include "basis.h"
#include <boost/timer/timer.hpp>
#include <cfloat>
#include <SymEigsSolver.h>  // Also includes <MatOp/DenseGenMatProd.h>

//#define SPARSE

using namespace helfem;

arma::mat form_density(const arma::mat & C, size_t nocc) {
  if(C.n_cols<nocc)
    throw std::logic_error("Not enough orbitals!\n");
  else if(nocc>0)
    return C.cols(0,nocc-1)*arma::trans(C.cols(0,nocc-1));
  else // nocc=0
    return arma::zeros<arma::mat>(C.n_rows,C.n_rows);
}

void eig_gsym(arma::vec & E, arma::mat & C, const arma::mat & F, const arma::mat & Sinvh) {
  // Form matrix in orthonormal basis
  arma::mat Forth(Sinvh.t()*F*Sinvh);

  if(!arma::eig_sym(E,C,Forth))
    throw std::logic_error("Eigendecomposition failed!\n");

  // Return to non-orthonormal basis
  C=Sinvh*C;
}

void eig_sub_wrk(arma::vec & E, arma::mat & Cocc, arma::mat & Cvirt, const arma::mat & F, size_t Nact) {
  // Form orbital gradient
  arma::mat Forth(Cocc.t()*F*Cvirt);

  // Compute gradient norms
  arma::vec Fnorm(Forth.n_cols);
  for(size_t i=0;i<Forth.n_cols;i++)
    Fnorm(i)=arma::norm(Forth.col(i),"fro");

  // Sort in decreasing value
  arma::uvec idx(arma::sort_index(Fnorm,"descend"));

  // Update order
  Cvirt=Cvirt.cols(idx);
  Fnorm=Fnorm(idx);

  // Calculate norms
  double act(arma::sum(Fnorm.subvec(0,Nact-Cocc.n_cols-1)));
  double frz(arma::sum(Fnorm.subvec(Nact-Cocc.n_cols-1,Fnorm.n_elem-1)));
  printf("Active space norm %e, frozen space norm %e\n",act,frz);

  // Form subspace solution
  arma::mat C;
  arma::mat Corth(arma::join_rows(Cocc,Cvirt.cols(0,Nact-Cocc.n_cols-1)));
  eig_gsym(E,C,F,Corth);

  // Update occupied and virtual orbitals
  Cocc=C.cols(0,Cocc.n_cols-1);
  Cvirt.cols(0,Nact-Cocc.n_cols-1)=C.cols(Cocc.n_cols,Nact-1);
}

void eig_sub(arma::vec & E, arma::mat & Cocc, arma::mat & Cvirt, const arma::mat & F, size_t nsub, int maxit, double convthr) {
  if(nsub >= Cocc.n_cols+Cvirt.n_cols) {
    arma::mat Corth(arma::join_rows(Cocc,Cvirt));

    arma::mat C;
    eig_gsym(E,C,F,Corth);
    Cocc=C.cols(0,Cocc.n_cols-1);
    Cvirt=C.cols(Cocc.n_cols,C.n_cols-1);
    return;
  }

  // Perform initial solution
  eig_sub_wrk(E,Cocc,Cvirt,F,nsub);

  // Iterative improvement
  for(int iit=1;iit<=maxit;iit++) {
    // New subspace solution
    eig_sub_wrk(E,Cocc,Cvirt,F,nsub);
    // Frozen subspace gradient
    arma::mat Cfrz(Cvirt.cols(nsub-Cocc.n_cols,Cvirt.n_cols-1));
    // Orbital gradient
    arma::mat G(arma::trans(Cocc)*F*Cfrz);
    double ograd(arma::norm(G,"fro"));
    printf("Eigeniteration %i orbital gradient %e\n",iit,ograd);
    if(ograd<convthr)
      break;
  }
}

std::string memory_size(size_t size) {
  std::ostringstream ret;

  const size_t kilo(1000);
  const size_t mega(kilo*kilo);
  const size_t giga(mega*kilo);

  // Number of gigabytes
  size_t gigs(size/giga);
  if(gigs>0) {
    size-=gigs*giga;
    ret << gigs;
    ret << " G ";
  }
  size_t megs(size/mega);
  if(megs>0) {
    size-=megs*mega;
    ret << megs;
    ret << " M ";
  }
  size_t kils(size/kilo);
  if(kils>0) {
    size-=kils*kilo;
    ret << kils;
    ret << " k ";
  }

  return ret.str();
}

int main(int argc, char **argv) {
  cmdline::parser parser;

  // full option name, no short option, description, argument required
  parser.add<int>("Z", 0, "nuclear charge", false, 0);
  parser.add<int>("Zl", 0, "left-hand nuclear charge", false, 0);
  parser.add<int>("Zr", 0, "right-hand nuclear charge", false, 0);
  parser.add<double>("Rmid", 0, "distance of nuclei from center", false, 0.0);
  parser.add<int>("nela", 0, "number of alpha electrons", true);
  parser.add<int>("nelb", 0, "number of beta  electrons", true);
  parser.add<int>("lmax", 0, "maximum l quantum number", true);
  parser.add<int>("mmax", 0, "maximum m quantum number", true);
  parser.add<double>("Rmax", 0, "practical infinity", false, 40.0);
  parser.add<int>("grid", 0, "type of grid: 1 for linear, 2 for quadratic, 3 for polynomial, 4 for logarithmic", false, 4);
  parser.add<double>("zexp", 0, "parameter in radial grid", false, 2.0);
  parser.add<int>("nelem0", 0, "number of elements between center and off-center nuclei", true);
  parser.add<int>("nelem", 0, "number of elements", true);
  parser.add<int>("nnodes", 0, "number of nodes per element", false, 6);
  parser.add<int>("der_order", 0, "level of derivative continuity", false, 0);
  parser.add<int>("nquad", 0, "number of quadrature points", false, 10);
  parser.add<int>("maxit", 0, "maximum number of iterations", false, 50);
  parser.add<double>("convthr", 0, "convergence threshold", false, 1e-7);
  parser.add<double>("Ez", 0, "electric field", false, 0.0);
  parser.add<int>("nsub", 0, "dimension of active subspace", false, 1000);
  parser.add<double>("eigthr", 0, "convergence threshold for eigenvectors", false, 1e-9);
  parser.add<int>("maxeig", 0, "maximum number of iterations for eigensolution", false, 100);
  parser.add<bool>("diag", 0, "exact diagonalization", false, 0);
  parser.parse_check(argc, argv);

  // Get parameters
  double Rmax(parser.get<double>("Rmax"));
  int igrid(parser.get<int>("grid"));
  double zexp(parser.get<double>("zexp"));
  double Ez(parser.get<double>("Ez"));

  int maxit(parser.get<int>("maxit"));
  double convthr(parser.get<double>("convthr"));

  int nsub(parser.get<int>("nsub"));
  int maxeig(parser.get<int>("maxeig"));
  double eigthr(parser.get<double>("eigthr"));
  bool diag(parser.get<bool>("diag"));

  // Number of elements
  int Nelem0(parser.get<int>("nelem0"));
  int Nelem(parser.get<int>("nelem"));
  // Number of nodes
  int Nnodes(parser.get<int>("nnodes"));
  // Derivative order
  int der_order(parser.get<int>("der_order"));
  // Order of quadrature rule
  int Nquad(parser.get<int>("nquad"));
  // Angular grid
  int lmax(parser.get<int>("lmax"));
  int mmax(parser.get<int>("mmax"));

  // Nuclear charge
  int Z(parser.get<int>("Z"));
  int Zl(parser.get<int>("Zl"));
  int Zr(parser.get<int>("Zr"));
  double Rhalf(parser.get<double>("Rmid"));
  // Number of occupied states
  int nela(parser.get<int>("nela"));
  int nelb(parser.get<int>("nelb"));

  printf("Running calculation with Rmax=%e and %i elements.\n",Rmax,Nelem);
  printf("Using %i point quadrature rule.\n",Nquad);
  printf("Basis set composed of %i nodes with %i:th derivative continuity.\n",Nnodes,der_order);
  printf("This means using primitive polynomials of order %i.\n",Nnodes*(der_order+1)-1);

  printf("Angular grid spanning from l=0..%i, m=%i..%i.\n",lmax,-mmax,mmax);

  basis::TwoDBasis basis;
  if(Rhalf!=0.0)
    basis=basis::TwoDBasis(Z, Nnodes, der_order, Nquad, Nelem0, Nelem, Rmax, lmax, mmax, igrid, zexp, Zl, Zr, Rhalf);
  else
    basis=basis::TwoDBasis(Z, Nnodes, der_order, Nquad, Nelem, Rmax, lmax, mmax, igrid, zexp);
  printf("Basis set consists of %i angular shells composed of %i radial functions, totaling %i basis functions\n",(int) basis.Nang(), (int) basis.Nrad(), (int) basis.Nbf());

  printf("One-electron matrix requires %s\n",memory_size(basis.mem_1el()).c_str());
  printf("Auxiliary one-electron integrals require %s\n",memory_size(basis.mem_1el_aux()).c_str());
  printf("Auxiliary two-electron integrals require %s\n",memory_size(basis.mem_2el_aux()).c_str());

  double Enucr=(Rhalf>0) ? Z*(Zl+Zr)/Rhalf + Zl*Zr/(2*Rhalf) : 0.0;
  printf("Central nuclear charge is %i\n",Z);
  printf("Left- and right-hand nuclear charges are %i and %i at distance % .3f from center\n",Zl,Zr,Rhalf);
  printf("Nuclear repulsion energy is %e\n",Enucr);
  printf("Number of electrons is %i %i\n",nela,nelb);

  boost::timer::cpu_timer timer;

  // Form overlap matrix
  arma::mat S(basis.overlap());
  // Form nuclear attraction energy matrix
  timer.start();
  if(Zl!=0 || Zr !=0)
    printf("Computing nuclear attraction integrals\n");
  arma::mat Vnuc(basis.nuclear());
  if(Zl!=0 || Zr !=0)
    printf("Done in %.6f\n",timer.elapsed().wall*1e-9);

  // Form electric field coupling matrix
  arma::mat Vel(basis.electric(Ez));
  // Form kinetic energy matrix
  arma::mat T(basis.kinetic());

  // Form Hamiltonian
  arma::mat H0(T+Vnuc+Vel);

  printf("One-electron matrices formed in %.6f\n",timer.elapsed().wall*1e-9);

  // Get half-inverse
  timer.start();
  arma::mat Sinvh(basis.Sinvh(!diag));
  printf("Half-inverse formed in %.6f\n",timer.elapsed().wall*1e-9);

  // Occupied and virtual orbitals
  arma::mat Caocc, Cbocc, Cavirt, Cbvirt;
  arma::vec Ea, Eb;
  // Number of eigenenergies to print
  arma::uword nena(std::min((arma::uword) nela+4,Sinvh.n_cols));
  arma::uword nenb(std::min((arma::uword) nelb+4,Sinvh.n_cols));

  // Guess orbitals
  timer.start();
  {
    // Proceed by solving eigenvectors of core Hamiltonian with subspace iterations
    if(diag) {
      arma::mat C;
      eig_gsym(Ea,C,H0,Sinvh);
      Caocc=C.cols(0,nela-1);
      Cavirt=C.cols(nela,C.n_cols-1);
    } else {
      // Initialize with Cholesky
      Caocc=Sinvh.cols(0,nela-1);
      Cavirt=Sinvh.cols(nela,Sinvh.n_cols-1);
      eig_sub(Ea,Caocc,Cavirt,H0,nsub,maxeig,eigthr);
    }

    // Beta guess
    Cbocc=Caocc.cols(0,nelb-1);
    Cbvirt = (nelb<nela) ? arma::join_rows(Caocc.cols(nelb,nela-1),Cavirt) : Cavirt;
    Eb=Ea;

    Ea.subvec(0,nena-1).t().print("Alpha orbital energies");
    Eb.subvec(0,nenb-1).t().print("Beta  orbital energies");
  }
  printf("Initial guess performed in %.6f\n",timer.elapsed().wall*1e-9);

  printf("Computing two-electron integrals\n");
  fflush(stdout);
  timer.start();
  basis.compute_tei();
  printf("Done in %.6f\n",timer.elapsed().wall*1e-9);

  double Ekin, Epot, Ecoul, Exx, Efield, Etot;
  double Eold=0.0;

  double diiseps=1e-2, diisthr=1e-3;
  bool usediis=true, useadiis=true;
  bool diis_c1=false;
  int diisorder=5;
  uDIIS diis(S,Sinvh,usediis,diis_c1,diiseps,diisthr,useadiis,true,diisorder);
  double diiserr;

  // Subspace dimension
  if(nsub==0 || nsub>(int) (Caocc.n_cols+Cavirt.n_cols))
    nsub=(Caocc.n_cols+Cavirt.n_cols);

  for(int i=1;i<=maxit;i++) {
    printf("\n**** Iteration %i ****\n\n",i);

    // Form density matrix
    arma::mat Pa(form_density(Caocc,nela));
    arma::mat Pb(form_density(Cbocc,nelb));
    arma::mat P(Pa+Pb);

    // Calculate <r^2>
    //printf("<r^2> is %e\n",arma::trace(basis.radial_integral(2)*Pnew));

    printf("Tr Pa = %f\n",arma::trace(Pa*S));
    printf("Tr Pb = %f\n",arma::trace(Pb*S));

    Ekin=arma::trace(P*T);
    Epot=arma::trace(P*Vnuc);
    Efield=arma::trace(P*Vel);

    // Form Coulomb matrix
    timer.start();
    arma::mat J(basis.coulomb(P));
    double tJ(timer.elapsed().wall*1e-9);
    Ecoul=0.5*arma::trace(P*J);
    printf("Coulomb energy %.10e % .6f\n",Ecoul,tJ);

    // Form exchange matrix
    timer.start();
    arma::mat Ka(basis.exchange(Pa));
    arma::mat Kb;
    if(nelb)
      Kb=basis.exchange(Pb);
    else
      Kb.zeros(Cbocc.n_rows,Cbocc.n_rows);
    double tK(timer.elapsed().wall*1e-9);
    Exx=0.5*(arma::trace(Pa*Ka)+arma::trace(Pb*Kb));
    printf("Exchange energy %.10e % .6f\n",Exx,tK);

    // Fock matrices
    arma::mat Fa(H0+J+Ka);
    arma::mat Fb(H0+J+Kb);
    Etot=Ekin+Epot+Efield+Ecoul+Exx+Enucr;
    double dE=Etot-Eold;

    printf("Total energy is % .10f\n",Etot);
    if(i>1)
      printf("Energy changed by %e\n",dE);
    Eold=Etot;

    /*
      S.print("S");
      T.print("T");
      Vnuc.print("Vnuc");
      Ca.print("Ca");
      Pa.print("Pa");
      J.print("J");
      Ka.print("Ka");

      arma::mat Jmo(Ca.t()*J*Ca);
      arma::mat Kmo(Ca.t()*Ka*Ca);
      Jmo.submat(0,0,10,10).print("Jmo");
      Kmo.submat(0,0,10,10).print("Kmo");


      Kmo+=Jmo;
      Kmo.print("Jmo+Kmo");

      Fa.print("Fa");
      arma::mat Fao(Sinvh.t()*Fa*Sinvh);
      Fao.print("Fao");
      Sinvh.print("Sinvh");
    */

    /*
      arma::mat Jmo(Ca.t()*J*Ca);
      arma::mat Kmo(Ca.t()*Ka*Ca);
      arma::mat Fmo(Ca.t()*Fa*Ca);
      Jmo=Jmo.submat(0,0,4,4);
      Kmo=Kmo.submat(0,0,4,4);
      Fmo=Fmo.submat(0,0,4,4);
      Jmo.print("J");
      Kmo.print("K");
      Fmo.print("F");
    */

    // Update DIIS
    timer.start();
    diis.update(Fa,Fb,Pa,Pb,Etot,diiserr);
    printf("Diis error is %e\n",diiserr);
    fflush(stdout);
    // Solve DIIS to get Fock update
    diis.solve_F(Fa,Fb);
    printf("DIIS update and solution done in %.6f\n",timer.elapsed().wall*1e-9);

    // Have we converged? Note that DIIS error is still wrt full space, not active space.
    bool convd=(diiserr<convthr) && (std::abs(dE)<convthr);

    // Diagonalize Fock matrix to get new orbitals
    timer.start();
    if(diag) {
      arma::mat Ca, Cb;
      eig_gsym(Ea,Ca,Fa,Sinvh);
      eig_gsym(Eb,Cb,Fb,Sinvh);
      Caocc=Ca.cols(0,nela-1);
      Cavirt=Ca.cols(nela,Ca.n_cols-1);
      Cbocc=Cb.cols(0,nelb-1);
      Cbvirt=Cb.cols(nelb,Cb.n_cols-1);
      printf("Full diagonalization done in %.6f\n",timer.elapsed().wall*1e-9);
    } else {
      eig_sub(Ea,Caocc,Cavirt,Fa,nsub,maxeig,eigthr);
      eig_sub(Eb,Cbocc,Cbvirt,Fb,nsub,maxeig,eigthr);
      printf("Active space diagonalization done in %.6f\n",timer.elapsed().wall*1e-9);
    }

    if(nelb)
      printf("HOMO-LUMO gap is % .3f % .3f eV\n",(Ea(nela)-Ea(nela-1))*HARTREEINEV,(Eb(nelb)-Eb(nelb-1))*HARTREEINEV);
    else
      printf("HOMO-LUMO gap is % .3f eV\n",(Ea(nela)-Ea(nela-1))*HARTREEINEV);

    if(convd)
      break;
  }

  printf("%-21s energy: % .16f\n","Kinetic",Ekin);
  printf("%-21s energy: % .16f\n","Nuclear attraction",Epot);
  printf("%-21s energy: % .16f\n","Nuclear repulsion",Enucr);
  printf("%-21s energy: % .16f\n","Coulomb",Ecoul);
  printf("%-21s energy: % .16f\n","Exchange",Exx);
  printf("%-21s energy: % .16f\n","Electric field",Efield);
  printf("%-21s energy: % .16f\n","Total",Etot);
  Ea.subvec(0,nena-1).t().print("Alpha orbital energies");
  Eb.subvec(0,nenb-1).t().print("Beta  orbital energies");

  /*
  // Test orthonormality
  arma::mat Smo(Ca.t()*S*Ca);
  Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
  printf("Alpha orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  Smo=(Cb.t()*S*Cb);
  Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
  printf("Beta orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  */

  return 0;
}
