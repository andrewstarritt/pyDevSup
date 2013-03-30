
/* python has its own ideas about which version to support */
#undef _POSIX_C_SOURCE
#undef _XOPEN_SOURCE

#include <Python.h>

#include <epicsVersion.h>
#include <assert.h>
#include <dbCommon.h>
#include <dbStaticLib.h>
#include <dbAccess.h>
#include <devSup.h>
#include <dbLink.h>
#include <recGbl.h>
#include <alarm.h>
#include <cantProceed.h>

typedef struct {
    dbCommon *precord;

    DBLINK *plink;

    PyObject *pyrecord;
    PyObject *support;
} pyDevice;

static long parse_link(dbCommon *prec, const char* src)
{
    PyObject *mod, *ret;
    pyDevice *priv = prec->dpvt;

    mod = PyImport_ImportModule("devsup.db");
    if(!mod)
        return -1;

    ret = PyObject_CallMethod(mod, "processLink", "ss", prec->name, src);
    Py_DECREF(mod);
    if(!ret)
        return -1;

    if(!PyArg_ParseTuple(ret, "OO", &priv->pyrecord, &priv->support)) {
        Py_DECREF(ret);
        return -1;
    }

    Py_INCREF(priv->pyrecord);
    Py_INCREF(priv->support);
    Py_DECREF(ret);

    return 0;
}

static long detach_common(dbCommon *prec)
{
    pyDevice *priv = prec->dpvt;
    long ret = 0;

    if(priv->support) {
        PyObject *sup = 0;
        sup = PyObject_CallMethod(priv->support, "detach", "O", priv->pyrecord);
        Py_DECREF(priv->support);
        priv->support = NULL;
        if(sup)
            Py_DECREF(sup);
        else
            ret = -1;
    }
    if(priv->pyrecord)
        Py_DECREF(priv->pyrecord);
    priv->pyrecord = NULL;

    return ret;
}

static long process_common(dbCommon *prec, int cause)
{
    pyDevice *priv = prec->dpvt;
    PyObject *ret;

    ret = PyObject_CallMethod(priv->support, "process", "Oi", priv->pyrecord, cause);
    if(!ret)
        return -1;
    Py_DECREF(ret);
    return 0;
}

static long init_record(dbCommon *prec)
{
    return 0;
}

static long init_record2(dbCommon *prec)
{
    return 2;
}

static long add_record(dbCommon *prec)
{
    pyDevice *priv = prec->dpvt;
    PyGILState_STATE pystate;
    long ret;

    fprintf(stderr, "%s: init_record\n", prec->name);

    if(!priv) {
        DBENTRY ent;

        dbInitEntry(pdbbase, &ent);

        /* find ourself */
        ret = dbFindRecord(&ent, prec->name);
        assert(ret==0); /* really shouldn't fail */

        ret = dbFindField(&ent, "INP");

        if(ret)
            ret = dbFindField(&ent, "OUT");

        if(ret) {
            fprintf(stderr, "%s: Unable to find INP/OUT\n", prec->name);
            recGblSetSevr(prec, BAD_SUB_ALARM, INVALID_ALARM);
            return 0;
        }

        if(ent.pflddes->field_type!=DBF_INLINK
                && ent.pflddes->field_type!=DBF_OUTLINK)
        {
            fprintf(stderr, "%s: INP/OUT has unacceptible type %d\n",
                    prec->name, ent.pflddes->field_type);
            recGblSetSevr(prec, BAD_SUB_ALARM, INVALID_ALARM);
            return 0;
        }

        priv = callocMustSucceed(1, sizeof(*priv), "init_record");
        priv->precord = prec;
        priv->plink = ent.pfield;

        if(priv->plink->type != INST_IO) {
            fprintf(stderr, "%s: Has invalid link type %d\n", prec->name, priv->plink->type);
            recGblSetSevr(prec, BAD_SUB_ALARM, INVALID_ALARM);
            free(priv);
            return 0;
        }

        dbFinishEntry(&ent);
        prec->dpvt = priv;
    }

    assert(priv);
    assert(priv->plink->type == INST_IO);

    {
        char *msg=priv->plink->value.instio.string;
        if(!msg || *msg=='\0') {
            fprintf(stderr, "%s: Empty link string\n", prec->name);
            recGblSetSevr(prec, BAD_SUB_ALARM, INVALID_ALARM);
            return 0;
        }
    }

    pystate = PyGILState_Ensure();

    if(parse_link(prec, priv->plink->value.instio.string)) {
        fprintf(stderr, "%s: Exception in add_record\n", prec->name);
        PyErr_Print();
        PyErr_Clear();
        ret = S_db_errArg;
        goto done;
    }
    assert(priv->support);
    ret = 0;
done:
    PyGILState_Release(pystate);
    return ret;
}

static long del_record(dbCommon *prec)
{
    pyDevice *priv=prec->dpvt;
    PyGILState_STATE pystate;

    fprintf(stderr, "%s: del_record.  priv=%p\n", prec->name, priv);

    if(!priv)
        return 0;

    pystate = PyGILState_Ensure();

    if(detach_common(prec)) {
        fprintf(stderr, "%s: Exception in del_record\n", prec->name);
        PyErr_Print();
        PyErr_Clear();
    }

    assert(!priv->support);

    PyGILState_Release(pystate);
    return 0;
}

static long process_record(dbCommon *prec)
{
    pyDevice *priv = prec->dpvt;
    PyGILState_STATE pystate;

    if(!priv || !priv->support)
        return 0;
    pystate = PyGILState_Ensure();

    if(process_common(prec, 0)) {
        fprintf(stderr, "%s: Exception in process_record\n", prec->name);
        PyErr_Print();
        PyErr_Clear();
    }

    PyGILState_Release(pystate);
    return 0;
}

static dsxt pydevsupExt = {&add_record, &del_record};


static long init(int i)
{
    if(i==0)
        devExtend(&pydevsupExt);
    return 0;
}


typedef struct {
    dset com;
    DEVSUPFUN proc;
} dset5;

static dset5 pydevsupCom = {{5, NULL, (DEVSUPFUN)&init, (DEVSUPFUN)&init_record, NULL}, (DEVSUPFUN)&process_record};
static dset5 pydevsupCom2 = {{5, NULL, (DEVSUPFUN)&init, (DEVSUPFUN)&init_record2, NULL}, (DEVSUPFUN)&process_record};

#include <epicsExport.h>

epicsExportAddress(dset, pydevsupCom);
epicsExportAddress(dset, pydevsupCom2);
