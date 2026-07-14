// check.h — validate one loaded network, collecting EVERY problem rather than
// stopping at the first (that's what the Diagnostic type in error.h is for).
// The command layer turns a non-empty error list into exit code 1.
//
// This checks what wirebard OWNS — address allocation and structural
// invariants. It does not try to out-validate wg-quick's own parser; that is
// wg's job, run at `apply` time.
#pragma once

#include <vector>

#include "error.h"
#include "project.h"

namespace wirebard {

// Problems found in `net`: a missing/bad `subnet` or `address` variable, a
// missing [Interface], peers whose address falls outside the subnet or hits
// the network/broadcast/server address, and duplicate pubkeys or addresses.
// Empty result == clean.
std::vector<Diagnostic> check_network(const Network& net);

} // namespace wirebard
