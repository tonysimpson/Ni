import sys, os, StringIO


NO_SYS_GETFRAME = """using sys._getframe() fails with Psyco"""

#NO_THREAD = """XXX not reliable, check if Psyco is generally
#        unreliable with threads or if there is another problem"""

NO_PICKLE = """pickles function objects that Psyco rebinds"""

NO_SYS_EXC = """XXX Psyco does not set sys.exc_xxx upon exception"""


SKIP = {'test_gc': NO_SYS_GETFRAME,
#        'test_thread': NO_THREAD,
#        'test_asynchat': NO_THREAD,
        'test_extcall': 'prints to stdout a function object that Psyco rebinds',
        'test_descr': NO_PICKLE,
        'test_pickle': NO_PICKLE,
        'test_cpickle': NO_PICKLE,
        'test_re': NO_PICKLE,
        'test_sre': NO_SYS_EXC,
        'test_string': NO_SYS_EXC,
        'test_unicode': NO_SYS_EXC,
        'test_inspect': 'gets confused with Psyco rebinding functions',
        'test_profilehooks': NO_SYS_GETFRAME,
        'test_profile': 'profiling does not see all functions run by Psyco',
        'test_popen2': 'gets confused by Psyco debugging output to stderr',
        'test_repr': 'self-nested tuples and lists not supported',
        }

GROUP_TESTS = 4    # number of tests to run per Python process


# Per-module user-filtered warnings don't work correctly
# because sys._getframe() cannot see the Psyco frames.
# Some tests expect an OverflowError to be raised when
# an overflow is detected. To work around this, we
# globally force these to raise an error.
import warnings
warnings.filterwarnings("error", "", OverflowWarning, "")


for dir in sys.path:
    file = os.path.join(dir, "string.py")
    if os.path.isfile(file):
        test = os.path.join(dir, "test")
        if os.path.isdir(test):
            # Add the "test" directory to PYTHONPATH.
            sys.path = sys.path + [test]

import regrtest, test_support

repeat_counter = 4


def alltests():
    import random
    # randomize the list of tests, but try to ensure that we start with
    # not-already-seen tests and only after go on with the rest
    filename = "tmp_tests_passed"
    try:
        f = open(filename)
        tests_passed = eval(f.read())
        f.close()
    except IOError:
        tests_passed = {}
    testlist = regrtest.findtests()
    testlist = [test for test in testlist if test not in tests_passed]
    random.shuffle(testlist)
    testlist1 = tests_passed.keys()
    random.shuffle(testlist1)
    print '\t'.join(['Scheduled tests:']+testlist)
    if testlist1:
        print '%d more tests were already passed and are scheduled to run thereafter.' % len(testlist1)
    testlist += testlist1
    while testlist:
        print '='*40
        tests1 = testlist[:GROUP_TESTS]
        del testlist[:GROUP_TESTS]
        err = os.system('"%s" %s %s' % (sys.executable, sys.argv[0],
                                          ' '.join(tests1)))
        if err:
            print '*** exited with error code', err
            return err
        for test in tests1:
            tests_passed[test] = 1
        f = open(filename, 'w')
        f.write(repr(tests_passed))
        f.close()
    print "="*60
    print
    print "Classic Regression Tests with Psyco successfully completed."
    print "All tests that succeeded twice in the same Python process"
    print "also succeeded %d more times with Psyco activated." % repeat_counter
    print
    try:
        os.unlink(filename)
    except:
        pass
    import _psyco
    print "Psyco compilation flags:",
    d = _psyco.__dict__
    if 'ALL_CHECKS' not in d:
        print "Release mode",
    for key in d:
        if key == key.upper() and isinstance(d[key], int):
            print "%s=%d" % (key, d[key]),
    print

def python_check(test):
    if test in SKIP:
        print '%s skipped -- %s' % (test, SKIP[test])
        return 0
    for i in range(min(repeat_counter, 2)):
        print '%s, Python iteration %d' % (test, i+1)
        ok = regrtest.runtest(test, 0, 0, 0)
        special_cleanup()
        if ok <= 0:
            return 0   # skipped or failed -- don't test with Psyco
    return 1

def main(testlist, verbose=0, use_resources=None):
    if use_resources is None:
        use_resources = []
    test_support.verbose = verbose      # Tell tests to be moderately quiet
    test_support.use_resources = use_resources
    
    if isinstance(testlist, str):
        testlist = [testlist]
    testlist = filter(python_check, testlist)

    # Psyco selective compilation is only activated here
    import psyco
    #print "sleeping, time for a Ctrl-C !..."
    #import time; time.sleep(1.5)


    for test in testlist:
        for i in range(repeat_counter):
            print '%s, Psyco iteration %d' % (test, i+1)
            ok = regrtest.runtest(test, 0, 0, 0)
            special_cleanup()
            if ok == 0:
                return 0
            elif ok < 0:
                break
    return 1

def special_cleanup():
    try:
        dircache = sys.modules['dircache']
    except KeyError:
        pass
    else:
        for key in dircache.cache.keys():
            del dircache.cache[key]


if __name__ == '__main__':
    if len(sys.argv) <= 1:
        sys.exit(alltests() or 0)
    else:
        try:
            err = not main(sys.argv[1:])
        finally:
            # Write psyco.dump
            import _psyco
            if hasattr(_psyco, 'dumpcodebuf'):
                print "Dumping code buffers..."
                _psyco.dumpcodebuf()
        if err:
            sys.exit(2)