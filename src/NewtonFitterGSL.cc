/*! \file 
 *  \brief Implements class NewtonFitterGSL
 *
 * Author: Benno List
 * $Date: 2011/03/03 15:03:03 $
 * $Author: blist $
 
 Notes:
 - need routine to calculate best lambda values for a point
 - calculate mu from lambda values
 
 
 
 *
 * \b Changelog:
 * - 26.9.08 BL: Correct parameter counting (discared fixed parameters)
 *
 * \b CVS Log messages:
 * - $Log: NewtonFitterGSL.cc,v $
 * - Revision 1.6  2011/03/03 15:03:03  blist
 * - Latest version, with NewFitterGSL
 * -
 * - Revision 1.5  2009/09/02 13:10:57  blist
 * - Added errors for NewtonFitterGSL
 * -
 * - Revision 1.4  2009/09/01 09:48:13  blist
 * - Added tracer mechanism, added access to fit covariance matrix
 * -
 * - Revision 1.3  2009/02/18 11:56:21  mbeckman
 * - PhotonFitObject*.cc: documentation, debug output
 * - NewtonFitterGSL.cc:  bug fix (Lagrange multipliers not initialized), debug output
 * - JetFitObject.cc:     bug fix: division by 0, if energy <= mass
 * -
 * - Revision 1.2  2009/02/17 12:46:35  blist
 * - Improved version of NewtonFitterGSL, JetFitObject changed
 * -
 * - Revision 1.1  2008/10/16 08:13:44  blist
 * - New versions of OPALfitter and Newtonfitter using GSL
 * -
 * - Revision 1.4  2008/09/26 09:58:10  boehmej
 * - removed ~100 semicolons after } at end of function implementation :)
 * -
 * - Revision 1.3  2008/09/26 07:30:54  blist
 * - Added documentation,
 * - removed bug in Newtonfitter (could not handle fixed parameters correctly)
 *
 */ 

#undef NDEBUG

#include "NewtonFitterGSL.h" 

#include<iostream>
#include<cmath>
#include<cassert>

#include "BaseFitObject.h"
#include "BaseHardConstraint.h"
#include "BaseSoftConstraint.h"
#include "BaseTracer.h"

#include <gsl/gsl_block.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_cdf.h>

using std::cout;
using std::cerr;
using std::endl;
using std::abs;

static int nitdebug = 100;
static int nitcalc = 0;
static int nitsvd = 0;

// constructor
NewtonFitterGSL::NewtonFitterGSL() 
  : npar (0), ncon (0), nsoft (0), nunm(0), ierr(0), nit(0), fitprob(0), chi2(0),
    idim (0),
    x(0), xold(0), xbest(0), dx(0), dxscal (0), grad(0), y(0), yscal(0), 
    perr(0), v1 (0), v2(0), Meval (0),
    M(0), Mscal (0), M1(0), M2 (0), M3 (0), M4 (0), M5 (0), Mevec (0), 
    CC (0), CC1 (0), CCinv (0), permM(0), ws(0),
    wsdim(0), chi2best(0),
    chi2new(0),
    chi2old(0),
    fvalbest(0),scale(0),scalebest(0),stepsize(0),stepbest(0),
    imerit (1),
    debug (0)
{}

// destructor
NewtonFitterGSL::~NewtonFitterGSL() {

  if (x) gsl_vector_free (x);               x=0;
  if (xold) gsl_vector_free (xold);         xold=0;
  if (xbest) gsl_vector_free (xbest);       xbest=0;
  if (dx) gsl_vector_free (dx);             dx=0;
  if (dxscal) gsl_vector_free (dxscal);     dxscal=0;
  if (grad) gsl_vector_free (grad);         grad=0;
  if (y) gsl_vector_free (y);               y=0;
  if (yscal) gsl_vector_free (yscal);       yscal=0;
  if (perr) gsl_vector_free (perr);         perr=0;
  if (v1) gsl_vector_free (v1);             v1=0;
  if (v2) gsl_vector_free (v2);             v2=0;
  if (Meval) gsl_vector_free (Meval);       Meval=0;
  if (M) gsl_matrix_free (M);               M=0;
  if (Mscal) gsl_matrix_free (Mscal);       Mscal=0;
  if (M1) gsl_matrix_free (M1);             M1=0;
  if (M2) gsl_matrix_free (M2);             M2=0;
  if (M3) gsl_matrix_free (M3);             M3=0;
  if (M4) gsl_matrix_free (M4);             M4=0;
  if (M5) gsl_matrix_free (M5);             M5=0;
  if (Mevec) gsl_matrix_free (Mevec);       Mevec=0;
  if (CC) gsl_matrix_free (CC);             CC=0;
  if (CC1) gsl_matrix_free (CC1);           CC1=0;
  if (CCinv) gsl_matrix_free (CCinv);       CCinv=0;
  if (permM) gsl_permutation_free (permM);  permM=0;
  if (ws) gsl_eigen_symmv_free (ws);        ws=0; wsdim=0;
}



double NewtonFitterGSL::fit() {

  // order parameters etc
  initialize();
  
  // initialize eta, etasv, y   
  assert (x && x->size == idim);
  assert (dx && dx->size == idim);
  assert (y && y->size == idim);
  assert (perr && perr->size == idim);
  assert (v1 && v1->size == idim);
  assert (v2 && v2->size == idim);
  assert (Meval && Meval->size == idim);
  assert (M && M->size1 == idim && M->size1 == idim);
  assert (M1 && M1->size1 == idim && M1->size1 == idim);
  assert (Mevec && Mevec->size1 == idim && Mevec->size1 == idim);
  assert (permM && permM->size == idim);
  
  gsl_vector_set_zero (x);
  gsl_vector_set_zero (y);
  gsl_vector_set_all (perr, 1);

  // Store old x values in xold
  fillxold();    
  // make sure parameters are consistent
  updateParams (xold);
  fillxold();    
  // Get starting values into x
  gsl_vector_memcpy (x, xold);  
  
  
  // LET THE GAMES BEGIN
  
#ifndef FIT_TRACEOFF
  if (tracer) tracer->initialize (*this);
#endif   
  
  bool converged = 0;
  ierr = 0;
  
  double chi2new = calcChi2();
  nit = 0;
  if (debug>1) {
    cout << "Fit objects:\n";
    for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
      BaseFitObject *fo = *i;
      assert (fo);
      cout << fo->getName() << ": " << *fo << ", chi2=" << fo->getChi2() << endl;
      for (int ilocal = 0; ilocal < fo->getNPar(); ++ilocal) {
        if (!fo->isParamFixed(ilocal)) {
          int iglobal = fo->getGlobalParNum (ilocal);
          cout << "  par " << fo->getParamName(ilocal) << ": local: " << ilocal << ": global: " << iglobal
               << " value=" << fo->getParam (ilocal) << " +- " << fo->getError(ilocal);
          if (fo->isParamMeasured(ilocal)) 
            cout << " measured: " << fo->getMParam (ilocal);
          cout << endl;
        }
        else {
          cout << "  par " << fo->getParamName(ilocal) << ": local: " << ilocal << " -- fixed -- " 
               << " value=" << fo->getParam (ilocal) << " +- " << fo->getError(ilocal)
               << endl;
        
        }
      }
    }
    cout << "constraints:\n";
    for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
      BaseHardConstraint *c = *i;
      assert (c);
      cout << i-constraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() << endl;
      int kglobal = c->getGlobalNum();
      cout << "  global number: " << kglobal << endl;
    }
    cout << "soft constraints:\n";
    for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
      BaseSoftConstraint *c = *i;
      assert (c);
      cout << i-softconstraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() 
           << ", chi2: " << c->getChi2() << endl;
    }
  }
  
  do {
  
    double chi2old = chi2new;
    
    if (debug>1 && (nit==0 || nit<nitdebug)) cout << "===================\nStarting iteration " << nit << endl;
    if (debug>2 && (nit==0 || nit<nitdebug)) {
      cout << "Fit objects:\n";
      for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
        BaseFitObject *fo = *i;
        assert (fo);
        cout << fo->getName() << ": " << *fo << ", chi2=" << fo->getChi2() << endl;
      }
      cout << "constraints:\n";
      for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
        BaseHardConstraint *c = *i;
        assert (c);
        cout << i-constraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() << endl;
      }
      cout << "soft constraints:\n";
      for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
        BaseSoftConstraint *c = *i;
        assert (c);
        cout << i-softconstraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() 
             << ", chi2: " << c->getChi2() << endl;
      }
    }
    
    // Store old x values in xold
    fillxold();    
    // Fill errors into perr
    fillperr();    
 
    // Compose M:
    calcM(); 
    
    // Now, calculate the result vector y with the values of the derivatives
    // d chi^2/d x
    calcy();

    if (debug>3 && (nit==0 || nit<nitdebug)) {
      cout << "After setting up equations: \n";
      debug_print (M, "M");
      debug_print (Mscal, "Mscal");
      debug_print (y, "y");
      debug_print (yscal, "yscal");
      debug_print (perr, "perr");
      debug_print (x, "x");
      debug_print (xold, "xold");
    }
    
    scalevals[0] = 0;
    fvals[0] = 0.5*pow (gsl_blas_dnrm2 (yscal), 2);
    fvalbest = fvals[0];
    stepsize = 0;
    scalebest = 0;
    stepbest = 0;
    // cout << "fval before calcDx: " << fvals[0] << endl;    
  
    int ifail = calcDx ();
    if (ifail != 0) {
      cout << "NewtonFitterGSL::fit: calcDx: ifail=" << ifail << endl;
      ierr = 2;
      break;
    }
    // Update values in Fitobjects
    updateParams (xbest);
    
    if (debug > 1) { 
      debug_print (xbest, "new parameters");
    }  

    calcy();
    //cout << "New fval: " << 0.5*pow(gsl_blas_dnrm2 (yscal), 2) << endl;
    chi2new = calcChi2();
    //cout << "chi2: " << chi2old << " -> " << chi2new << endl;
   
    if (debug>3 && (nit==0 || nit<nitdebug)) {
      cout << "After solving equations: \n";
      debug_print (xbest, "xbest");
    }
    
  
//   *-- Convergence criteria 

    if (debug > 1&& nit<nitdebug) {
      cout << "old chi2: " << chi2old << ", new chi2: " << chi2new << ", diff=" << chi2old-chi2new << endl;
    }
    ++nit;
    if (nit > 200) ierr = 1;
    
    converged = (abs (chi2new - chi2old) < 0.001 && fvalbest < 1E-3 && 
                (fvalbest < 1E-6 || abs(fvals[0]-fvalbest) < 0.2*fvalbest));
                
//     if (abs (chi2new - chi2old) >= 0.001)
//       cout << "abs (chi2new - chi2old)=" << abs (chi2new - chi2old) << " -> try again\n";      
//     if (fvalbest >= 1E-3)
//       cout << "fvalbest=" << fvalbest << " -> try again\n";      
//     if (fvalbest >= 1E-6 && abs(fvals[0]-fvalbest) >= 0.2*fvalbest )
//       cout << "fvalbest=" << fvalbest  
//            << ", abs(fvals[0]-fvalbest)=" << abs(fvals[0]-fvalbest)<< " -> try again\n";      
//     if (stepbest >= 1E-3)
//       cout << "stepbest=" << stepbest << " -> try again\n";      
//     cout << "converged=" << converged << endl;
    if (debug > 0 && converged) {
      cout << "abs (chi2new - chi2old)=" << abs (chi2new - chi2old) << "\n"      
           << "fvalbest=" << fvalbest << "\n"
           << "abs(fvals[0]-fvalbest)=" << abs(fvals[0]-fvalbest)<< "\n";      
    } 

#ifndef FIT_TRACEOFF
    if (tracer) tracer->step (*this);
#endif  
 
  } while (!(converged || ierr));
  
// *-- End of iterations - calculate errors.

// ERROR CALCULATION 

  if (!ierr) {

    calcCovMatrix();  

    // update errors in fitobjects
    for (unsigned int ifitobj = 0; ifitobj < fitobjects.size(); ++ifitobj) {
      for (int ilocal = 0; ilocal < fitobjects[ifitobj]->getNPar(); ++ilocal) {
        int iglobal = fitobjects[ifitobj]->getGlobalParNum (ilocal); 
        for (int jlocal = ilocal; jlocal < fitobjects[ifitobj]->getNPar(); ++jlocal) {
          int jglobal = fitobjects[ifitobj]->getGlobalParNum (jlocal); 
          if (iglobal >= 0 && jglobal >= 0) 
          fitobjects[ifitobj]->setCov(ilocal, jlocal, gsl_matrix_get (CCinv, iglobal, jglobal)); 
	}//CCinv
      }
    }
  }
  

  if (debug>1) {
    cout << "========= END =========\n";
    cout << "Fit objects:\n";
    for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
      BaseFitObject *fo = *i;
      assert (fo);
      cout << fo->getName() << ": " << *fo << ", chi2=" << fo->getChi2() << endl;
    }
    cout << "constraints:\n";
    for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
      BaseHardConstraint *c = *i;
      assert (c);
      cout << i-constraints.begin() << " " << c->getName() << ": " << c->getValue() << "+-" << c->getError() << endl;
    }
    cout << "=============================================\n";
  }

// *-- Turn chisq into probability.
  fitprob = (chi2new >= 0 && ncon+nsoft-nunm> 0) ? gsl_cdf_chisq_Q(chi2new, ncon+nsoft-nunm) : -1;
  
#ifndef FIT_TRACEOFF
    if (tracer) tracer->finish (*this);
#endif   

  if (debug > 0) {
    cout << "NewtonFitterGSL::fit: converged=" << converged
         << ", nit=" << nit << ", fitprob=" << fitprob << endl;
  }

  if (ierr > 0) fitprob = -1;

  return fitprob;
    
}

bool NewtonFitterGSL::initialize() {
  covValid = false;
//  bool debug = true;

  // tell fitobjects the global ordering of their parameters:
  npar = 0;
  nunm = 0;
  // 
  for (unsigned int ifitobj = 0; ifitobj < fitobjects.size(); ++ifitobj) {
    for (int ilocal = 0; ilocal < fitobjects[ifitobj]->getNPar(); ++ilocal) {
      if (!fitobjects[ifitobj]->isParamFixed(ilocal)) {
        if (debug > 3) cout << "NewtonFitterGSL::initialize: parameter " << ilocal 
                            << " of fitobject " << fitobjects[ifitobj]->getName()
                            << " gets global number " << npar << endl;
        fitobjects[ifitobj]->setGlobalParNum (ilocal, npar);
        ++npar;        
        if (!fitobjects[ifitobj]->isParamMeasured(ilocal)) ++nunm;
      }
    }
  }
  
  // set number of constraints
  ncon = constraints.size();
  // Tell the constraints their numbers
  for (unsigned int icon = 0; icon < ncon; ++icon) {
    BaseHardConstraint *c = constraints[icon];
    assert (c);
    if (debug > 3) cout << "NewtonFitterGSL::initialize: constraint " << c->getName() 
                        << " gets global number " << npar+icon << endl;
    c->setGlobalNum (npar+icon);
//    if (debug) cout << "Constraint " << icon << " -> global " << c->getGlobalNum() << endl;
  }
  
  nsoft = softconstraints.size();
  
  if (nunm > ncon+nsoft) {
    cerr << "NewtonFitterGSL::initialize: nunm=" << nunm << " > ncon+nsoft=" 
         << ncon << "+" << nsoft << endl;
  }
  
  idim = npar+ncon;
  
  ini_gsl_vector (x, idim);
  ini_gsl_vector (xold, idim);
  ini_gsl_vector (xbest, idim);
  ini_gsl_vector (dx, idim);
  ini_gsl_vector (dxscal, idim);
  ini_gsl_vector (grad, idim);
  ini_gsl_vector (y, idim);
  ini_gsl_vector (yscal, idim);
  ini_gsl_vector (perr, idim);
  ini_gsl_vector (v1, idim);
  ini_gsl_vector (v2, idim);
  ini_gsl_vector (Meval, idim);
  
  ini_gsl_matrix (M, idim, idim);
  ini_gsl_matrix (Mscal, idim, idim);
  ini_gsl_matrix (M1, idim, idim);
  ini_gsl_matrix (M2, idim, idim);
  ini_gsl_matrix (M3, idim, idim);
  ini_gsl_matrix (M4, idim, idim);
  ini_gsl_matrix (M5, idim, idim);
  ini_gsl_matrix (Mevec, idim, idim);
  ini_gsl_matrix (CC, idim, idim);
  ini_gsl_matrix (CC1, idim, idim);
  ini_gsl_matrix (CCinv, idim, idim);
  
  ini_gsl_permutation (permM, idim);
  
  if (ws && wsdim != idim) {
    gsl_eigen_symmv_free (ws); 
    ws = 0;
  }
  if (ws == 0) ws = gsl_eigen_symmv_alloc (idim); 
  wsdim = idim;
 
  return true;

}
  
double NewtonFitterGSL::calcChi2() {
  chi2 = 0;
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    chi2 += fo->getChi2();
  }
  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    chi2 += bsc->getChi2();
  }
  return chi2;
}

void NewtonFitterGSL::printMy (double M[], double y[], int idim) {
  for (int i = 0; i < idim; ++i) {
    cout << i << "  [ " << M[idim*i+0];
      for (int j = 1; j<idim; ++j) cout << ", " << M[idim*i+j];
      cout << "]  [" << y[i] << "]\n";
  }
}

int NewtonFitterGSL::getError() const {return ierr;}
double NewtonFitterGSL::getProbability() const {return fitprob;}
double NewtonFitterGSL::getChi2() const {return chi2;}
int NewtonFitterGSL::getDoF() const {return ncon+nsoft-nunm;}
int NewtonFitterGSL::getIterations() const {return nit;}

int NewtonFitterGSL::calcDx () {
    if (debug>1)cout << "entering calcDx" << endl;
    nitcalc++;
    // from x_(n+1) = x_n - y/y' = x_n - M^(-1)*y we have M*(x_n-x_(n+1)) = y, 
    // which we solve for dx = x_n-x_(n+1) and hence x_(n+1) = x_n-dx
  
    gsl_matrix_memcpy (M1, Mscal);
    
//     cout << "Complete system:\n";
//     printMy(Mscal, yscal, idim);
    
    int ifail = 0;
    
    int signum;
    int result = gsl_linalg_LU_decomp (M1, permM, &signum);
    if (debug>1)cout << "calcDx: gsl_linalg_LU_decomp result=" << result << endl;
    // Solve M1*dx = y
    ifail = gsl_linalg_LU_solve (M1, permM, yscal, dxscal);
    if (debug>1)cout << "calcDx: gsl_linalg_LU_solve result=" << ifail << endl;
    
    if (ifail != 0) {
      cerr << "NewtonFitter::calcDx: ifail from gsl_linalg_LU_solve=" << ifail << endl;
      return calcDxSVD ();
      return -1;
    }
    stepsize=std::abs(gsl_vector_get (dxscal, gsl_blas_idamax (dxscal)));

    // dx = dxscal*perr (component wise)
    gsl_vector_memcpy (dx, dxscal);
    gsl_vector_mul (dx, perr);

    // optimize scale
        
    gsl_vector_memcpy (xbest, xold);
    chi2best = chi2old;
    
    optimizeScale();
    
    if (scalebest < 0.01) {
      if (debug > 1) cout << "NewtonFitter::calcDx: reverting to calcDxSVD\n";
      return calcDxSVD ();
    }
        
    return 0;
}

int NewtonFitterGSL::calcDxSVD () {
    //cout << "entering calcDxSVD" << endl;

    nitsvd++;
    // from x_(n+1) = x_n - y/y' = x_n - M^(-1)*y we have M*(x_n-x_(n+1)) = y, 
    // which we solve for dx = x_n-x_(n+1) and hence x_(n+1) = x_n-dx
  
    for (unsigned int i = 0; i < idim; ++i) assert (gsl_vector_get (perr, i) > 0);
        
//     cout << "Complete system:\n";
//     printMy(M, y, idim);
     // Get eigenvalues and eigenvectors of Mscal
     int ierr=0;
     gsl_matrix_memcpy (M1, Mscal);
     if (debug > 3) cout << "NewtonFitterGSL::calcDxSVD: Calling gsl_eigen_symmv" << endl;
     ierr = gsl_eigen_symmv (M1, Meval, Mevec, ws); 
     if (debug > 3) cout << "NewtonFitterGSL::calcDxSVD: result of gsl_eigen_symmv: " << ierr << endl;
     if (ierr != 0) {
       cerr << "NewtonFitter::calcDxSVD: ierr=" << ierr << "from gsl_eigen_symmv!\n";
     }
     // Sort the eigenvalues and eigenvectors in descending order in magnitude
     ierr = gsl_eigen_symmv_sort (Meval, Mevec, GSL_EIGEN_SORT_ABS_DESC);
     if (ierr != 0) {
       cerr << "NewtonFitter::calcDxSVD: ierr=" << ierr << "from gsl_eigen_symmv_sort!\n";
     }
     
     
     // The eigenvectors are stored in the columns of Mevec;
     // the eigenvectors are orthonormal, therefore Mevec^T = Mevec^-1
     // Therefore, Mscal = Mevec * diag(Meval)* Mevec^T, and
     // Mscal^-1 = (Mevec^T)^-1 * diag(Meval)^-1 * Mevec^-1 
     //       =  Mevec * diag(Meval)^-1 * Mevec^T
     // So, the solution of M*dx = y is given by
     // dx = M^-1 * y = Mevec * diag(Meval)^-1 * Mevec^-1 *y
     //    = Mevec * v2
     // For the pseudoinverse, the last elements of Meveal^-1 are set
     // to 0, therefore the last elements of v2 will be 0, 
     // therefore we can restrict the calculation of Mevec * v2
     // to the first ndim rows.
     // So, we calculate v2 only once, with only the inverse of zero eigenvalues
     // set to 0, and then calculate Mevec * v2 for fewer and fewer rows
   
//    cout << "calcDxSVD: info = " << info << endl;
//    cout << " s = ";
//    for (int i = 0; i < idim; ++i) cout << s[i] << ", ";
//    cout << endl;
   
   // Now M = U * s * V^T
   // We want to solve M*dx = y, hence dx = V * s^-1 * U^T * y
   // Calculate UTy first; we need only the first ndim entries
   unsigned int ndim = 0;
   while (ndim<idim && gsl_vector_get (Meval, ndim) != 0) ++ndim;
   
   if (ndim < idim) {
     cout << "calcDxSVD: idim = " << idim << " > ndim = " << ndim << endl;
   }
   //  cout << "calcDxSVD: idim = " << idim << " , ndim = " << ndim << endl;
   //  cout << " Meval = ";
   //  for (unsigned int i = 0; i < idim; ++i) cout << gsl_vector_get (Meval, i) << ", ";
   //  cout << endl;
   
   
   // Calculate v2 = 1*Mevec^T*y + 0*v2
   gsl_blas_dgemv (CblasTrans, 1, Mevec, yscal, 0, v2);
    
   // Divide by nonzero eigenvalues
   for (unsigned int i = 0; i<idim; ++i) {
     if (double e = gsl_vector_get (Meval, i)) gsl_vector_set (v2, i, gsl_vector_get (v2, i)/e);
     else gsl_vector_set (v2, i, 0);
   }
   
   stepsize = 0;
      
   do { 
     gsl_vector_view v2part = gsl_vector_subvector (v2, 0, ndim);
     gsl_matrix_view Mevecpart = gsl_matrix_submatrix (Mevec, 0, 0, idim, ndim);
     
     // Calculate dx = 1*Mevecpart^T*v2 + 0*dx
     gsl_blas_dgemv (CblasNoTrans, 1, &Mevecpart.matrix, &v2part.vector, 0, dxscal);
     // get maximum element
//      for (unsigned int i = 0; i < idim; ++i) {
//        if(std::abs(gsl_vector_get (dxscal, i))>stepsize) 
//          stepsize=std::abs(gsl_vector_get (dxscal, i));
//      }
     stepsize=std::abs(gsl_vector_get (dxscal, gsl_blas_idamax (dxscal)));
     
     // dx = dxscal*perr (component wise)
     gsl_vector_memcpy (dx, dxscal);
     gsl_vector_mul (dx, perr);

     if (debug>1) {
       cout << "calcDxSVD: Optimizing scale for ndim=" << ndim << endl;
       debug_print (dxscal, "dxscal");
     }
     
     optimizeScale();
     
     --ndim;
     
     if (debug>1 && (scalebest < 0.01 || ndim < idim-1)) {
       cout << "ndim=" << ndim << ", scalebest=" << scalebest << endl;
     }
   } 
   while (ndim > 0 && scalebest < 0.01);
       
   
    return 0;
}

void NewtonFitterGSL::ini_gsl_permutation (gsl_permutation *&p, unsigned int size) {
  if (p) {
    if (p->size != size) {
      gsl_permutation_free (p);
      if (size > 0) p = gsl_permutation_alloc (size);
      else p=0;
    }
  }
  else 
    if (size > 0) p = gsl_permutation_alloc (size);
}

void NewtonFitterGSL::ini_gsl_vector (gsl_vector *&v, unsigned int size) {
  
  if (v) {
    if (v->size != size) {
      gsl_vector_free (v);
      if (size>0) v = gsl_vector_alloc (size);
      else v=0;
    }
  }
  else 
    if (size > 0) v = gsl_vector_alloc (size);
}

void NewtonFitterGSL::ini_gsl_matrix (gsl_matrix *&m, unsigned int size1, unsigned int size2) {
  if (m) {
    if (m->size1 != size1 || m->size2 != size2) {
      gsl_matrix_free (m);
      if (size1*size2 > 0) m = gsl_matrix_alloc (size1, size2);
      else m=0;
    }
  }
  else 
    if (size1*size2 > 0) m = gsl_matrix_alloc (size1, size2);
}

void NewtonFitterGSL::debug_print (gsl_matrix *m, const char *name) {
  for (unsigned int  i = 0; i < m->size1; ++i) 
    for (unsigned int j = 0; j < m->size2; ++j)
      if (gsl_matrix_get (m, i, j) != 0)
        cout << name << "[" << i << "][" << j << "]=" << gsl_matrix_get (m, i, j) << endl;
}

void NewtonFitterGSL::debug_print (gsl_vector *v, const char *name) {
  for (unsigned int  i = 0; i < v->size; ++i) 
      if (gsl_vector_get (v, i) != 0)
        cout << name << "[" << i << "]=" << gsl_vector_get (v, i) << endl;
}

int NewtonFitterGSL::getNcon() const {return ncon;}
int NewtonFitterGSL::getNsoft() const {return nsoft;}
int NewtonFitterGSL::getNunm() const {return nunm;}
int NewtonFitterGSL::getNpar() const {return npar;}

bool NewtonFitterGSL::updateParams (gsl_vector *xnew) {
  assert (xnew);
  assert (xnew->size == idim);
  bool significant = false;
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    bool s = fo->updateParams (xnew->block->data, xnew->size);
    significant |=  s;
    if (debug > 1 && nit<nitdebug && s) {
      cout << "Significant update for FO " << i-fitobjects.begin() << " (" 
           << fo->getName() << ")\n";
    }
  }
  return significant;
}


void NewtonFitterGSL::fillxold() {
  assert (xold);
  assert (xold->size == idim);
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    for (int ilocal = 0; ilocal < fo->getNPar(); ++ilocal) {
      if (!fo->isParamFixed(ilocal)) {
        int iglobal = fo->getGlobalParNum (ilocal);
        assert (iglobal >= 0 && iglobal < npar);
        gsl_vector_set (xold, iglobal, fo->getParam (ilocal));
      }
    }
  }
  for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
    BaseHardConstraint *c = *i;
    assert (c);
    int iglobal = c->getGlobalNum ();
    assert (iglobal >= 0 && iglobal < (int)idim);
    gsl_vector_set (xold, iglobal, gsl_vector_get (x, iglobal));
  }
}

void NewtonFitterGSL::fillperr() {
  assert (perr);
  assert (perr->size == idim);
  gsl_vector_set_all (perr, 1);
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    for (int ilocal = 0; ilocal < fo->getNPar(); ++ilocal) {
      if (!fo->isParamFixed(ilocal)) {
        int iglobal = fo->getGlobalParNum (ilocal);
        assert (iglobal >= 0 && iglobal < npar);
        double e = std::abs(fo->getError (ilocal));
        gsl_vector_set (perr, iglobal, e ? e : 1);
      }
    }
  }
  for (ConstraintIterator i = constraints.begin(); i != constraints.end(); ++i) {
    BaseHardConstraint *c = *i;
    assert (c);
    int iglobal = c->getGlobalNum ();
    assert (iglobal >= 0 && iglobal < (int)idim);
    double e =  c->getError();
    gsl_vector_set (perr, iglobal, e ? 1/e : 1);
  }
}

int NewtonFitterGSL::calcM (bool errorpropagation) {
  assert (M);
  assert (M->size1 == idim && M->size2 == idim);
  
  gsl_matrix_set_zero (M);
  
  // First, all terms d^2 chi^2/dx1 dx2
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerMatrix (M->block->data, M->tda);
  }
  if (debug > 3) { 
    cout << "After adding covariances from fit objects:\n";
    //printMy ((double*) M, (double*) y, (int) idim);
    debug_print (M, "M");
  }
  
  // Second, all terms d^2 chi^2/dlambda dx, 
  // i.e. the first derivatives of the contraints,
  // plus the second derivatives times the lambda values
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->add1stDerivativesToMatrix (M->block->data, M->tda);
    if (debug > 3) { 
      cout << "After adding first derivatives of constraint " << c->getName() << endl;
      //printMy ((double*) M, (double*) y, (int) idim);
      debug_print (M, "M");
      cout << "errorpropagation = " << errorpropagation << endl;
    }
    // for error propagation after fit, 
    //2nd derivatives of constraints times lambda should _not_ be included!
    if (!errorpropagation) c->add2ndDerivativesToMatrix (M->block->data, M->tda, gsl_vector_get (x, kglobal));
  }
  if (debug > 3) { 
    cout << "After adding derivatives of constraints::\n";
    //printMy ((double*) M, (double*) y, (int) idim);
    debug_print (M, "M");
    cout << "===========================================::\n";
  }  
  
  
  // Finally, treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->add2ndDerivativesToMatrix (M->block->data, M->tda);
  }
  if (debug > 3) { 
    cout << "After adding soft constraints::\n";
    //printMy ((double*) M, (double*) y, (int) idim);
    debug_print (M, "M");
    cout << "===========================================::\n";
  }  

  // Rescale columns and rows by perr
  for (unsigned int i = 0; i < idim; ++i) 
    for (unsigned int j = 0; j < idim; ++j) 
      gsl_matrix_set (Mscal, i, j,  
        gsl_vector_get (perr, i)*gsl_vector_get (perr, j)*gsl_matrix_get (M, i, j));

  return 0;
}


int NewtonFitterGSL::calcy() {
  assert (y);
  assert (y->size == idim);
  gsl_vector_set_zero (y);
  // First, for the parameters
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerVector (y->block->data, y->size);
  }
//   cout << "After adding fo derivatives to y::\n";
//   printMy (M, y, idim);
  
  // Now add lambda*derivatives of constraints,
  // And finally, the derivatives w.r.t. to the constraints, i.e. the constraints themselves
  for (unsigned int k = 0; k < constraints.size(); ++k) {
    BaseHardConstraint *c = constraints[k];
    assert (c);
    int kglobal = c->getGlobalNum();
    assert (kglobal >= 0 && kglobal < (int)idim);
    c->addToGlobalChi2DerVector (y->block->data, y->size, gsl_vector_get (x, kglobal));
    gsl_vector_set (y, kglobal, c->getValue());
  }
  
    // Finally, treat the soft constraints

  for (SoftConstraintIterator i = softconstraints.begin(); i != softconstraints.end(); ++i) {
    BaseSoftConstraint *bsc = *i;
    assert (bsc);
    bsc->addToGlobalChi2DerVector (y->block->data, y->size);
  }
  gsl_vector_memcpy (yscal, y);
  gsl_vector_mul (yscal, perr);
  return 0;
}

int NewtonFitterGSL::optimizeScale () {
  updateParams (xold);
  calcy ();    
  if (debug > 1) {
    cout << "NewtonFitterGSL::optimizeScale" << endl;
    debug_print (xold, "xold");  
    debug_print (yscal, "yscal");  
    debug_print (dx, "dx");  
    debug_print (dxscal, "dxscal");  
  }
  scalevals[0] = 0;
  fvals[0] = 0.5*pow (gsl_blas_dnrm2 (yscal), 2);
  if (debug > 1) {
    cout << "NewtonFitterGSL::optimizeScale: fvals[0] = " << fvals[0] << endl;
  }
  // -dx is a direction
  // we want to minimize f=0.5*|y|^2 in that direction with y=grad chi2
  // The gradient grad f is given by grad f = d^chi^2/dxi dxj * dchi^2/dxj
  // = Mscal*yscal
  
  // Calculate grad = 1*Mscal*yscal + 0*grad
  gsl_blas_dgemv (CblasNoTrans, 1, Mscal, yscal, 0, grad);
  if (debug > 1) {
    debug_print (grad, "grad");  
  }
  
  // Code adapted from Numerical Recipies (3rd ed), page 479
  // routine lnsrch
  
  int nit = 0;
  
  static const double ALF = 1E-4;
  
  stepsize=std::abs(gsl_vector_get (dxscal, gsl_blas_idamax (dxscal)));
  static const double maxstepsize = 5;
  double scalefactor = maxstepsize/stepsize;
  if (stepsize > maxstepsize) {
    gsl_vector_scale (dxscal, maxstepsize/stepsize);
    if (debug > 2) {
      cout << "NewtonFitterGSL::optimizeScale: Rescaling dxscal by factor " << scalefactor << endl;
    }
    stepsize=std::abs(gsl_vector_get (dxscal, gsl_blas_idamax (dxscal)));
    if (debug > 1) {
      debug_print (dxscal, "dxscal");  
    }
  }
  
  double slope;
  gsl_blas_ddot (dxscal, grad, &slope);
  slope *= -1;
  if (debug > 2) {
    cout << "NewtonFitterGSL::optimizeScale: slope=" << slope 
         << ", 2*fvals[0]*factor=" << 2*fvals[0]*scalefactor
         << endl;
  }
    
  scale = 1;
  double tmpscale, scaleold = 1;
  do {
    // sets x = xold
    gsl_vector_memcpy (x, xold);
    // x = scale*dx + x
    if (debug > 1) {
      debug_print (x, "x(1)");  
    }
    gsl_blas_daxpy (-scale, dx, x);
    if (debug > 1) {
      debug_print (x, "x(2)");  
    }

    updateParams (x);
    calcy ();    
    if (debug > 1) {
      debug_print (x, "x(3)");  
      debug_print (yscal, "yscal");  
    }
    ++nit;
    scalevals[nit] = scale;
    fvals[nit] = 0.5*pow (gsl_blas_dnrm2 (yscal), 2);
    
    chi2new = calcChi2();
    
    
//    if (chi2new <= chi2best && fvals[nit] <= fvalbest) {
//    if ((fvals[nit] < fvalbest && chi2new <= chi2best) ||
//        (fvals[nit] < 1E-4 && chi2new < chi2best)) {
    if ((fvals[nit] < fvalbest)) {
      if (debug > 3) {
        cout << "new best value: "
             << "  scale " << scalevals[nit] << " -> |y|^2 = " << fvals[nit] 
             << ", chi2=" << chi2new << ", old best chi2: " << chi2best << endl;      
      }
      gsl_vector_memcpy (xbest, x);
      chi2best = chi2new;
      fvalbest = fvals[nit];
      scalebest = scale;
      stepbest = scale*stepsize;
    }
    
    if (fvals[nit] < fvals[0] + ALF*scale*slope) break;
    if (nit == 1) {
      tmpscale = -slope/(2*(fvals[nit] - fvals[0] - slope));
      if (debug > 3) cout << "quadratic estimate for best scale: " << tmpscale << endl;
    }
    else {
      double rhs1 = fvals[nit] - fvals[0]-scale*slope;
      double rhs2 = fvals[nit-1] - fvals[0]-scaleold*slope;
      double a = (rhs1/(scale*scale)-rhs2/(scaleold*scaleold))/(scale-scaleold);
      double b = (-scaleold*rhs1/(scale*scale)+scale*rhs2/(scaleold*scaleold))/(scale-scaleold);
      if (a==0) tmpscale = -slope/(2*b);
      else {
        double disc = b*b-3*a*slope;
        if (disc < 0)    tmpscale = 0.5*scale;
        else if (b <= 0) tmpscale = (-b + sqrt(disc))/(3*a);
        else             tmpscale = -slope / (b+sqrt(disc));
      }
      if (debug > 3) cout << "cubic estimate for best scale: " << tmpscale << endl;
      if (tmpscale > 0.5*scale) tmpscale = 0.5*scale;
    }
    scaleold = scale;
    scale = (tmpscale < 0.1*scale) ? 0.1*scale : tmpscale;
    if (debug > 1) cout << "New scale: " << scale << endl;
    
  } while (nit < NITMAX && scale > 0.0001);

  if (debug > 1) {
    for (int it = 0; it <= nit; ++it) {
      cout << "  scale " << scalevals[it] << " -> |y|^2 = " << fvals[it] 
      << " should be " << fvals[0] + ALF*scale*slope << endl;
    }
  }      
  return chi2best < chi2old;
}        
  
int NewtonFitterGSL::invertM() {
    gsl_matrix_memcpy (M1, M);
    
    int ifail = 0;
    
    int signum;
    // Calculate LU decomposition of M into M1
    int result = gsl_linalg_LU_decomp (M1, permM, &signum);
    if (debug>1)cout << "invertM: gsl_linalg_LU_decomp result=" << result << endl;
    // Calculate inverse of M
    ifail = gsl_linalg_LU_invert (M1, permM, M);
    if (debug>1)cout << "invertM: gsl_linalg_LU_solve result=" << ifail << endl;
    
    if (ifail != 0) {
      cerr << "NewtonFitter::invert: ifail from gsl_linalg_LU_invert=" << ifail << endl;
    }
    return ifail;
}

void NewtonFitterGSL::setDebug (int debuglevel) {
  debug = debuglevel;
  //cout << "NewtonFitterGSL::setDebug: debug level set to " << debug << endl;
}


void NewtonFitterGSL::calcCovMatrix() {
  // Set up equation system M*dadeta + dydeta = 0
  // here, dadeta is d a / d eta, i.e. the derivatives of the fitted 
  // parameters a w.r.t. to the measured parameters eta,
  // and dydeta is the derivative of the set of equations
  // w.r.t eta, i.e. simply d^2 chi^2 / d a d eta.
  // Now, if chi2 = (a-eta)^T*Vinv((a-eta), we have simply
  // d^2 chi^2 / d a d eta = - d^2 chi^2 / d a d a
  // and can use the method addToGlobalChi2DerMatrix.
  
  gsl_matrix_set_zero (M1);
  gsl_matrix_set_zero (M2);
  // First, all terms d^2 chi^2/dx1 dx2
  for (FitObjectIterator i = fitobjects.begin(); i != fitobjects.end(); ++i) {
    BaseFitObject *fo = *i;
    assert (fo);
    fo->addToGlobalChi2DerMatrix (M1->block->data, M1->tda);
    fo->addToGlobCov (M2->block->data, M2->tda);
  }
  
  // multiply by -1  
  gsl_matrix_scale (M1, -1);
  
  // JL: dy/eta are the derivatives of the "objective function" with respect to the MEASURED parameters.
  // Since the soft constraints do not depend on the measured, but only on the fitted (!) parameters, 
  // dy/deta stays -1*M1 also in the presence of soft constraints!   
  gsl_matrix_view dydeta  = gsl_matrix_submatrix (M1, 0, 0, idim, npar);
  gsl_matrix_view Cov_eta = gsl_matrix_submatrix (M2, 0, 0, npar, npar);
  
  if (debug > 3) {
    cout << "NewtonFitterGSL::calcCovMatrix\n";
    debug_print (&dydeta.matrix, "dydeta");
    debug_print (&Cov_eta.matrix, "Cov_eta");\
  }  
  
  // JL: calculates d^2 chi^2 / dx1 dx2 + first (!) derivatives of hard & soft constraints, and the
  // second derivatives of the soft constraints times the values of the fitted parameters 
  // - all of the with respect to the FITTED parameters, therefore with soft constraints like in the fit itself.
  calcM (true);

  if (debug > 3) {
    debug_print (M, "M");
  }  
  
  // Now, solve M*dadeta = dydeta

  // Calculate LU decomposition of M into M3
  int signum;
  int result = gsl_linalg_LU_decomp (M, permM, &signum);

  if (debug > 3) {
    cout << "invertM: gsl_linalg_LU_decomp result=" << result << endl;
    debug_print (M, "M_LU"); 
  }  

  // Calculate inverse of M, store in M3
  int ifail = gsl_linalg_LU_invert (M, permM, M3);
  
  if (debug > 3) {
    cout << "invertM: gsl_linalg_LU_invert ifail=" << ifail << endl;
    debug_print (M3, "Minv");
  }

  // Calculate dadeta = M3*dydeta
  gsl_matrix_set_zero (M4);
  gsl_matrix_view dadeta   = gsl_matrix_submatrix (M4, 0, 0, idim, npar);

  if (debug > 3) {
    debug_print (&dadeta.matrix, "dadeta");
  }
  
  // dadeta = 1*M*dydeta + 0*dadeta
  gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1, M3, &dydeta.matrix, 0, &dadeta.matrix);
  
  
  // Now calculate Cov_a = dadeta*Cov_eta*dadeta^T

  // First, calculate M3 = Cov_eta*dadeta^T as 
  gsl_matrix_view M3part   = gsl_matrix_submatrix (M3, 0, 0, npar, idim);
  gsl_blas_dgemm (CblasNoTrans, CblasTrans, 1, &Cov_eta.matrix, &dadeta.matrix, 0, &M3part.matrix);
  // Now Cov_a = dadeta*M3part
  gsl_matrix_set_zero (M5);
  gsl_matrix_view  Cov_a = gsl_matrix_submatrix (M5, 0, 0, npar, npar);
  gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1, &dadeta.matrix, &M3part.matrix, 0, M5);
  gsl_matrix_memcpy(CCinv,M5);

  if (debug > 3) {
    debug_print (&Cov_a.matrix, "Cov_a");
    debug_print (CCinv, "full Cov from err prop");
    debug_print (M1, "uncorr Cov from err prop");
  }
  
    // Finally, copy covariance matrix
  if (cov && covDim != npar) {
    delete[] cov;
    cov = 0;
  }
  covDim = npar;
  if (!cov) cov = new double[covDim*covDim];
  for (int i = 0; i < covDim; ++i) {
    for (int j = 0; j < covDim; ++j) {
      cov[i*covDim+j] = gsl_matrix_get (&Cov_a.matrix, i, j);
    }
  }    
  covValid = true;
}

    
double NewtonFitterGSL::meritFunction(double mu) {
  double result = 0;
  switch (imerit) {
    case 1: // l1 penalty function, Nocedal&Wright Eq. (15.24)
      result = chi2;
      for (unsigned int k = 0; k < constraints.size(); ++k) {
        BaseHardConstraint *c = constraints[k];
        assert (c);
        int kglobal = c->getGlobalNum();
        assert (kglobal >= 0 && kglobal < (int)idim);
        result += mu*std::fabs (c->getValue());
      }
      break;
    default: assert (0);
  }
  return result;
}

double NewtonFitterGSL::meritFunctionDeriv(double mu) {
  double result = 0;
  switch (imerit) {
    case 1: 
      break;
    default: assert (0);
  }
  return result;
}
  

