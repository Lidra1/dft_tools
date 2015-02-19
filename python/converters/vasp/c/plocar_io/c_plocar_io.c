
#include <Python.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION

#include <numpy/arrayobject.h>
#include <complex.h>
#include <string.h>

#define MAX_STR_LEN 512

static int verbose = 0;

typedef struct {
  int nion;
  int ns;
  int nk;
  int nb;
  int nlmmax;
  int nc_flag;
  int isdouble;
} t_params;

static PyObject* io_read_plocar(PyObject *self, PyObject *args);
PyObject* create_par_dictionary(t_params* p);
PyArrayObject* create_plo_array(t_params* p);
PyArrayObject* create_ferw_array(t_params* p);
int read_arrays(FILE* fh, t_params* p, PyArrayObject* py_plo, PyArrayObject* py_ferw);

// Python module descriptor
static PyMethodDef c_plocar_io[] = {
  {"read_plocar", io_read_plocar, METH_VARARGS,
      "Reads from PLOCAR and returns PLOs"},
  {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
initc_plocar_io(void)
{
  (void) Py_InitModule("c_plocar_io", c_plocar_io);
  import_array();
}

/*
   Main function.

   Reads data from the specified file (default is 'PLOCAR')
and returns it as a Python tuple.

*/
static PyObject *
io_read_plocar(PyObject *self, PyObject *args)
{
  PyArrayObject *py_plo = NULL;
  PyArrayObject *py_ferw = NULL;
  PyObject *par_dict = NULL;
  PyObject *ret_tuple = NULL;

  char *fname = "PLOCAR";
  char errmsg[MAX_STR_LEN] = {"\0"};

  FILE* fh;
  
  int prec;
  t_params params;

  if(!PyArg_ParseTuple(args, "|s", &fname))
    return NULL;

  if(verbose)
    printf("  Reading PLO data from file: %s\n", fname);

//
// Read the header
//
  fh = fopen(fname, "r");
  if(fh == NULL) {
// Treat this error separately because no clean-up is necessary
    snprintf(errmsg, MAX_STR_LEN, "Error opening %s\n%s", fname, strerror(errno));
    PyErr_SetString(PyExc_IOError, errmsg);
    return NULL;
  } 

  if(!fread(&prec, 4, 1, fh)) goto ioerror;
  if(!fread(&params.nion, 4, 1, fh)) goto ioerror;
  if(!fread(&params.ns, 4, 1, fh)) goto ioerror;
  if(!fread(&params.nk, 4, 1, fh)) goto ioerror;
  if(!fread(&params.nb, 4, 1, fh)) goto ioerror;
  if(!fread(&params.nlmmax, 4, 1, fh)) goto ioerror;
  if(!fread(&params.nc_flag, 4, 1, fh)) goto ioerror;
 
  switch(prec) {
    case 8:
      params.isdouble = 1;
      if(verbose) printf("  Data in double precision\n");
      break;
    case 4:
      params.isdouble = 0;
      if(verbose) printf("  Data in single precision\n");
      break;
    default:
      PyErr_SetString(PyExc_ValueError, 
         "Error reading PLOCAR: only 'prec = 4, 8' are supported");
      goto error;
  }

  if(verbose) {
    printf("  nion: %d\n", params.nion);
    printf("  ns: %d\n", params.ns);
    printf("  nk: %d\n", params.nk);
    printf("  nb: %d\n", params.nb);
    printf("  nlmmax: %d\n", params.nlmmax);
    printf("  nc_flag: %d\n", params.nc_flag);
  }

//
// Create parameter dictionary
//
  par_dict = create_par_dictionary(&params);

//
// Create PLO and Fermi-weight arrays
//
  py_plo = create_plo_array(&params);
  py_ferw = create_ferw_array(&params);

//
// Read the data from file
//
  if(read_arrays(fh, &params, py_plo, py_ferw)) goto ioerror;

//
// Create return tuple
//
  ret_tuple = PyTuple_New(3);

  if(PyTuple_SetItem(ret_tuple, 0, par_dict) < 0) {
    PyErr_SetString(PyExc_ValueError, 
       "Error adding element to the return tuple (parameter dictionary)");
    goto error;
  }
    
  if(PyTuple_SetItem(ret_tuple, 1, (PyObject *)py_plo) < 0) {
    PyErr_SetString(PyExc_ValueError, 
       "Error adding element to the return tuple (PLO array)");
    goto error;
  }
 
  if(PyTuple_SetItem(ret_tuple, 2, (PyObject *)py_ferw) < 0) {
    PyErr_SetString(PyExc_ValueError, 
       "Error adding element to the return tuple (Fermi-weight array)");
    goto error;
  }
 //  Py_DECREF(par_dict);

  fclose(fh);

  return ret_tuple;

//
// Handle IO-errors
//
ioerror:
  if(feof(fh)) {
    snprintf(errmsg, MAX_STR_LEN, "End-of-file reading %s", fname);
    PyErr_SetString(PyExc_IOError, errmsg);
  }
  else {
    snprintf(errmsg, MAX_STR_LEN, "Error reading %s: %s", fname, strerror(errno));
    PyErr_SetString(PyExc_IOError, errmsg);
  }

//
// Clean-up after an error
//
error:
  fclose(fh);

  Py_XDECREF(par_dict);
  Py_XDECREF(py_plo);
  Py_XDECREF(py_ferw);
  Py_XDECREF(ret_tuple);

  return NULL;
}

//
// Auxiliary functions
//
PyObject*
create_par_dictionary(t_params* p)
{
  PyObject *par_dict = PyDict_New();
  PyDict_SetItemString(par_dict, "nion", PyInt_FromLong((long)p->nion));
  PyDict_SetItemString(par_dict, "ns", PyInt_FromLong((long)p->ns));
  PyDict_SetItemString(par_dict, "nk", PyInt_FromLong((long)p->nk));
  PyDict_SetItemString(par_dict, "nb", PyInt_FromLong((long)p->nb));
  PyDict_SetItemString(par_dict, "nc_flag", PyInt_FromLong((long)p->nc_flag));

  return par_dict;
}

PyArrayObject*
create_plo_array(t_params* p)
{
  double complex *plo;
  npy_intp *dims;
  int ntot = p->nion * p->ns * p->nk * p->nb * p->nlmmax;
  int ndim = 5;

  plo = (double complex*)malloc(ntot * sizeof(double complex));
  memset(plo, 0, ntot * sizeof(double complex));
  dims = (npy_intp *)malloc(ndim * sizeof(npy_intp));

  dims[0] = p->nion;
  dims[1] = p->ns;
  dims[2] = p->nk;
  dims[3] = p->nb;
  dims[4] = p->nlmmax;

  return (PyArrayObject *)PyArray_SimpleNewFromData(ndim, dims, NPY_CDOUBLE, plo);
}

PyArrayObject*
create_ferw_array(t_params* p)
{
  double *ferw;
  npy_intp *dims;
  int ntot = p->nion * p->ns * p->nk * p->nb;
  int ndim = 4;

  ferw = (double *)malloc(ntot * sizeof(double));
  memset(ferw, 0, ntot * sizeof(double));
  dims = (npy_intp *)malloc(ndim * sizeof(npy_intp));

  dims[0] = p->nion;
  dims[1] = p->ns;
  dims[2] = p->nk;
  dims[3] = p->nb;

  return (PyArrayObject *)PyArray_SimpleNewFromData(ndim, dims, NPY_DOUBLE, ferw);
}
 
int read_arrays(FILE* fh, t_params* p, PyArrayObject* py_plo, PyArrayObject* py_ferw)
{
  double complex *plo;
  double *ferw;
  npy_intp idx5[5], idx4[4];

  int ion, ik, ib, is, ilm;
  int nlm;
  float rtmp;
  float complex rbuf[50]; 
  double dtmp;
  double complex dbuf[50]; 

  for(ion = 0; ion < p->nion; ion++) {
    if(fread(&nlm, 4, 1, fh) < 1) goto error;
//    printf("  nlm = %d\n", nlm);
    for(is = 0; is < p->ns; is++)
      for(ik = 0; ik < p->nk; ik++)
        for(ib = 0; ib < p->nb; ib++) {
// Get the pointers to corresponding elements according to the new API
          idx5[0] = ion;
          idx5[1] = is;
          idx5[2] = ik;
          idx5[3] = ib;
          idx5[4] = 0;
          plo = (double complex *)PyArray_GetPtr(py_plo, idx5);

          idx4[0] = ion;
          idx4[1] = is;
          idx4[2] = ik;
          idx4[3] = ib;
          ferw = (double *)PyArray_GetPtr(py_ferw, idx4);

          if(p->isdouble) {
            if(fread(&dtmp, sizeof(double), 1, fh) < 1) goto error;
            if(fread(dbuf, sizeof(double complex), nlm, fh) < nlm) goto error;
 
            ferw[0] = dtmp;
//            printf("%5d %5d %5d %5d %lf\n", ion, is, ik, ib, dtmp);
            memcpy(plo, dbuf, nlm * sizeof(double complex));
          }
          else {
            if(fread(&rtmp, sizeof(float), 1, fh) < 1) goto error;
            if(fread(rbuf, sizeof(float complex), nlm, fh) < nlm) goto error;
 
            ferw[0] = (double)rtmp;
//            printf("%5d %5d %5d %5d %f\n", ion, is, ik, ib, rtmp);
// In this case destination and source arrays are not compatible,
// we have to copy element-wise
            for(ilm = 0; ilm < nlm; ilm++) {
              plo[ilm] = (double complex)rbuf[ilm];
//              printf("%5d %5d %f\n", ilm, ind2 + ilm, rbuf[ilm]);
            }
          } // if p->isdouble

        }
  }
  
  return 0;

error:
  return -1;
}
