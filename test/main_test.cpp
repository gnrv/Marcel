// CppUnit test runner, integrated with CTest.
//
// Usage:
//   marcel_tests             — run every registered suite
//   marcel_tests <SuiteName> — run one named suite (this is what each
//                              add_test() entry in CMakeLists.txt does)
//
// Each suite registers itself twice (see e.g. DpiInfoTest.cpp):
//   CPPUNIT_TEST_SUITE_REGISTRATION(X);              — into the default registry
//   CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(X, "X");   — into a named registry
#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <iostream>

int main(int argc, char **argv)
{
    CppUnit::TextUi::TestRunner runner;
    if (argc > 1) {
        CppUnit::TestFactoryRegistry &registry =
            CppUnit::TestFactoryRegistry::getRegistry(argv[1]);
        runner.addTest(registry.makeTest());
    } else {
        runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
    }
    runner.setOutputter(new CppUnit::CompilerOutputter(&runner.result(), std::cerr));
    bool success = runner.run("", false);
    return success ? 0 : 1;
}
