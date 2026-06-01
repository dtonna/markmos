# CMake generated Testfile for 
# Source directory: /Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine
# Build directory: /Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/build-xcode
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[backend_concept_tests]=] "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/build-xcode/Debug/backend_concept_tests")
  set_tests_properties([=[backend_concept_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;391;add_test;/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[backend_concept_tests]=] "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/build-xcode/Release/backend_concept_tests")
  set_tests_properties([=[backend_concept_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;391;add_test;/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[backend_concept_tests]=] "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/build-xcode/MinSizeRel/backend_concept_tests")
  set_tests_properties([=[backend_concept_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;391;add_test;/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[backend_concept_tests]=] "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/build-xcode/RelWithDebInfo/backend_concept_tests")
  set_tests_properties([=[backend_concept_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;391;add_test;/Users/noppadolanuroje/Documents/Projects/engines2/markmos/engine/CMakeLists.txt;0;")
else()
  add_test([=[backend_concept_tests]=] NOT_AVAILABLE)
endif()
