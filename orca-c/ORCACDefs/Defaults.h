/*
 * Defaults.h -- ORCA/C platform macros for the Apple IIgs / GNO environment.
 *
 * This file is automatically processed before every ORCA/C compilation.
 * It defines macros that identify the platform and execution environment.
 *
 * To suppress these definitions, use #undef after the implicit include,
 * or compile with -i (ignore symbol files) and provide your own prefix file
 * via -P.
 */

#ifndef __appleiigs__
#define __appleiigs__  1    /* target is the Apple IIgs                  */
#endif

#ifndef __GNO__
#define __GNO__        1    /* GNO/ME or GoldenGate environment          */
#endif
