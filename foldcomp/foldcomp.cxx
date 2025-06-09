#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <sstream> // IWYU pragma: keep

// These would be your actual project headers
#include "atom_coordinate.h" // Defines AtomCoordinate and float3d
#include "foldcomp.h"        // Defines Foldcomp class and DEFAULT_ANCHOR_THRESHOLD
#include "database_reader.h" // Defines reader_*, free_reader, make_reader, etc.

static PyObject *FoldcompError;

typedef struct {
    PyObject_HEAD
    std::vector<int64_t>* user_indices;
    bool decompress;
    void* memory_handle;
} FoldcompDatabaseObject;

// Forward declaration for the C++ decompress function (original, leads to PDB)
int decompress(const char* input, size_t input_size, bool use_alt_order, std::ostream& oss, std::string& name);
static PyObject* FoldcompDatabase_close(PyObject* self);
static PyObject* FoldcompDatabase_enter(PyObject* self);
static PyObject* FoldcompDatabase_exit(PyObject* self, PyObject* args);
// PyObject* vectorToList_Int64(const std::vector<int64_t>& data); // Already present

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wcast-function-type"
static PyMethodDef FoldcompDatabase_methods[] = {
    {"close", (PyCFunction)FoldcompDatabase_close, METH_NOARGS, "Close the database."},
    {"__enter__", (PyCFunction)FoldcompDatabase_enter, METH_NOARGS, "Enter the runtime context related to this object."},
    {"__exit__", (PyCFunction)FoldcompDatabase_exit, METH_VARARGS, "Exit the runtime context related to this object."},
    {NULL, NULL, 0, NULL} /* Sentinel */
};
#pragma GCC diagnostic pop

// FoldcompDatabase_sq_length
static Py_ssize_t FoldcompDatabase_sq_length(PyObject* self) {
    FoldcompDatabaseObject* db = (FoldcompDatabaseObject*)self;
    if (db->user_indices != NULL) {
        return db->user_indices->size();
    }
    return (Py_ssize_t)reader_get_size(db->memory_handle);
}

// FoldcompDatabase_sq_item
static PyObject* FoldcompDatabase_sq_item(PyObject* self, Py_ssize_t index) {
    FoldcompDatabaseObject* db = (FoldcompDatabaseObject*)self;

    const char* data_ptr; // Renamed from 'data' to avoid conflict
    size_t length;
    int64_t id;
    if (db->user_indices != NULL) {
        if (index >= (Py_ssize_t)db->user_indices->size() || index < 0) { // Added lower bound check
            PyErr_SetString(PyExc_IndexError, "index out of range");
            return NULL;
        }
        id = db->user_indices->at(index);
        data_ptr = reader_get_data(db->memory_handle, id);
        length = std::max(reader_get_length(db->memory_handle, id), (int64_t)1) - (int64_t)1;
    } else {
        if (index >= (Py_ssize_t)reader_get_size(db->memory_handle) || index < 0) { // Added lower bound check
            PyErr_SetString(PyExc_IndexError, "index out of range");
            return NULL;
        }
        data_ptr = reader_get_data(db->memory_handle, index);
        length = std::max(reader_get_length(db->memory_handle, index), (int64_t)1) - (int64_t)1;
    }

    if (data_ptr == NULL && length == (size_t)-1) { // Check if reader_get_data failed
        PyErr_SetString(FoldcompError, "Failed to retrieve data from database.");
        return NULL;
    }


    if (db->decompress) {
        std::ostringstream oss;
        std::string name_str; // Renamed from 'name'
        // The C++ decompress function internally calls Foldcomp::decompress and then writeAtomCoordinatesToPDB
        int err = decompress(data_ptr, length, false, oss, name_str);
        if (err != 0) {
            std::string err_msg = "Error decompressing entry ID " + std::to_string(id) + ": " + name_str;
            PyErr_SetString(FoldcompError, err_msg.c_str());
            return NULL;
        }
        PyObject* pdb = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, oss.str().c_str(), oss.str().size());
        if (!pdb) { // Check for PyUnicode_FromKindAndData failure
             PyErr_SetString(FoldcompError, "Failed to create PDB string object.");
             return NULL;
        }
        PyObject* result = Py_BuildValue("(sO)", name_str.c_str(), pdb); // Removed comma after s
        Py_DECREF(pdb);
        return result;
    }
    return PyBytes_FromStringAndSize(data_ptr, length);
}

// PySequenceMethods
static PySequenceMethods FoldcompDatabase_as_sequence = {
    &FoldcompDatabase_sq_length, // sq_length
    0, // sq_concat
    0, // sq_repeat
    &FoldcompDatabase_sq_item, // sq_item
    0, // sq_slice
    0, // sq_ass_item
    0, // sq_ass_slice
    0, // sq_contains
    0, // sq_inplace_concat
    0, // sq_inplace_repeat
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static PyTypeObject FoldcompDatabaseType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "foldcomp.FoldcompDatabase",    /* tp_name */
    sizeof(FoldcompDatabaseObject), /* tp_basicsize */
    0,                         /* tp_itemsize */
    0,                         /* tp_dealloc */ // Should be defined to free user_indices and memory_handle
    0,                         /* tp_vectorcall_offset */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_as_async */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    &FoldcompDatabase_as_sequence, /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "FoldcompDatabase objects", /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */ // Could implement standard iterator methods
    0,                         /* tp_iternext */
    FoldcompDatabase_methods,  /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */ // Should be defined
    0,                         /* tp_alloc */
    PyType_GenericNew,         /* tp_new */ // Standard new
    0,                         /* tp_free */ // PyObject_GC_Del if GC is used
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
    0,                         /* tp_del */
    0,                         /* tp_version_tag */
    0,                         /* tp_finalize */
    //0,                         /* tp_vectorcall */ // For newer Python versions
};
#pragma GCC diagnostic pop

// FoldcompDatabase_close
static PyObject* FoldcompDatabase_close(PyObject* self) {
    if (!PyObject_TypeCheck(self, &FoldcompDatabaseType)) {
        PyErr_SetString(PyExc_TypeError, "Expected FoldcompDatabase object.");
        return NULL;
    }
    FoldcompDatabaseObject* db = (FoldcompDatabaseObject*)self;
    if (db->memory_handle != NULL) {
        free_reader(db->memory_handle);
        db->memory_handle = NULL;
    }
    if (db->user_indices != NULL) {
        delete db->user_indices; // Clean up the vector
        db->user_indices = NULL;
    }
    Py_RETURN_NONE;
}
// Proper dealloc for FoldcompDatabaseObject
static void FoldcompDatabase_dealloc(FoldcompDatabaseObject* self) {
    FoldcompDatabase_close((PyObject*)self); // Call close to free resources
    Py_TYPE(self)->tp_free((PyObject*)self); // Standard deallocation
}


// FoldcompDatabase_enter
static PyObject* FoldcompDatabase_enter(PyObject* self) {
    // No specific resource acquisition needed beyond what __init__ (or foldcomp_open) does
    Py_INCREF(self);
    return self;
}

// FoldcompDatabase_exit
static PyObject *FoldcompDatabase_exit(PyObject *self, PyObject* /* args */) {
    // args typically contains exc_type, exc_value, traceback
    return FoldcompDatabase_close(self); // Ensure resources are released
}

// https://stackoverflow.com/questions/1448467/initializing-a-c-stdistringstream-from-an-in-memory-buffer/1449527
struct OneShotReadBuf : public std::streambuf
{
    OneShotReadBuf(char* s, std::size_t n)
    {
        setg(s, s, s + n); // Set read pointers: beginning, current, end
    }
};

// C++ decompress function (original, leads to PDB)
// This function is called by FoldcompDatabase_sq_item when db->decompress is true
// and by the Python binding foldcomp_decompress
int decompress(const char* input, size_t input_size, bool use_alt_order, std::ostream& oss, std::string& name) {
    OneShotReadBuf buf((char*)input, input_size);
    std::istream istr(&buf);

    // Suppress cout from Foldcomp internals
    std::streambuf* orig_cout_rdbuf = std::cout.rdbuf();
    std::ostringstream dev_null_stream; // Redirect cout to a dummy stream
    std::cout.rdbuf(dev_null_stream.rdbuf());

    Foldcomp compRes;
    int flag = compRes.read(istr);
    if (flag != 0) {
        std::cout.rdbuf(orig_cout_rdbuf); // Restore cout
        // name might not be set yet, or might contain partial data if read failed.
        // Consider how to report error if compRes.strTitle is not valid.
        name = "Error reading compressed data, title unknown"; // Fallback name
        return 1; // Error reading
    }
    std::vector<AtomCoordinate> atomCoordinates;
    compRes.useAltAtomOrder = use_alt_order;
    flag = compRes.decompress(atomCoordinates);
    
    std::cout.rdbuf(orig_cout_rdbuf); // Restore cout

    if (flag != 0) {
        name = compRes.strTitle; // Title should be available even if decompress fails
        return 1; // Error decompressing
    }
    
    // Write decompressed data to PDB format into the output stream 'oss'
    writeAtomCoordinatesToPDB(atomCoordinates, compRes.strTitle, oss);
    name = compRes.strTitle;

    return 0; // Success
}

// Python binding for decompress (original, returns PDB string)
static PyObject *foldcomp_decompress(PyObject* /* self */, PyObject *args) {
    const char *strArg;
    Py_ssize_t strSize;
    if (!PyArg_ParseTuple(args, "y#", &strArg, &strSize)) { // "y#" for bytes object
        return NULL;
    }

    std::ostringstream oss;
    std::string name_str; // Renamed
    int err = decompress(strArg, strSize, false, oss, name_str);
    if (err != 0) {
        std::string error_message = "Error decompressing: " + name_str;
        PyErr_SetString(FoldcompError, error_message.c_str());
        return NULL;
    }
    PyObject* pdb_unicode = PyUnicode_FromString(oss.str().c_str());
    if (!pdb_unicode) {
        PyErr_SetString(FoldcompError, "Failed to create PDB unicode string.");
        return NULL;
    }
    PyObject* result = Py_BuildValue("(sO)", name_str.c_str(), pdb_unicode);
    Py_DECREF(pdb_unicode);
    return result;
}


// NEW Python binding: decompress_to_data
static PyObject *foldcomp_decompress_to_data(PyObject* /* self */, PyObject *args) {
    const char *strArg;
    Py_ssize_t strSize;
    if (!PyArg_ParseTuple(args, "y#", &strArg, &strSize)) { // "y#" for bytes object
        return NULL;
    }

    OneShotReadBuf buf((char*)strArg, strSize);
    std::istream istr(&buf);

    // Suppress cout from Foldcomp internals
    std::streambuf* orig_cout_rdbuf = std::cout.rdbuf();
    std::ostringstream dev_null_stream;
    std::cout.rdbuf(dev_null_stream.rdbuf());

    Foldcomp compRes;
    int flag = compRes.read(istr);
    if (flag != 0) {
        std::cout.rdbuf(orig_cout_rdbuf); // Restore cout
        PyErr_SetString(FoldcompError, "Error reading compressed data structure.");
        return NULL;
    }

    std::vector<AtomCoordinate> atomCoordinates;
    // compRes.useAltAtomOrder = false; // Set if needed, default is false
    flag = compRes.decompress(atomCoordinates);
    
    std::cout.rdbuf(orig_cout_rdbuf); // Restore cout

    if (flag != 0) {
        std::string error_message = "Error decompressing to atom coordinates for " + compRes.strTitle;
        PyErr_SetString(FoldcompError, error_message.c_str());
        return NULL;
    }

    // Build Python dictionary
    PyObject* resultDict = PyDict_New();
    if (!resultDict) {
        PyErr_SetString(PyExc_MemoryError, "Could not create result dictionary.");
        return NULL;
    }

    // Helper lambda for adding items to dict and handling errors
    auto addItemToDict = [&](const char* key, PyObject* value) -> bool {
        if (!value) { // If value creation failed
            Py_DECREF(resultDict); // Clean up dict
            return false;
        }
        if (PyDict_SetItemString(resultDict, key, value) < 0) {
            Py_DECREF(value); // Clean up value
            Py_DECREF(resultDict); // Clean up dict
            return false;
        }
        Py_DECREF(value); // PyDict_SetItemString increments ref count
        return true;
    };

    if (!addItemToDict("name", PyUnicode_FromString(compRes.strTitle.c_str()))) return NULL;
    if (!addItemToDict("sequence_1letter", PyUnicode_FromStringAndSize(compRes.residues.data(), compRes.residues.size()))) return NULL;

    size_t numAtoms = atomCoordinates.size();
    PyObject* pyCoordsList = PyList_New(numAtoms);
    PyObject* pyAtomNamesList = PyList_New(numAtoms);
    PyObject* pyResidueNamesList = PyList_New(numAtoms);
    PyObject* pyResidueIndicesList = PyList_New(numAtoms);
    PyObject* pyAtomSerialNumbersList = PyList_New(numAtoms);
    PyObject* pyBFactorList = PyList_New(numAtoms);
    PyObject* pyChainIDsList = PyList_New(numAtoms);
    PyObject* pyOccupancyList = PyList_New(numAtoms);


    if (!pyCoordsList || !pyAtomNamesList || !pyResidueNamesList || !pyResidueIndicesList ||
        !pyAtomSerialNumbersList || !pyBFactorList || !pyChainIDsList || !pyOccupancyList) {
        Py_XDECREF(pyCoordsList); Py_XDECREF(pyAtomNamesList); Py_XDECREF(pyResidueNamesList);
        Py_XDECREF(pyResidueIndicesList); Py_XDECREF(pyAtomSerialNumbersList); Py_XDECREF(pyBFactorList);
        Py_XDECREF(pyChainIDsList); Py_XDECREF(pyOccupancyList);
        Py_DECREF(resultDict);
        PyErr_SetString(PyExc_MemoryError, "Could not create lists for atom data.");
        return NULL;
    }

    for (size_t i = 0; i < numAtoms; ++i) {
        const AtomCoordinate& ac = atomCoordinates[i];
        // Assuming AtomCoordinate has members: x, y, z, atom, residue, residue_index, atom_index, tempFactor, chain, occupancy

        PyObject* coordTuple = Py_BuildValue("(fff)", ac.x, ac.y, ac.z);
        PyList_SET_ITEM(pyCoordsList, i, coordTuple); // Steals reference to coordTuple

        PyList_SET_ITEM(pyAtomNamesList, i, PyUnicode_FromString(ac.atom.c_str()));
        PyList_SET_ITEM(pyResidueNamesList, i, PyUnicode_FromString(ac.residue.c_str()));
        PyList_SET_ITEM(pyResidueIndicesList, i, PyLong_FromLong(ac.residue_index));
        PyList_SET_ITEM(pyAtomSerialNumbersList, i, PyLong_FromLong(ac.atom_index));
        PyList_SET_ITEM(pyBFactorList, i, PyFloat_FromDouble(ac.tempFactor));
        PyList_SET_ITEM(pyChainIDsList, i, PyUnicode_FromString(ac.chain.c_str()));
        PyList_SET_ITEM(pyOccupancyList, i, PyFloat_FromDouble(ac.occupancy));


        if (PyErr_Occurred()) { // Check if any PyList_SET_ITEM or Py*_From* failed
            Py_DECREF(pyCoordsList); Py_DECREF(pyAtomNamesList); Py_DECREF(pyResidueNamesList);
            Py_DECREF(pyResidueIndicesList); Py_DECREF(pyAtomSerialNumbersList); Py_DECREF(pyBFactorList);
            Py_DECREF(pyChainIDsList); Py_DECREF(pyOccupancyList);
            Py_DECREF(resultDict);
            // Error already set by the failing Python C API call
            return NULL;
        }
    }

    if (!addItemToDict("atom_coords", pyCoordsList)) return NULL;
    if (!addItemToDict("atom_names", pyAtomNamesList)) return NULL;
    if (!addItemToDict("atom_residue_names_3letter", pyResidueNamesList)) return NULL;
    if (!addItemToDict("atom_residue_indices", pyResidueIndicesList)) return NULL;
    if (!addItemToDict("atom_serial_numbers", pyAtomSerialNumbersList)) return NULL;
    if (!addItemToDict("atom_b_factors", pyBFactorList)) return NULL;
    if (!addItemToDict("atom_chain_ids", pyChainIDsList)) return NULL;
    if (!addItemToDict("atom_occupancies", pyOccupancyList)) return NULL;

    return resultDict;
}


std::string trim(const std::string& str, const std::string& whitespace = " \t") {
    const std::string::size_type strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const std::string::size_type strEnd = str.find_last_not_of(whitespace);
    const std::string::size_type strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

// Compress C++ function (original)
int compress(const std::string& name, const std::string& pdb_input, std::ostream& oss, int anchor_residue_threshold) {
    std::vector<AtomCoordinate> atomCoordinates;
    std::istringstream iss(pdb_input);
    std::string line;
    std::string current_chain_id = ""; // Use current_chain_id to detect multiple chains

    for (int line_num = 1; std::getline(iss, line); ++line_num) {
        if (line.rfind("ATOM  ", 0) == 0 || line.rfind("HETATM", 0) == 0) { // Check prefix
            if (line.length() < 66) { // Basic check for line length
                 // Potentially log a warning or skip
                continue;
            }
            std::string parsed_chain_id = trim(line.substr(21, 1));
            if (current_chain_id == "") {
                current_chain_id = parsed_chain_id;
            } else if (parsed_chain_id != current_chain_id) {
                return 2; // FLAG 2: multiple chains
            }
            try {
                atomCoordinates.emplace_back(
                    trim(line.substr(12, 4)), // atom
                    trim(line.substr(17, 3)), // residue
                    parsed_chain_id,          // chain
                    std::stoi(trim(line.substr(6,  5))), // atom_index
                    std::stoi(trim(line.substr(22, 4))), // residue_index
                    std::stof(trim(line.substr(30, 8))), // x
                    std::stof(trim(line.substr(38, 8))), // y
                    std::stof(trim(line.substr(46, 8))), // z
                    std::stof(trim(line.substr(54, 6))), // occupancy
                    std::stof(trim(line.substr(60, 6)))  // tempFactor
                );
            } catch (const std::invalid_argument& ia) {
                // Handle error: PDB line format error (e.g. non-numeric value)
                // Optionally, log ia.what() and line_num
                return 3; // FLAG 3: PDB format error
            } catch (const std::out_of_range& oor) {
                // Handle error: numeric value out of range for std::sto*
                return 3; // FLAG 3: PDB format error
            }
        }
    }
    if (atomCoordinates.empty()) { // Changed from size() == 0
        return 1; // FLAG 1: no ATOM lines
    }

    removeAlternativePosition(atomCoordinates); // Assumed to be defined elsewhere

    Foldcomp compRes;
    compRes.strTitle = name;
    compRes.anchorThreshold = anchor_residue_threshold;
    int compress_flag = compRes.compress(atomCoordinates); // Capture return value
    if (compress_flag != 0) {
        return 4; // FLAG 4: Internal compression error
    }
    compRes.writeStream(oss);

    return 0; // Success
}

// Python binding for compress (original)
static PyObject *foldcomp_compress(PyObject* /* self */, PyObject *args, PyObject* kwargs) {
    const char* name_arg; // Renamed to avoid conflict
    const char* pdb_input_arg; // Renamed
    PyObject* anchor_residue_threshold_obj = NULL; // Renamed
    static const char *kwlist[] = {"name", "pdb_content", "anchor_residue_threshold", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|$O", const_cast<char**>(kwlist), 
                                     &name_arg, &pdb_input_arg, &anchor_residue_threshold_obj)) {
        return NULL;
    }

    if (anchor_residue_threshold_obj != NULL && !PyLong_Check(anchor_residue_threshold_obj)) {
        PyErr_SetString(PyExc_TypeError, "anchor_residue_threshold must be an integer");
        return NULL;
    }

    int threshold = DEFAULT_ANCHOR_THRESHOLD; // Assumed to be defined in foldcomp.h or elsewhere
    if (anchor_residue_threshold_obj != NULL) {
        threshold = PyLong_AsLong(anchor_residue_threshold_obj);
        if (PyErr_Occurred()) { // Check for error in PyLong_AsLong
            return NULL;
        }
    }

    std::ostringstream oss;
    int flag = compress(name_arg, pdb_input_arg, oss, threshold);
    if (flag == 1) {
        PyErr_SetString(FoldcompError, "No ATOM lines found in PDB content.");
        return NULL;
    } else if (flag == 2) {
        PyErr_SetString(FoldcompError, "Multiple chains found. Please provide a single chain."); // Simplified message
        return NULL;
    } else if (flag == 3) {
        PyErr_SetString(FoldcompError, "Invalid PDB format in input content.");
        return NULL;
    } else if (flag == 4) {
        PyErr_SetString(FoldcompError, "Internal error during compression process.");
        return NULL;
    } else if (flag != 0) { // Catch-all for other non-zero flags
        PyErr_SetString(FoldcompError, "Unknown error during compression.");
        return NULL;
    }
    
    std::string result_str = oss.str();
    return PyBytes_FromStringAndSize(result_str.c_str(), result_str.length());
}


// PyTypeObject* pathType = NULL; // This was unused

static PyObject *foldcomp_open(PyObject* /* self */, PyObject* args, PyObject* kwargs) {
    PyObject* path_obj; // Renamed
    PyObject* user_ids_obj = NULL; // Renamed
    PyObject* decompress_obj = NULL; // Renamed
    PyObject* err_on_missing_obj = NULL; // Renamed

    static const char *kwlist[] = {"path", "ids", "decompress", "err_on_missing", NULL};
    // PyUnicode_FSConverter converts path-like object to a PyBytes object (encoded filename)
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O&|$OOO", const_cast<char**>(kwlist), 
                                     PyUnicode_FSConverter, &path_obj, 
                                     &user_ids_obj, &decompress_obj, &err_on_missing_obj)) {
        return NULL; // PyUnicode_FSConverter DECREFs path_obj on error.
    }
    // path_obj is a PyBytes object now.
    
    const char* pathCStr = PyBytes_AS_STRING(path_obj); // No need to check for NULL if FSConverter succeeded
    
    if (user_ids_obj != NULL && !PyList_Check(user_ids_obj)) {
        Py_DECREF(path_obj);
        PyErr_SetString(PyExc_TypeError, "ids argument must be a list.");
        return NULL;
    }

    if (decompress_obj != NULL && !PyBool_Check(decompress_obj)) {
        Py_DECREF(path_obj);
        PyErr_SetString(PyExc_TypeError, "decompress argument must be a boolean.");
        return NULL;
    }

    if (err_on_missing_obj != NULL && !PyBool_Check(err_on_missing_obj)) {
        Py_DECREF(path_obj);
        PyErr_SetString(PyExc_TypeError, "err_on_missing argument must be a boolean.");
        return NULL;
    }

    std::string dbname_str(pathCStr);
    std::string index_str = dbname_str + ".index";
    Py_DECREF(path_obj); // Done with path_obj

    FoldcompDatabaseObject *db_obj = PyObject_New(FoldcompDatabaseObject, &FoldcompDatabaseType);
    if (db_obj == NULL) {
        // PyObject_New doesn't set Python error, so we do.
        PyErr_SetString(PyExc_MemoryError, "Could not allocate memory for FoldcompDatabaseObject");
        return NULL;
    }
    // Initialize members to known state
    db_obj->user_indices = NULL;
    db_obj->memory_handle = NULL;


    int mode = DB_READER_USE_DATA; // Assumed DB_READER_USE_DATA is defined
    if (user_ids_obj != NULL && PyList_Size(user_ids_obj) > 0) { // Use PyList_Size for lists
        mode |= DB_READER_USE_LOOKUP; // Assumed DB_READER_USE_LOOKUP is defined
    }

    db_obj->decompress = (decompress_obj == NULL || PyObject_IsTrue(decompress_obj));
    bool err_on_missing_flag = (err_on_missing_obj != NULL && PyObject_IsTrue(err_on_missing_obj));


    db_obj->memory_handle = make_reader(dbname_str.c_str(), index_str.c_str(), mode);
    if (db_obj->memory_handle == NULL) {
        Py_DECREF(db_obj); // PyObject_New was used, so DECREF for error
        PyErr_SetString(FoldcompError, ("Could not open database: " + dbname_str).c_str());
        return NULL;
    }


    if (user_ids_obj != NULL && PyList_Size(user_ids_obj) > 0) {
        Py_ssize_t id_count = PyList_Size(user_ids_obj);
        db_obj->user_indices = new (std::nothrow) std::vector<int64_t>(); // Use nothrow and check for NULL
        if (db_obj->user_indices == NULL) {
            free_reader(db_obj->memory_handle); // Clean up already acquired resource
            Py_DECREF(db_obj);
            PyErr_SetString(PyExc_MemoryError, "Could not allocate memory for user_indices vector.");
            return NULL;
        }
        db_obj->user_indices->reserve(id_count);

        for (Py_ssize_t i = 0; i < id_count; i++) {
            PyObject* item = PyList_GetItem(user_ids_obj, i); // GetItem borrows reference
            if (!item || !PyUnicode_Check(item)) { // Check item validity and type
                 // Error or skip? For now, let's be strict.
                delete db_obj->user_indices;
                db_obj->user_indices = NULL;
                free_reader(db_obj->memory_handle);
                Py_DECREF(db_obj);
                PyErr_SetString(PyExc_TypeError, "All items in 'ids' list must be strings.");
                return NULL;
            }
            const char* data_id_str = PyUnicode_AsUTF8(item);
            if (!data_id_str) { // PyUnicode_AsUTF8 failed
                delete db_obj->user_indices;
                db_obj->user_indices = NULL;
                free_reader(db_obj->memory_handle);
                Py_DECREF(db_obj);
                // PyUnicode_AsUTF8 sets an error
                return NULL;
            }

            uint32_t key = reader_lookup_entry(db_obj->memory_handle, data_id_str);
            int64_t looked_up_id = reader_get_id(db_obj->memory_handle, key);

            if (looked_up_id == -1 || key == UINT32_MAX) { // Check for lookup failure
                std::string err_msg = "Skipping entry '";
                err_msg += data_id_str;
                err_msg += "' which is not in the database.";
                if (err_on_missing_flag) {
                    delete db_obj->user_indices;
                    db_obj->user_indices = NULL;
                    free_reader(db_obj->memory_handle);
                    Py_DECREF(db_obj);
                    PyErr_SetString(PyExc_KeyError, err_msg.c_str());
                    return NULL;
                } else {
                    // Consider PySys_WriteStderr for warnings if appropriate
                    std::cerr << "Foldcomp Warning: " << err_msg << std::endl;
                    continue; 
                }
            }
            db_obj->user_indices->push_back(looked_up_id);
        }
    }

    return (PyObject*)db_obj;
}


// Module method definitions
static PyMethodDef FoldcompMethods[] = {
    {"decompress", foldcomp_decompress, METH_VARARGS, "Decompress FCZ data to PDB string."},
    {"compress", (PyCFunction)foldcomp_compress, METH_VARARGS | METH_KEYWORDS, "Compress PDB string to FCZ data."},
    {"open", (PyCFunction)foldcomp_open, METH_VARARGS | METH_KEYWORDS, "Open a Foldcomp database."},
    {"decompress_to_data", foldcomp_decompress_to_data, METH_VARARGS, "Decompress FCZ data to a Python dictionary of coordinates and other data."}, // NEW
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

// Module definition
static struct PyModuleDef foldcompmodule = {
    PyModuleDef_HEAD_INIT,
    "foldcomp",   /* name of module */
    "Foldcomp: Protein structure compression library", /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    FoldcompMethods
};

// Module initialization function
PyMODINIT_FUNC PyInit_foldcomp(void) {
    PyObject* m;

    // Initialize FoldcompDatabaseType
    FoldcompDatabaseType.tp_new = PyType_GenericNew;
    FoldcompDatabaseType.tp_dealloc = (destructor)FoldcompDatabase_dealloc; // Set deallocator
    if (PyType_Ready(&FoldcompDatabaseType) < 0)
        return NULL;

    m = PyModule_Create(&foldcompmodule);
    if (m == NULL)
        return NULL;

    // Add FoldcompDatabase type to the module
    Py_INCREF(&FoldcompDatabaseType);
    if (PyModule_AddObject(m, "FoldcompDatabase", (PyObject *)&FoldcompDatabaseType) < 0) {
        Py_DECREF(&FoldcompDatabaseType);
        Py_DECREF(m);
        return NULL;
    }
    
    // Add custom exception
    FoldcompError = PyErr_NewException("foldcomp.FoldcompError", NULL, NULL);
    Py_XINCREF(FoldcompError); // PyModule_AddObject steals a reference, so INCREF if we keep a static pointer
    if (PyModule_AddObject(m, "FoldcompError", FoldcompError) < 0) {
        Py_XDECREF(FoldcompError);
        Py_DECREF(m); // FoldcompDatabaseType already added, DECREF it too if error here
        Py_DECREF(&FoldcompDatabaseType); 
        return NULL;
    }

    return m;
}
