#include "test_x11_errors.h"
#include "x11_errors.h"

using namespace revere;

REGISTER_TEST_FIXTURE(test_x11_errors);

// What an X server gave for "RANDR" on the machine the bug was seen on: major opcode 140. The error base differs
// from server to server; 147 here.
static const int MAJOR = 140;
static const int ERROR_BASE = 147;

void test_x11_errors::test_randr_request_errors_are_randr()
{
    // RRGetOutputInfo (minor 9) for an output that has just gone: BadRROutput, the error base itself.
    RTF_ASSERT(x_error_is_randr(MAJOR, ERROR_BASE, MAJOR, ERROR_BASE));
    // A core error on a RandR request counts too: BadMatch (8), BadValue (2).
    RTF_ASSERT(x_error_is_randr(MAJOR, 8, MAJOR, ERROR_BASE));
    RTF_ASSERT(x_error_is_randr(MAJOR, 2, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_randr_error_codes_are_randr()
{
    // BadRROutput, BadRRCrtc, BadRRMode, BadRRProvider, whatever request they came back on.
    for(int i = 0; i < RANDR_ERROR_COUNT; ++i)
        RTF_ASSERT(x_error_is_randr(12, ERROR_BASE + i, MAJOR, ERROR_BASE));
    // One past the last of them belongs to another extension.
    RTF_ASSERT(!x_error_is_randr(12, ERROR_BASE + RANDR_ERROR_COUNT, MAJOR, ERROR_BASE));
    RTF_ASSERT(!x_error_is_randr(12, ERROR_BASE - 1, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_other_errors_are_not()
{
    // BadWindow (3) on ConfigureWindow (12), BadDrawable (9) on PutImage (72): still fatal, as before.
    RTF_ASSERT(!x_error_is_randr(12, 3, MAJOR, ERROR_BASE));
    RTF_ASSERT(!x_error_is_randr(72, 9, MAJOR, ERROR_BASE));
    // Another extension's request.
    RTF_ASSERT(!x_error_is_randr(139, 8, MAJOR, ERROR_BASE));
}

void test_x11_errors::test_no_randr_on_the_server()
{
    // XQueryExtension found no RandR: nothing is taken for a RandR error, not even request code 0.
    RTF_ASSERT(!x_error_is_randr(0, 0, 0, 0));
    RTF_ASSERT(!x_error_is_randr(140, 147, 0, 0));
    RTF_ASSERT(!x_error_is_randr(0, 2, 0, 0));
}
