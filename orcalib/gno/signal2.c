#ifdef __ORCAC__
segment "libc_sys__";
#endif

/* defaults.h defines __GNO__, __appleiigs__, and the GNO include paths.
   On a real GNO system the occ wrapper includes this automatically; for
   GoldenGate cross-compilation we must include it explicitly. */
#include <defaults.h>

#include <gno/kerntool.h>
#include <errno.h>

int raise(int sig) {
	return Kkill(Kgetpid(), sig, &errno);
}
