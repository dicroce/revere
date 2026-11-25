#include "test_x11_errors.h"
#include "x11_errors.h"

using namespace revere;

REGISTER_TEST_FIXTURE(test_x11_errors);

// What an X server gave for "RANDR" on the machine the bug was seen on: major opcode 140. The error base differs
// from server to server; 147 here.
static const int MAJOR = 140;
static const int ERROR_BASE = 147;

void test_x11_errors::test_core_errors_on_randr_requests_are_not()
{
    // The request doesn't decide it. BadWindow (3) from RRSelectInput, BadMatch (8), BadValue (2), BadAlloc (11):
    // core errors, fatal on a RandR request as on any other.
    for(int core : {2, 3, 8, 11})
        RTF_ASSERT(!x_error_is_randr(core, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_randr_error_codes_are_randr()
{
    // BadRROutput (what RRGetOutputInfo gives for an output that has just gone), BadRRCrtc, BadRRMode, BadRRProvider.
    for(int i = 0; i < RANDR_ERROR_COUNT; ++i)
        RTF_ASSERT(x_error_is_randr(ERROR_BASE + i, MAJOR, ERROR_BASE));
    // The codes on either side belong to other extensions.
    RTF_ASSERT(!x_error_is_randr(ERROR_BASE + RANDR_ERROR_COUNT, MAJOR, ERROR_BASE));
    RTF_ASSERT(!x_error_is_randr(ERROR_BASE - 1, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_other_errors_are_not()
{
    // BadWindow (3), BadDrawable (9), BadImplementation (17): still fatal, as before.
    RTF_ASSERT(!x_error_is_randr(3, MAJOR, ERROR_BASE));
    RTF_ASSERT(!x_error_is_randr(9, MAJOR, ERROR_BASE));
    RTF_ASSERT(!x_error_is_randr(17, MAJOR, ERROR_BASE));
    // RandR's major opcode is a request code, not an error code: an error numbered 140 isn't RandR's.
    RTF_ASSERT(!x_error_is_randr(MAJOR, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_no_randr_on_the_server()
{
    // XQueryExtension found no RandR: nothing is taken for a RandR error.
    RTF_ASSERT(!x_error_is_randr(0, 0, 0));
    RTF_ASSERT(!x_error_is_randr(147, 0, 0));
    RTF_ASSERT(!x_error_is_randr(2, 0, 0));
    RTF_ASSERT(!x_error_is_randr(1, 0, 0));
}
