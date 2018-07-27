#include "../general/cmdline.h"
#include "../general/constants.h"
#include "../general/diis.h"
#include "../general/dftfuncs.h"
#include "../general/elements.h"
#include "../general/timer.h"
#include "basis.h"
#include "dftgrid.h"
#include <cfloat>

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

void sort_eig(arma::vec & Eorb, arma::mat & Cocc, arma::mat & Cvirt, const arma::mat & Fao, size_t Nact, int maxit, double convthr) {
  // Initialize vector
  arma::mat C(Cocc.n_rows,Cocc.n_cols+Cvirt.n_cols);
  C.cols(0,Cocc.n_cols-1)=Cocc;
  C.cols(Cocc.n_cols,Cocc.n_cols+Cvirt.n_cols-1)=Cvirt;

  // Compute lower bounds of eigenvalues
  for(int it=0;it<maxit;it++) {
    // Fock matrix
    arma::mat Fmo(arma::trans(C)*Fao*C);

    // Gerschgorin lower bound for eigenvalues
    arma::vec Ebar(Fao.n_cols), R(Fao.n_cols);
    for(size_t i=0;i<Fmo.n_cols;i++) {
      double r=0.0;
      for(size_t j=0;j<i;j++)
	r+=std::pow(Fmo(j,i),2);
      for(size_t j=i+1;j<Fmo.n_rows;j++)
	r+=std::pow(Fmo(j,i),2);
      r=sqrt(r);

      Ebar(i)=Fmo(i,i);
      R(i)=r;
    }

    // Sort by lowest possible eigenvalue
    arma::uvec idx(arma::sort_index(Ebar-R,"ascend"));
    printf("Orbital guess iteration %i\n",(int) it);
    double ograd(arma::sum(arma::square(R.subvec(0,Cocc.n_cols-1))));
    printf("Orbital gradient %e, occupied orbitals\n",ograd);
    for(size_t i=0;i<Cocc.n_cols;i++)
      printf("%2i %5i % e .. % e\n",(int) i, (int) idx(i), Ebar(idx(i))-R(idx(i)), Ebar(i)+R(idx(i)));

    // Has sort converged?
    bool convd=true;
    // Check if circles overlap. Maximum occupied orbital energy is
    double Emax=Ebar(0)+R(0);
    for(size_t i=0;i<Cocc.n_cols;i++)
      Emax=std::max(Emax,Ebar(i)+R(i));
    for(size_t i=Cocc.n_cols;i<Ebar.n_elem;i++)
      if(Ebar(i)-R(i) >= Emax)
	// Circles overlap!
	convd=false;
    // Check if gradient has converged
    if(ograd>=convthr)
      convd=false;
    if(convd)
      break;

    // Change orbital ordering
    C=C.cols(idx);

    // Occupy orbitals with lowest estimated eigenvalues
    arma::mat Cocctest(C.cols(0,Cocc.n_cols-1));
    arma::mat Cvirttest(C.cols(Cocc.n_cols,C.n_cols-1));

    // Improve Gerschgorin estimate
    eig_sub_wrk(Eorb,Cocctest,Cvirttest,Fao,Nact);

    // Update C
    C.cols(0,Cocc.n_cols-1)=Cocctest;
    C.cols(Cocc.n_cols,C.n_cols-1)=Cvirttest;
  }

  Cocc=C.cols(0,Cocc.n_cols-1);
  Cvirt=C.cols(Cocc.n_cols,C.n_cols-1);
}

void eig_sub(arma::vec & E, arma::mat & Cocc, arma::mat & Cvirt, const arma::mat & F, size_t nsub, int maxit, double convthr) {
  if(nsub >= Cocc.n_cols+Cvirt.n_cols) {
    arma::mat Corth(arma::join_rows(Cocc,Cvirt));

    arma::mat C;
    eig_gsym(E,C,F,Corth);
    if(Cocc.n_cols)
      Cocc=C.cols(0,Cocc.n_cols-1);
    Cvirt=C.cols(Cocc.n_cols,C.n_cols-1);
    return;
  }

  // Initialization: make sure we're occupying the lowest eigenstates
  sort_eig(E, Cocc, Cvirt, F, nsub, maxit, convthr);
  // The above already does everything
  return;

  // Perform initial solution
  eig_sub_wrk(E,Cocc,Cvirt,F,nsub);

  // Iterative improvement
  int iit;
  arma::vec Eold;
  for(iit=0;iit<maxit;iit++) {
    // New subspace solution
    eig_sub_wrk(E,Cocc,Cvirt,F,nsub);
    // Frozen subspace gradient
    arma::mat Cfrz(Cvirt.cols(nsub-Cocc.n_cols,Cvirt.n_cols-1));
    // Orbital gradient
    arma::mat G(arma::trans(Cocc)*F*Cfrz);
    double ograd(arma::norm(G,"fro"));
    printf("Eigeniteration %i orbital gradient %e\n",iit,ograd);
    arma::vec Ecur(E.subvec(0,Cocc.n_cols-1));
    Ecur.t().print("Occupied energies");
    if(iit>0)
      (Ecur-Eold).t().print("Energy change");
    Eold=Ecur;
    if(ograd<convthr)
      break;
  }
  if(iit==maxit) throw std::runtime_error("Eigensolver did not converge!\n");
}

void eig_iter(arma::vec & E, arma::mat & Cocc, arma::mat & Cvirt, const arma::mat & F, const arma::mat & Sinvh, size_t nocc, size_t neig, size_t nsub, int maxit, double convthr) {
  arma::mat Forth(Sinvh.t()*F*Sinvh);

  const arma::newarp::DenseGenMatProd<double> op(Forth);

  arma::newarp::SymEigsSolver< double, arma::newarp::EigsSelect::SMALLEST_ALGE, arma::newarp::DenseGenMatProd<double> > eigs(op, neig, nsub);
  eigs.init();

  arma::uword nconv = eigs.compute(maxit, convthr);
  printf("%i eigenvalues converged in %i iterations\n",(int) nconv, (int) eigs.num_iterations());
  E = eigs.eigenvalues();
  if(nconv < nocc)
    throw std::logic_error("Eigendecomposition did not convege!\n");

  arma::mat eigvec;
  // Go back to non-orthogonal basis
  eigvec = Sinvh*eigs.eigenvectors();

  Cocc=eigvec.cols(0,nocc-1);
  if(eigvec.n_cols>nocc)
    Cvirt=eigvec.cols(nocc,eigvec.n_cols-1);
  else
    Cvirt.clear();
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

void parse_nela_nelb(int & nela, int & nelb, int & Q, int & M, int Z) {
  if(nela==0 && nelb==0) {
    // Use Q and M. Number of electrons is
    int nel=Z-Q;
    if(M<1)
      throw std::runtime_error("Invalid value for multiplicity, which must be >=1.\n");
    else if(nel%2==0 && M%2!=1) {
      std::ostringstream oss;
      oss << "Requested multiplicity " << M << " with " << nel << " electrons.\n";
      throw std::runtime_error(oss.str());
    } else if(nel%2==1 && M%2!=0) {
      std::ostringstream oss;
      oss << "Requested multiplicity " << M << " with " << nel << " electrons.\n";
      throw std::runtime_error(oss.str());
    }

    if(nel%2==0)
      // Even number of electrons, the amount of spin up is
      nela=nel/2+(M-1)/2;
    else
      // Odd number of electrons, the amount of spin up is
      nela=nel/2+M/2;
    // The rest are spin down
    nelb=nel-nela;

    if(nela<0) {
      std::ostringstream oss;
      oss << "A multiplicity of " << M << " would mean " << nela << " alpha electrons!\n";
      throw std::runtime_error(oss.str());
    } else if(nelb<0) {
      std::ostringstream oss;
      oss << "A multiplicity of " << M << " would mean " << nelb << " beta electrons!\n";
      throw std::runtime_error(oss.str());
    }

  } else {
    Q=Z-nela-nelb;
    M=1+nela-nelb;

    if(M<1) {
      std::ostringstream oss;
      oss << "nela=" << nela << ", nelb=" << nelb << " would mean multiplicity " << M << " which is not allowed!\n";
      throw std::runtime_error(oss.str());
    }
  }
}

int main(int argc, char **argv) {
  cmdline::parser parser;

  // full option name, no short option, description, argument required
  parser.add<std::string>("Z", 0, "nuclear charge", true);
  parser.add<std::string>("Zl", 0, "left-hand nuclear charge", false, "");
  parser.add<std::string>("Zr", 0, "right-hand nuclear charge", false, "");
  parser.add<double>("Rmid", 0, "distance of nuclei from center", false, 0.0);
  parser.add<bool>("angstrom", 0, "input distances in angstrom", false, false);
  parser.add<int>("nela", 0, "number of alpha electrons", false, 0);
  parser.add<int>("nelb", 0, "number of beta  electrons", false, 0);
  parser.add<int>("Q", 0, "charge state", false, 0);
  parser.add<int>("M", 0, "spin multiplicity", false, 0);
  parser.add<int>("lmax", 0, "maximum l quantum number", true);
  parser.add<int>("mmax", 0, "maximum m quantum number", true);
  parser.add<double>("Rmax", 0, "practical infinity in au", false, 40.0);
  parser.add<int>("grid", 0, "type of grid: 1 for linear, 2 for quadratic, 3 for polynomial, 4 for logarithmic", false, 4);
  parser.add<double>("zexp", 0, "parameter in radial grid", false, 2.0);
  parser.add<int>("nelem0", 0, "number of elements between center and off-center nuclei", false, 0);
  parser.add<int>("nelem", 0, "number of elements", true);
  parser.add<int>("nnodes", 0, "number of nodes per element", false, 6);
  parser.add<int>("der_order", 0, "level of derivative continuity", false, 0);
  parser.add<int>("nquad", 0, "number of quadrature points", false, 10);
  parser.add<int>("maxit", 0, "maximum number of iterations", false, 50);
  parser.add<double>("convthr", 0, "convergence threshold", false, 1e-7);
  parser.add<double>("Ez", 0, "electric dipole field", false, 0.0);
  parser.add<double>("Qzz", 0, "electric quadrupole field", false, 0.0);
  parser.add<int>("nsub", 0, "dimension of active subspace", false, 1000);
  parser.add<double>("eigthr", 0, "convergence threshold for eigenvectors", false, 1e-9);
  parser.add<int>("maxeig", 0, "maximum number of iterations for eigensolution", false, 100);
  parser.add<bool>("diag", 0, "exact diagonalization", false, 1);
  parser.add<std::string>("method", 0, "method to use", false, "HF");
  parser.add<int>("ldft", 0, "theta rule for dft quadrature (0 for auto)", false, 0);
  parser.add<int>("mdft", 0, "phi rule for dft quadrature (0 for auto)", false, 0);
  parser.add<double>("dftthr", 0, "density threshold for dft", false, 1e-12);
  parser.parse_check(argc, argv);

  // Get parameters
  double Rmax(parser.get<double>("Rmax"));
  int igrid(parser.get<int>("grid"));
  double zexp(parser.get<double>("zexp"));
  double Ez(parser.get<double>("Ez"));
  double Qzz(parser.get<double>("Qzz"));

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

  // DFT angular grid
  int ldft(parser.get<int>("ldft"));
  int mdft(parser.get<int>("mdft"));
  double dftthr(parser.get<double>("dftthr"));

  // Nuclear charge
  int Z(get_Z(parser.get<std::string>("Z")));
  int Zl(get_Z(parser.get<std::string>("Zl")));
  int Zr(get_Z(parser.get<std::string>("Zr")));
  double Rhalf(parser.get<double>("Rmid"));
  // Number of occupied states
  int nela(parser.get<int>("nela"));
  int nelb(parser.get<int>("nelb"));
  int Q(parser.get<int>("Q"));
  int M(parser.get<int>("M"));

  std::string method(parser.get<std::string>("method"));

  if(parser.get<bool>("angstrom")) {
    // Convert to atomic units
    Rhalf*=ANGSTROMINBOHR;
  }

  parse_nela_nelb(nela,nelb,Q,M,Z+Zl+Zr);

  printf("Running calculation with Rmax=%e and %i elements.\n",Rmax,Nelem);
  printf("Using %i point quadrature rule.\n",Nquad);
  printf("Basis set composed of %i nodes with %i:th derivative continuity.\n",Nnodes,der_order);
  printf("This means using primitive polynomials of order %i.\n",Nnodes*(der_order+1)-1);

  printf("Angular grid spanning from l=0..%i, m=%i..%i.\n",lmax,-mmax,mmax);

  atomic::basis::TwoDBasis basis;
  if(Rhalf!=0.0)
    basis=atomic::basis::TwoDBasis(Z, Nnodes, der_order, Nquad, Nelem0, Nelem, Rmax, lmax, mmax, igrid, zexp, Zl, Zr, Rhalf);
  else
    basis=atomic::basis::TwoDBasis(Z, Nnodes, der_order, Nquad, Nelem, Rmax, lmax, mmax, igrid, zexp);
  printf("Basis set consists of %i angular shells composed of %i radial functions, totaling %i basis functions\n",(int) basis.Nang(), (int) basis.Nrad(), (int) basis.Nbf());

  printf("One-electron matrix requires %s\n",memory_size(basis.mem_1el()).c_str());
  printf("Auxiliary one-electron integrals require %s\n",memory_size(basis.mem_1el_aux()).c_str());
  printf("Auxiliary two-electron integrals require %s\n",memory_size(basis.mem_2el_aux()).c_str());

  double Enucr=(Rhalf>0) ? Z*(Zl+Zr)/Rhalf + Zl*Zr/(2*Rhalf) : 0.0;
  printf("Central nuclear charge is %i\n",Z);
  printf("Left- and right-hand nuclear charges are %i and %i at distance % .3f from center\n",Zl,Zr,Rhalf);
  printf("Nuclear repulsion energy is %e\n",Enucr);
  printf("Number of electrons is %i %i\n",nela,nelb);

  // Functional
  int x_func, c_func;
  ::parse_xc_func(x_func, c_func, method);
  ::print_info(x_func, c_func);

  bool dft=(x_func>0 || c_func>0);
  if(is_range_separated(x_func))
    throw std::logic_error("Range separated functionals are not supported.\n");
  // Fraction of exact exchange
  double kfrac(exact_exchange(x_func));

  helfem::dftgrid::DFTGrid grid;
  if(dft) {
    // These would appear to give reasonably converged values
    if(ldft==0)
      // Default value: we have 2*lmax from the bra and ket and 2 from
      // the volume element, and allow for 2*lmax from the
      // density/potential. Add in 3 more for a bit more accuracy.
      ldft=4*lmax+3;
    if(ldft<2*lmax)
      throw std::logic_error("Increase ldft to guarantee accuracy of quadrature!\n");
    
    if(mdft==0)
      // Default value: we have 2*mmax from the bra and ket, and allow
      // for 2*mmax from the density/potential. Add in 3 to make
      // sure quadrature is still accurate for mmax=0
      mdft=4*mmax+3;
    if(mdft<2*mmax)
      throw std::logic_error("Increase mdft to guarantee accuracy of quadrature!\n");

    grid=helfem::dftgrid::DFTGrid(&basis,ldft,mdft);
  }

  Timer timer;

  // Form overlap matrix
  arma::mat S(basis.overlap());
  // Get half-inverse
  timer.set();
  arma::mat Sinvh(basis.Sinvh(!diag));
  printf("Half-inverse formed in %.6f\n",timer.get());
  {
    arma::mat Smo(Sinvh.t()*S*Sinvh);
    Smo-=arma::eye<arma::mat>(Smo.n_rows,Smo.n_cols);
    printf("Orbital orthonormality deviation is %e\n",arma::norm(Smo,"fro"));
  }

  if(dft) {
    arma::mat Snum(grid.eval_overlap());
    // Convert to orthonormalized basis since norms of basis functions vary over a huge scale
    Snum=arma::trans(Sinvh)*(Snum-S)*Sinvh;
    double Serr(arma::norm(Snum,"fro"));
    printf("Error in overlap matrix evaluated through xc grid is %e\n",Serr);
    fflush(stdout);
    if(Serr>=1e-10)
      throw std::logic_error("Increase size of DFT grid!\n");
  }
  
  // Form nuclear attraction energy matrix
  Timer tnuc;
  if(Zl!=0 || Zr !=0)
    printf("Computing nuclear attraction integrals\n");
  arma::mat Vnuc(basis.nuclear());
  if(Zl!=0 || Zr !=0)
    printf("Done in %.6f\n",tnuc.get());

  // Dipole coupling
  arma::mat dip(basis.dipole_z());
  // Quadrupole coupling
  arma::mat quad(basis.quadrupole_zz());

  // Electric field coupling (minus sign cancels one from charge)
  arma::mat Vel(Ez*dip + Qzz*quad/3.0);
  // Form kinetic energy matrix
  arma::mat T(basis.kinetic());

  // Form Hamiltonian
  arma::mat H0(T+Vnuc+Vel);

  printf("One-electron matrices formed in %.6f\n",timer.get());

  // Occupied and virtual orbitals
  arma::mat Caocc, Cbocc, Cavirt, Cbvirt;
  arma::vec Ea, Eb;
  // Number of eigenenergies to print
  arma::uword nena(std::min((arma::uword) nela+4,Sinvh.n_cols));
  arma::uword nenb(std::min((arma::uword) nelb+4,Sinvh.n_cols));

  // Guess orbitals
  timer.set();
  {
    // Proceed by solving eigenvectors of core Hamiltonian with subspace iterations
    if(diag) {
      arma::mat C;
      eig_gsym(Ea,C,H0,Sinvh);
      Caocc=C.cols(0,nela-1);
      if(C.n_cols>Caocc.n_cols)
        Cavirt=C.cols(nela,C.n_cols-1);
    } else {
      {
	arma::vec E;
	arma::mat C;
	eig_gsym(E,C,H0,Sinvh);
	E.print("True eigenvalues");
      }

      // Initialize with Cholesky
      Caocc=Sinvh.cols(0,nela-1);
      if(Sinvh.n_cols>nela)
        Cavirt=Sinvh.cols(nela,Sinvh.n_cols-1);
      eig_sub(Ea,Caocc,Cavirt,H0,nsub,maxeig,eigthr);

      //eig_iter(Ea,Caocc,Cavirt,H0,Sinvh,nela,nena,nsub,maxeig,eigthr);
    }

    // Beta guess
    if(nelb)
      Cbocc=Caocc.cols(0,nelb-1);
    Cbvirt = (nelb<nela) ? arma::join_rows(Caocc.cols(nelb,nela-1),Cavirt) : Cavirt;
    Eb=Ea;

    Ea.subvec(0,nena-1).t().print("Alpha orbital energies");
    Eb.subvec(0,nenb-1).t().print("Beta  orbital energies");
  }
  printf("Initial guess performed in %.6f\n",timer.get());

  printf("Computing two-electron integrals\n");
  fflush(stdout);
  timer.set();
  basis.compute_tei(kfrac!=0.0);
  printf("Done in %.6f\n",timer.get());

  double Ekin, Epot, Ecoul, Exx, Exc, Efield, Etot;
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

  // Density matrices
  arma::mat P, Pa, Pb;

  for(int i=1;i<=maxit;i++) {
    printf("\n**** Iteration %i ****\n\n",i);

    // Form density matrix
    Pa=form_density(Caocc,nela);
    Pb=form_density(Cbocc,nelb);
    if(Pb.n_rows == 0)
      Pb.zeros(Pa.n_rows,Pa.n_cols);
    P=Pa+Pb;

    printf("Tr Pa = %f\n",arma::trace(Pa*S));
    if(nelb)
      printf("Tr Pb = %f\n",arma::trace(Pb*S));
    fflush(stdout);

    Ekin=arma::trace(P*T);
    Epot=arma::trace(P*Vnuc);
    Efield=arma::trace(P*Vel);

    // Form Coulomb matrix
    timer.set();
    arma::mat J(basis.coulomb(P));
    double tJ(timer.get());
    Ecoul=0.5*arma::trace(P*J);
    printf("Coulomb energy %.10e % .6f\n",Ecoul,tJ);
    fflush(stdout);

    // Form exchange matrix
    timer.set();
    arma::mat Ka, Kb;
    if(kfrac!=0.0) {
      Ka=kfrac*basis.exchange(Pa);
      if(nelb)
        Kb=kfrac*basis.exchange(Pb);
      else
        Kb.zeros(Cbocc.n_rows,Cbocc.n_rows);
      double tK(timer.get());
      Exx=0.5*arma::trace(Pa*Ka);
      if(Kb.n_rows == Pb.n_rows && Kb.n_cols == Pb.n_cols)
        Exx+=0.5*arma::trace(Pb*Kb);
      printf("Exchange energy %.10e % .6f\n",Exx,tK);
    } else {
      Exx=0.0;
    }
    fflush(stdout);

    // Exchange-correlation
    Exc=0.0;
    arma::mat XCa, XCb;
    if(dft) {
      timer.set();
      double nelnum;
      double ekin;
      grid.eval_Fxc(x_func, c_func, Pa, Pb, XCa, XCb, Exc, nelnum, ekin, nelb>0, dftthr);
      double txc(timer.get());
      printf("DFT energy %.10e % .6f\n",Exc,txc);
      printf("Error in integrated number of electrons % e\n",nelnum-nela-nelb);
      if(ekin!=0.0)
        printf("Error in integral of kinetic energy density % e\n",ekin-Ekin);
    }
    fflush(stdout);

    // Fock matrices
    arma::mat Fa(H0+J);
    arma::mat Fb(H0+J);
    if(Ka.n_rows == Fa.n_rows) {
      Fa+=Ka;
    }
    if(Kb.n_rows == Fb.n_rows) {
      Fb+=Kb;
    }
    if(dft) {
      Fa+=XCa;
      if(nelb>0) {
        Fb+=XCb;
      }
    }
    Etot=Ekin+Epot+Efield+Ecoul+Exx+Exc+Enucr;
    double dE=Etot-Eold;

    printf("Total energy is % .10f\n",Etot);
    if(i>1)
      printf("Energy changed by %e\n",dE);
    Eold=Etot;
    fflush(stdout);

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
    timer.set();
    diis.update(Fa,Fb,Pa,Pb,Etot,diiserr);
    printf("DIIS error is %e, update done in %.6f\n",diiserr,timer.get());
    fflush(stdout);

    // Solve DIIS to get Fock update
    timer.set();
    diis.solve_F(Fa,Fb);
    printf("DIIS solution done in %.6f\n",timer.get());
    fflush(stdout);

    // Have we converged? Note that DIIS error is still wrt full space, not active space.
    bool convd=(diiserr<convthr) && (std::abs(dE)<convthr);

    // Diagonalize Fock matrix to get new orbitals
    timer.set();
    if(diag) {
      arma::mat Ca, Cb;
      eig_gsym(Ea,Ca,Fa,Sinvh);
      eig_gsym(Eb,Cb,Fb,Sinvh);
      Caocc=Ca.cols(0,nela-1);
      if(Ca.n_cols>(size_t) nela)
        Cavirt=Ca.cols(nela,Ca.n_cols-1);
      if(nelb>0)
        Cbocc=Cb.cols(0,nelb-1);
      if(Cb.n_cols>(size_t) nelb)
        Cbvirt=Cb.cols(nelb,Cb.n_cols-1);
      printf("Full diagonalization done in %.6f\n",timer.get());
    } else {
      eig_sub(Ea,Caocc,Cavirt,Fa,nsub,maxeig,eigthr);
      eig_sub(Eb,Cbocc,Cbvirt,Fb,nsub,maxeig,eigthr);
      /*
      eig_iter(Ea,Caocc,Cavirt,Fa,Sinvh,nela,nena,nsub,maxeig,eigthr);
      eig_iter(Eb,Cbocc,Cbvirt,Fb,Sinvh,nelb,nenb,nsub,maxeig,eigthr);
      */
      printf("Active space diagonalization done in %.6f\n",timer.get());
    }

    if(Ea.n_elem>(size_t) nela)
      printf("Alpha HOMO-LUMO gap is % .3f eV\n",(Ea(nela)-Ea(nela-1))*HARTREEINEV);
    if(nelb && Eb.n_elem>(size_t) nelb)
      printf("Beta  HOMO-LUMO gap is % .3f eV\n",(Eb(nelb)-Eb(nelb-1))*HARTREEINEV);
    fflush(stdout);

    if(convd)
      break;
  }

  printf("%-21s energy: % .16f\n","Kinetic",Ekin);
  printf("%-21s energy: % .16f\n","Nuclear attraction",Epot);
  printf("%-21s energy: % .16f\n","Nuclear repulsion",Enucr);
  printf("%-21s energy: % .16f\n","Coulomb",Ecoul);
  printf("%-21s energy: % .16f\n","Exact exchange",Exx);
  printf("%-21s energy: % .16f\n","Exchange-correlation",Exc);
  printf("%-21s energy: % .16f\n","Electric field",Efield);
  printf("%-21s energy: % .16f\n","Total",Etot);

  printf("\n");
  printf("Electronic dipole     moment % .16e\n",arma::trace(dip*P));
  printf("Electronic quadrupole moment % .16e\n",arma::trace(quad*P));

  // Calculate <r^2> matrix
  arma::mat rmat(basis.radial_integral(1));
  arma::mat rsqmat(basis.radial_integral(2));
  // rms sizes
  arma::vec ra(arma::sqrt(arma::diagvec(arma::trans(Caocc)*rmat*Caocc)));
  arma::vec rmsa(arma::sqrt(arma::diagvec(arma::trans(Caocc)*rsqmat*Caocc)));
  arma::vec rb, rmsb;
  if(nelb) {
    rb=arma::sqrt(arma::diagvec(arma::trans(Cbocc)*rmat*Cbocc));
    rmsb=arma::sqrt(arma::diagvec(arma::trans(Cbocc)*rsqmat*Cbocc));
  }

  printf("\nOccupied orbital analysis:\n");
  printf("%2s %13s %12s %12s %13s %12s %12s\n","io","energy","<r>","sqrt(<r^2>)","energy","<r>","sqrt(<r^2>)");
  for(int io=0;io<nelb;io++) {
    printf("%2i % e %e %e % e %e %e\n",(int) io+1, Ea(io), ra(io), rmsa(io), Eb(io), rb(io), rmsb(io));
  }
  for(int io=nelb;io<nela;io++) {
    printf("%2i % e %e %e\n",(int) io+1, Ea(io), ra(io), rmsa(io));
  }
  printf("\n");

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
