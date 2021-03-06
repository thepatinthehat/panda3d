/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file py_panda.cxx
 * @author drose
 * @date 2005-07-04
 */

#include "py_panda.h"
#include "config_interrogatedb.h"
#include "executionEnvironment.h"

#ifdef HAVE_PYTHON

PyMemberDef standard_type_members[] = {
  {(char *)"this", (sizeof(void*) == sizeof(int)) ? T_UINT : T_ULONGLONG, offsetof(Dtool_PyInstDef, _ptr_to_object), READONLY, (char *)"C++ 'this' pointer, if any"},
  {(char *)"this_ownership", T_BOOL, offsetof(Dtool_PyInstDef, _memory_rules), READONLY, (char *)"C++ 'this' ownership rules"},
  {(char *)"this_const", T_BOOL, offsetof(Dtool_PyInstDef, _is_const), READONLY, (char *)"C++ 'this' const flag"},
// {(char *)"this_signature", T_INT, offsetof(Dtool_PyInstDef, _signature),
// READONLY, (char *)"A type check signature"},
  {(char *)"this_metatype", T_OBJECT, offsetof(Dtool_PyInstDef, _My_Type), READONLY, (char *)"The dtool meta object"},
  {nullptr}  /* Sentinel */
};

static RuntimeTypeMap runtime_type_map;
static RuntimeTypeSet runtime_type_set;
static NamedTypeMap named_type_map;

/**

 */
void DTOOL_Call_ExtractThisPointerForType(PyObject *self, Dtool_PyTypedObject *classdef, void **answer) {
  if (DtoolInstance_Check(self)) {
    *answer = DtoolInstance_UPCAST(self, *classdef);
  } else {
    *answer = nullptr;
  }
}

/**
 * This is a support function for the Python bindings: it extracts the
 * underlying C++ pointer of the given type for a given Python object.  If it
 * was of the wrong type, raises an AttributeError.
 */
bool Dtool_Call_ExtractThisPointer(PyObject *self, Dtool_PyTypedObject &classdef, void **answer) {
  if (self == nullptr || !DtoolInstance_Check(self) || DtoolInstance_VOID_PTR(self) == nullptr) {
    Dtool_Raise_TypeError("C++ object is not yet constructed, or already destructed.");
    return false;
  }

  *answer = DtoolInstance_UPCAST(self, classdef);
  return true;
}

/**
 * The same thing as Dtool_Call_ExtractThisPointer, except that it performs
 * the additional check that the pointer is a non-const pointer.  This is
 * called by function wrappers for functions of which all overloads are non-
 * const, and saves a bit of code.
 *
 * The extra method_name argument is used in formatting the error message.
 */
bool Dtool_Call_ExtractThisPointer_NonConst(PyObject *self, Dtool_PyTypedObject &classdef,
                                            void **answer, const char *method_name) {

  if (self == nullptr || !DtoolInstance_Check(self) || DtoolInstance_VOID_PTR(self) == nullptr) {
    Dtool_Raise_TypeError("C++ object is not yet constructed, or already destructed.");
    return false;
  }

  if (DtoolInstance_IS_CONST(self)) {
    // All overloads of this function are non-const.
    PyErr_Format(PyExc_TypeError,
                 "Cannot call %s() on a const object.",
                 method_name);
    return false;
  }

  *answer = DtoolInstance_UPCAST(self, classdef);
  return true;
}

/**
 * Extracts the C++ pointer for an object, given its Python wrapper object,
 * for passing as the parameter to a C++ function.
 *
 * self is the Python wrapper object in question.
 *
 * classdef is the Python class wrapper for the C++ class in which the this
 * pointer should be returned.  (This may require an upcast operation, if self
 * is not already an instance of classdef.)
 *
 * param and function_name are used for error reporting only, and describe the
 * particular function and parameter index for this parameter.
 *
 * const_ok is true if the function is declared const and can therefore be
 * called with either a const or non-const "this" pointer, or false if the
 * function is declared non-const, and can therefore be called with only a
 * non-const "this" pointer.
 *
 * The return value is the C++ pointer that was extracted, or NULL if there
 * was a problem (in which case the Python exception state will have been
 * set).
 */
void *
DTOOL_Call_GetPointerThisClass(PyObject *self, Dtool_PyTypedObject *classdef,
                               int param, const string &function_name, bool const_ok,
                               bool report_errors) {
  // if (PyErr_Occurred()) { return nullptr; }
  if (self == nullptr) {
    if (report_errors) {
      return Dtool_Raise_TypeError("self is nullptr");
    }
    return nullptr;
  }

  if (DtoolInstance_Check(self)) {
    void *result = DtoolInstance_UPCAST(self, *classdef);

    if (result != nullptr) {
      if (const_ok || !DtoolInstance_IS_CONST(self)) {
        return result;
      }

      if (report_errors) {
        return PyErr_Format(PyExc_TypeError,
                            "%s() argument %d may not be const",
                            function_name.c_str(), param);
      }
      return nullptr;
    }
  }

  if (report_errors) {
    return Dtool_Raise_ArgTypeError(self, param, function_name.c_str(), classdef->_PyType.tp_name);
  }

  return nullptr;
}

/**
 * This is similar to a PyErr_Occurred() check, except that it also checks
 * Notify to see if an assertion has occurred.  If that is the case, then it
 * raises an AssertionError.
 *
 * Returns true if there is an active exception, false otherwise.
 *
 * In the NDEBUG case, this is simply a #define to _PyErr_OCCURRED() (which is
 * an undocumented inline version of PyErr_Occurred()).
 */
bool _Dtool_CheckErrorOccurred() {
  if (_PyErr_OCCURRED()) {
    return true;
  }
  if (Notify::ptr()->has_assert_failed()) {
    Dtool_Raise_AssertionError();
    return true;
  }
  return false;
}

/**
 * Raises an AssertionError containing the last thrown assert message, and
 * clears the assertion flag.  Returns NULL.
 */
PyObject *Dtool_Raise_AssertionError() {
  Notify *notify = Notify::ptr();
#if PY_MAJOR_VERSION >= 3
  PyObject *message = PyUnicode_FromString(notify->get_assert_error_message().c_str());
#else
  PyObject *message = PyString_FromString(notify->get_assert_error_message().c_str());
#endif
  Py_INCREF(PyExc_AssertionError);
  PyErr_Restore(PyExc_AssertionError, message, nullptr);
  notify->clear_assert_failed();
  return nullptr;
}

/**
 * Raises a TypeError with the given message, and returns NULL.
 */
PyObject *Dtool_Raise_TypeError(const char *message) {
  // PyErr_Restore is what PyErr_SetString would have ended up calling
  // eventually anyway, so we might as well just get to the point.
  Py_INCREF(PyExc_TypeError);
#if PY_MAJOR_VERSION >= 3
  PyErr_Restore(PyExc_TypeError, PyUnicode_FromString(message), nullptr);
#else
  PyErr_Restore(PyExc_TypeError, PyString_FromString(message), nullptr);
#endif
  return nullptr;
}

/**
 * Raises a TypeError of the form: function_name() argument n must be type,
 * not type for a given object passed to a function.
 *
 * Always returns NULL so that it can be conveniently used as a return
 * expression for wrapper functions that return a PyObject pointer.
 */
PyObject *Dtool_Raise_ArgTypeError(PyObject *obj, int param, const char *function_name, const char *type_name) {
#if PY_MAJOR_VERSION >= 3
  PyObject *message = PyUnicode_FromFormat(
#else
  PyObject *message = PyString_FromFormat(
#endif
    "%s() argument %d must be %s, not %s",
    function_name, param, type_name,
    Py_TYPE(obj)->tp_name);

  Py_INCREF(PyExc_TypeError);
  PyErr_Restore(PyExc_TypeError, message, nullptr);
  return nullptr;
}

/**
 * Raises an AttributeError of the form: 'type' has no attribute 'attr'
 *
 * Always returns NULL so that it can be conveniently used as a return
 * expression for wrapper functions that return a PyObject pointer.
 */
PyObject *Dtool_Raise_AttributeError(PyObject *obj, const char *attribute) {
#if PY_MAJOR_VERSION >= 3
  PyObject *message = PyUnicode_FromFormat(
#else
  PyObject *message = PyString_FromFormat(
#endif
    "'%.100s' object has no attribute '%.200s'",
    Py_TYPE(obj)->tp_name, attribute);

  Py_INCREF(PyExc_TypeError);
  PyErr_Restore(PyExc_TypeError, message, nullptr);
  return nullptr;
}

/**
 * Raises a TypeError of the form: Arguments must match: <list of overloads>
 *
 * However, in release builds, this instead is defined to a function that just
 * prints out a generic message, to help reduce the amount of strings in the
 * compiled library.
 *
 * Always returns NULL so that it can be conveniently used as a return
 * expression for wrapper functions that return a PyObject pointer.
 */
PyObject *_Dtool_Raise_BadArgumentsError() {
  return Dtool_Raise_TypeError("arguments do not match any function overload");
}

/**
 * Convenience method that checks for exceptions, and if one occurred, returns
 * NULL, otherwise Py_None.
 */
PyObject *_Dtool_Return_None() {
  if (UNLIKELY(_PyErr_OCCURRED())) {
    return nullptr;
  }
#ifndef NDEBUG
  if (UNLIKELY(Notify::ptr()->has_assert_failed())) {
    return Dtool_Raise_AssertionError();
  }
#endif
  Py_INCREF(Py_None);
  return Py_None;
}

/**
 * Convenience method that checks for exceptions, and if one occurred, returns
 * NULL, otherwise the given boolean value as a PyObject *.
 */
PyObject *Dtool_Return_Bool(bool value) {
  if (UNLIKELY(_PyErr_OCCURRED())) {
    return nullptr;
  }
#ifndef NDEBUG
  if (UNLIKELY(Notify::ptr()->has_assert_failed())) {
    return Dtool_Raise_AssertionError();
  }
#endif
  PyObject *result = (value ? Py_True : Py_False);
  Py_INCREF(result);
  return result;
}

/**
 * Convenience method that checks for exceptions, and if one occurred, returns
 * NULL, otherwise the given return value.  Its reference count is not
 * increased.
 */
PyObject *_Dtool_Return(PyObject *value) {
  if (UNLIKELY(_PyErr_OCCURRED())) {
    return nullptr;
  }
#ifndef NDEBUG
  if (UNLIKELY(Notify::ptr()->has_assert_failed())) {
    return Dtool_Raise_AssertionError();
  }
#endif
  return value;
}

/**
 * Creates a Python 3.4-style enum type.  Steals reference to 'names'.
 */
PyObject *Dtool_EnumType_Create(const char *name, PyObject *names, const char *module) {
  static PyObject *enum_class = nullptr;
  static PyObject *enum_meta = nullptr;
  static PyObject *enum_create = nullptr;
  if (enum_meta == nullptr) {
    PyObject *enum_module = PyImport_ImportModule("enum");
    nassertr_always(enum_module != nullptr, nullptr);

    enum_class = PyObject_GetAttrString(enum_module, "Enum");
    enum_meta = PyObject_GetAttrString(enum_module, "EnumMeta");
    enum_create = PyObject_GetAttrString(enum_meta, "_create_");
    nassertr(enum_meta != nullptr, nullptr);
  }

  PyObject *result = PyObject_CallFunction(enum_create, (char *)"OsN", enum_class, name, names);
  nassertr(result != nullptr, nullptr);
  if (module != nullptr) {
    PyObject *modstr = PyUnicode_FromString(module);
    PyObject_SetAttrString(result, "__module__", modstr);
    Py_DECREF(modstr);
  }
  return result;
}

/**

 */
PyObject *DTool_CreatePyInstanceTyped(void *local_this_in, Dtool_PyTypedObject &known_class_type, bool memory_rules, bool is_const, int type_index) {
  // We can't do the NULL check here like in DTool_CreatePyInstance, since the
  // caller will have to get the type index to pass to this function to begin
  // with.  That code probably would have crashed by now if it was really NULL
  // for whatever reason.
  nassertr(local_this_in != nullptr, nullptr);

  // IF the class is possibly a run time typed object
  if (type_index > 0) {
    // get best fit class...
    Dtool_PyTypedObject *target_class = Dtool_RuntimeTypeDtoolType(type_index);
    if (target_class != nullptr) {
      // cast to the type...
      void *new_local_this = target_class->_Dtool_DowncastInterface(local_this_in, &known_class_type);
      if (new_local_this != nullptr) {
        // ask class to allocate an instance..
        Dtool_PyInstDef *self = (Dtool_PyInstDef *) target_class->_PyType.tp_new(&target_class->_PyType, nullptr, nullptr);
        if (self != nullptr) {
          self->_ptr_to_object = new_local_this;
          self->_memory_rules = memory_rules;
          self->_is_const = is_const;
          // self->_signature = PY_PANDA_SIGNATURE;
          self->_My_Type = target_class;
          return (PyObject *)self;
        }
      }
    }
  }

  // if we get this far .. just wrap the thing in the known type ?? better
  // than aborting...I guess....
  Dtool_PyInstDef *self = (Dtool_PyInstDef *) known_class_type._PyType.tp_new(&known_class_type._PyType, nullptr, nullptr);
  if (self != nullptr) {
    self->_ptr_to_object = local_this_in;
    self->_memory_rules = memory_rules;
    self->_is_const = is_const;
    // self->_signature = PY_PANDA_SIGNATURE;
    self->_My_Type = &known_class_type;
  }
  return (PyObject *)self;
}

// DTool_CreatePyInstance .. wrapper function to finalize the existance of a
// general dtool py instance..
PyObject *DTool_CreatePyInstance(void *local_this, Dtool_PyTypedObject &in_classdef, bool memory_rules, bool is_const) {
  if (local_this == nullptr) {
    // This is actually a very common case, so let's allow this, but return
    // Py_None consistently.  This eliminates code in the wrappers.
    Py_INCREF(Py_None);
    return Py_None;
  }

  Dtool_PyTypedObject *classdef = &in_classdef;
  Dtool_PyInstDef *self = (Dtool_PyInstDef *) classdef->_PyType.tp_new(&classdef->_PyType, nullptr, nullptr);
  if (self != nullptr) {
    self->_ptr_to_object = local_this;
    self->_memory_rules = memory_rules;
    self->_is_const = is_const;
    self->_My_Type = classdef;
  }
  return (PyObject *)self;
}

// Th Finalizer for simple instances..
int DTool_PyInit_Finalize(PyObject *self, void *local_this, Dtool_PyTypedObject *type, bool memory_rules, bool is_const) {
  // lets put some code in here that checks to see the memory is properly
  // configured.. prior to my call ..

  ((Dtool_PyInstDef *)self)->_My_Type = type;
  ((Dtool_PyInstDef *)self)->_ptr_to_object = local_this;
  ((Dtool_PyInstDef *)self)->_memory_rules = memory_rules;
  ((Dtool_PyInstDef *)self)->_is_const = is_const;
  return 0;
}

// A helper function to glue method definition together .. that can not be
// done at code generation time because of multiple generation passes in
// interrogate..
void Dtool_Accum_MethDefs(PyMethodDef in[], MethodDefmap &themap) {
  for (; in->ml_name != nullptr; in++) {
    if (themap.find(in->ml_name) == themap.end()) {
      themap[in->ml_name] = in;
    }
  }
}

// ** HACK ** alert.. Need to keep a runtime type dictionary ... that is
// forward declared of typed object.  We rely on the fact that typed objects
// are uniquly defined by an integer.
void
RegisterNamedClass(const string &name, Dtool_PyTypedObject &otype) {
  pair<NamedTypeMap::iterator, bool> result =
    named_type_map.insert(NamedTypeMap::value_type(name, &otype));

  if (!result.second) {
    // There was already a class with this name in the dictionary.
    interrogatedb_cat.warning()
      << "Double definition for class " << name << "\n";
  }
}

void
RegisterRuntimeTypedClass(Dtool_PyTypedObject &otype) {
  int type_index = otype._type.get_index();

  if (type_index == 0) {
    interrogatedb_cat.warning()
      << "Class " << otype._PyType.tp_name
      << " has a zero TypeHandle value; check that init_type() is called.\n";

  } else if (type_index < 0 || type_index >= TypeRegistry::ptr()->get_num_typehandles()) {
    interrogatedb_cat.warning()
      << "Class " << otype._PyType.tp_name
      << " has an illegal TypeHandle value; check that init_type() is called.\n";

  } else {
    pair<RuntimeTypeMap::iterator, bool> result =
      runtime_type_map.insert(RuntimeTypeMap::value_type(type_index, &otype));
    if (!result.second) {
      // There was already an entry in the dictionary for type_index.
      Dtool_PyTypedObject *other_type = (*result.first).second;
      interrogatedb_cat.warning()
        << "Classes " << otype._PyType.tp_name
        << " and " << other_type->_PyType.tp_name
        << " share the same TypeHandle value (" << type_index
        << "); check class definitions.\n";

    } else {
      runtime_type_set.insert(type_index);
    }
  }
}

Dtool_PyTypedObject *
LookupNamedClass(const string &name) {
  NamedTypeMap::const_iterator it;
  it = named_type_map.find(name);

  if (it == named_type_map.end()) {
    // Find a type named like this in the type registry.
    TypeHandle handle = TypeRegistry::ptr()->find_type(name);
    if (handle.get_index() > 0) {
      RuntimeTypeMap::const_iterator it2;
      it2 = runtime_type_map.find(handle.get_index());
      if (it2 != runtime_type_map.end()) {
        return it2->second;
      }
    }

    interrogatedb_cat.error()
      << "Attempt to use type " << name << " which has not yet been defined!\n";
    return nullptr;
  } else {
    return it->second;
  }
}

Dtool_PyTypedObject *
LookupRuntimeTypedClass(TypeHandle handle) {
  RuntimeTypeMap::const_iterator it;
  it = runtime_type_map.find(handle.get_index());

  if (it == runtime_type_map.end()) {
    interrogatedb_cat.error()
      << "Attempt to use type " << handle << " which has not yet been defined!\n";
    return nullptr;
  } else {
    return it->second;
  }
}

Dtool_PyTypedObject *Dtool_RuntimeTypeDtoolType(int type) {
  RuntimeTypeMap::iterator di = runtime_type_map.find(type);
  if (di != runtime_type_map.end()) {
    return di->second;
  } else {
    int type2 = get_best_parent_from_Set(type, runtime_type_set);
    di = runtime_type_map.find(type2);
    if (di != runtime_type_map.end()) {
      return di->second;
    }
  }
  return nullptr;
}

#if PY_MAJOR_VERSION >= 3
PyObject *Dtool_PyModuleInitHelper(LibraryDef *defs[], PyModuleDef *module_def) {
#else
PyObject *Dtool_PyModuleInitHelper(LibraryDef *defs[], const char *modulename) {
#endif
  // Check the version so we can print a helpful error if it doesn't match.
  string version = Py_GetVersion();

  if (version[0] != '0' + PY_MAJOR_VERSION ||
      version[2] != '0' + PY_MINOR_VERSION) {
    // Raise a helpful error message.  We can safely do this because the
    // signature and behavior for PyErr_SetString has remained consistent.
    ostringstream errs;
    errs << "this module was compiled for Python "
         << PY_MAJOR_VERSION << "." << PY_MINOR_VERSION << ", which is "
         << "incompatible with Python " << version.substr(0, 3);
    string error = errs.str();
    PyErr_SetString(PyExc_ImportError, error.c_str());
    return nullptr;
  }

  // Initialize the types we define in py_panda.
  static bool dtool_inited = false;
  if (!dtool_inited) {
    dtool_inited = true;

    if (PyType_Ready(&Dtool_SequenceWrapper_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_SequenceWrapper)");
    }

    if (PyType_Ready(&Dtool_MutableSequenceWrapper_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MutableSequenceWrapper)");
    }

    if (PyType_Ready(&Dtool_MappingWrapper_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MappingWrapper)");
    }

    if (PyType_Ready(&Dtool_MutableMappingWrapper_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MutableMappingWrapper)");
    }

    if (PyType_Ready(&Dtool_MappingWrapper_Keys_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MappingWrapper_Keys)");
    }

    if (PyType_Ready(&Dtool_MappingWrapper_Values_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MappingWrapper_Values)");
    }

    if (PyType_Ready(&Dtool_MappingWrapper_Items_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_MappingWrapper_Items)");
    }

    if (PyType_Ready(&Dtool_GeneratorWrapper_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_GeneratorWrapper)");
    }

    if (PyType_Ready(&Dtool_StaticProperty_Type) < 0) {
      return Dtool_Raise_TypeError("PyType_Ready(Dtool_StaticProperty_Type)");
    }

    // Initialize the base class of everything.
    Dtool_PyModuleClassInit_DTOOL_SUPER_BASE(nullptr);
  }

  // the module level function inits....
  MethodDefmap functions;
  for (int xx = 0; defs[xx] != nullptr; xx++) {
    Dtool_Accum_MethDefs(defs[xx]->_methods, functions);
  }

  PyMethodDef *newdef = new PyMethodDef[functions.size() + 1];
  MethodDefmap::iterator mi;
  int offset = 0;
  for (mi = functions.begin(); mi != functions.end(); mi++, offset++) {
    newdef[offset] = *mi->second;
  }
  newdef[offset].ml_doc = nullptr;
  newdef[offset].ml_name = nullptr;
  newdef[offset].ml_meth = nullptr;
  newdef[offset].ml_flags = 0;

#if PY_MAJOR_VERSION >= 3
  module_def->m_methods = newdef;
  PyObject *module = PyModule_Create(module_def);
#else
  PyObject *module = Py_InitModule((char *)modulename, newdef);
#endif

  if (module == nullptr) {
#if PY_MAJOR_VERSION >= 3
    return Dtool_Raise_TypeError("PyModule_Create returned NULL");
#else
    return Dtool_Raise_TypeError("Py_InitModule returned NULL");
#endif
  }

  // MAIN_DIR needs to be set very early; this seems like a convenient place
  // to do that.  Perhaps we'll find a better place for this in the future.
  static bool initialized_main_dir = false;
  if (!initialized_main_dir) {
    if (interrogatedb_cat.is_debug()) {
      // Good opportunity to print this out once, at startup.
      interrogatedb_cat.debug()
        << "Python " << version << "\n";
    }

    // Grab the __main__ module.
    PyObject *main_module = PyImport_ImportModule("__main__");
    if (main_module == nullptr) {
      interrogatedb_cat.warning() << "Unable to import __main__\n";
    }

    // Extract the __file__ attribute, if present.
    Filename main_dir;
    PyObject *file_attr = PyObject_GetAttrString(main_module, "__file__");
    if (file_attr == nullptr) {
      // Must be running in the interactive interpreter.  Use the CWD.
      main_dir = ExecutionEnvironment::get_cwd();
    } else {
#if PY_MAJOR_VERSION >= 3
      Py_ssize_t length;
      wchar_t *buffer = PyUnicode_AsWideCharString(file_attr, &length);
      if (buffer != nullptr) {
        main_dir = Filename::from_os_specific_w(std::wstring(buffer, length));
        main_dir.make_absolute();
        main_dir = main_dir.get_dirname();
        PyMem_Free(buffer);
      }
#else
      char *buffer;
      Py_ssize_t length;
      if (PyString_AsStringAndSize(file_attr, &buffer, &length) != -1) {
        main_dir = Filename::from_os_specific(std::string(buffer, length));
        main_dir.make_absolute();
        main_dir = main_dir.get_dirname();
      }
#endif
      else {
        interrogatedb_cat.warning() << "Invalid string for __main__.__file__\n";
      }
    }
    ExecutionEnvironment::shadow_environment_variable("MAIN_DIR", main_dir.to_os_specific());
    PyErr_Clear();
    initialized_main_dir = true;
  }

  PyModule_AddIntConstant(module, "Dtool_PyNativeInterface", 1);
  return module;
}

// HACK.... Be careful Dtool_BorrowThisReference This function can be used to
// grab the "THIS" pointer from an object and use it Required to support
// historical inheritance in the form of "is this instance of"..
PyObject *Dtool_BorrowThisReference(PyObject *self, PyObject *args) {
  PyObject *from_in = nullptr;
  PyObject *to_in = nullptr;
  if (PyArg_UnpackTuple(args, "Dtool_BorrowThisReference", 2, 2, &to_in, &from_in)) {

    if (DtoolInstance_Check(from_in) && DtoolInstance_Check(to_in)) {
      Dtool_PyInstDef *from = (Dtool_PyInstDef *) from_in;
      Dtool_PyInstDef *to = (Dtool_PyInstDef *) to_in;

      // if (PyObject_TypeCheck(to_in, Py_TYPE(from_in))) {
      if (from->_My_Type == to->_My_Type) {
        to->_memory_rules = false;
        to->_is_const = from->_is_const;
        to->_ptr_to_object = from->_ptr_to_object;

        Py_INCREF(Py_None);
        return Py_None;
      }

      return PyErr_Format(PyExc_TypeError, "types %s and %s do not match",
                          Py_TYPE(from)->tp_name, Py_TYPE(to)->tp_name);
    } else {
      return Dtool_Raise_TypeError("One of these does not appear to be DTOOL Instance ??");
    }
  }
  return nullptr;
}

// We do expose a dictionay for dtool classes .. this should be removed at
// some point..
EXPCL_INTERROGATEDB PyObject *Dtool_AddToDictionary(PyObject *self1, PyObject *args) {
  PyObject *self;
  PyObject *subject;
  PyObject *key;
  if (PyArg_ParseTuple(args, "OSO", &self, &key, &subject)) {
    PyObject *dict = ((PyTypeObject *)self)->tp_dict;
    if (dict == nullptr || !PyDict_Check(dict)) {
      return Dtool_Raise_TypeError("No dictionary On Object");
    } else {
      PyDict_SetItem(dict, key, subject);
    }
  }
  if (PyErr_Occurred()) {
    return nullptr;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

/**
 * This is a support function for a synthesized __copy__() method from a C++
 * make_copy() method.
 */
PyObject *copy_from_make_copy(PyObject *self, PyObject *noargs) {
  PyObject *callable = PyObject_GetAttrString(self, "make_copy");
  if (callable == nullptr) {
    return nullptr;
  }
  PyObject *result = _PyObject_CallNoArg(callable);
  Py_DECREF(callable);
  return result;
}

/**
 * This is a support function for a synthesized __copy__() method from a C++
 * copy constructor.
 */
PyObject *copy_from_copy_constructor(PyObject *self, PyObject *noargs) {
  PyObject *callable = (PyObject *)Py_TYPE(self);
  return _PyObject_FastCall(callable, &self, 1);
}

/**
 * This is a support function for a synthesized __deepcopy__() method for any
 * class that has a __copy__() method.  The sythethic method simply invokes
 * __copy__().
 */
PyObject *map_deepcopy_to_copy(PyObject *self, PyObject *args) {
  PyObject *callable = PyObject_GetAttrString(self, "__copy__");
  if (callable == nullptr) {
    return nullptr;
  }
  PyObject *result = _PyObject_CallNoArg(callable);
  Py_DECREF(callable);
  return result;
}

/**
 * A more efficient version of PyArg_ParseTupleAndKeywords for the special
 * case where there is only a single PyObject argument.
 */
bool Dtool_ExtractArg(PyObject **result, PyObject *args, PyObject *kwds,
                      const char *keyword) {

  if (PyTuple_GET_SIZE(args) == 1) {
    if (kwds == nullptr || PyDict_GET_SIZE(kwds) == 0) {
      *result = PyTuple_GET_ITEM(args, 0);
      return true;
    }
  } else if (PyTuple_GET_SIZE(args) == 0) {
    PyObject *key;
    Py_ssize_t ppos = 0;
    if (kwds != nullptr && PyDict_GET_SIZE(kwds) == 1 &&
        PyDict_Next(kwds, &ppos, &key, result)) {
      // We got the item, we just need to make sure that it had the right key.
#if PY_VERSION_HEX >= 0x03060000
      return PyUnicode_CheckExact(key) && _PyUnicode_EqualToASCIIString(key, keyword);
#elif PY_MAJOR_VERSION >= 3
      return PyUnicode_CheckExact(key) && PyUnicode_CompareWithASCIIString(key, keyword) == 0;
#else
      return PyString_CheckExact(key) && strcmp(PyString_AS_STRING(key), keyword) == 0;
#endif
    }
  }

  return false;
}

/**
 * Variant of Dtool_ExtractArg that does not accept a keyword argument.
 */
bool Dtool_ExtractArg(PyObject **result, PyObject *args, PyObject *kwds) {
  if (PyTuple_GET_SIZE(args) == 1 &&
      (kwds == nullptr || PyDict_GET_SIZE(kwds) == 0)) {
    *result = PyTuple_GET_ITEM(args, 0);
    return true;
  }
  return false;
}

/**
 * A more efficient version of PyArg_ParseTupleAndKeywords for the special
 * case where there is only a single optional PyObject argument.
 *
 * Returns true if valid (including if there were 0 items), false if there was
 * an error, such as an invalid number of parameters.
 */
bool Dtool_ExtractOptionalArg(PyObject **result, PyObject *args, PyObject *kwds,
                              const char *keyword) {

  if (PyTuple_GET_SIZE(args) == 1) {
    if (kwds == nullptr || PyDict_GET_SIZE(kwds) == 0) {
      *result = PyTuple_GET_ITEM(args, 0);
      return true;
    }
  } else if (PyTuple_GET_SIZE(args) == 0) {
    if (kwds != nullptr && PyDict_GET_SIZE(kwds) == 1) {
      PyObject *key;
      Py_ssize_t ppos = 0;
      if (!PyDict_Next(kwds, &ppos, &key, result)) {
        return true;
      }

      // We got the item, we just need to make sure that it had the right key.
#if PY_VERSION_HEX >= 0x03060000
      return PyUnicode_CheckExact(key) && _PyUnicode_EqualToASCIIString(key, keyword);
#elif PY_MAJOR_VERSION >= 3
      return PyUnicode_CheckExact(key) && PyUnicode_CompareWithASCIIString(key, keyword) == 0;
#else
      return PyString_CheckExact(key) && strcmp(PyString_AS_STRING(key), keyword) == 0;
#endif
    } else {
      return true;
    }
  }

  return false;
}

/**
 * Variant of Dtool_ExtractOptionalArg that does not accept a keyword argument.
 */
bool Dtool_ExtractOptionalArg(PyObject **result, PyObject *args, PyObject *kwds) {
  if (kwds != nullptr && PyDict_GET_SIZE(kwds) != 0) {
    return false;
  }
  if (PyTuple_GET_SIZE(args) == 1) {
    *result = PyTuple_GET_ITEM(args, 0);
    return true;
  }
  return (PyTuple_GET_SIZE(args) == 0);
}

#endif  // HAVE_PYTHON
